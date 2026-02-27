/*
 * arda_mpi.cpp - MPI Cluster Coordinator for CuCLARK
 *
 * This program coordinates distributed metagenomic classification
 * across a cluster of Jetson Nano devices using MPI.
 *
 * Usage: ./bin/arda-mpi -c config/cluster.conf
 * (Automatically launches mpirun internally)
 *
 * All nodes run in parallel via MPI - no SSH coordination.
 * Requires passwordless SSH for MPI to work.
 *
 * Copyright 2024-2026
 * License: GNU GPL v3
 */

#include <mpi.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace std;

// =============================================================================
// CONSTANTS AND TAGS
// =============================================================================

// MPI message tags
const int TAG_CONFIG = 1;
const int TAG_RESULT_DATA = 2;

// Log levels
enum LogLevel { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 };

// =============================================================================
// DATA STRUCTURES
// =============================================================================

struct ClusterConfig {
    // Cluster nodes
    string master;
    vector<string> workers;

    // Paths
    string cuclark_dir;
    string database;
    string results_dir;

    // Per-node reads (hostname -> list of read files)
    // Single entry = single-end, two entries = paired-end
    map<string, vector<string>> reads;

    // Classification settings (mirrors arda.cpp ClassifyOptions)
    int kmer_size = 31;         // -k
    int batch_size = 32;        // -b
    int min_freq_target = -1;   // -t, -1 = unset
    int num_threads = -1;       // -n, -1 = unset
    int num_devices = -1;       // -d (inside classify), -1 = unset
    int gap_iteration = -1;     // -g, -1 = unset
    string sampling_factor;     // -s, "" = unset
    bool tsk = false;           // --tsk
    bool extended = false;      // --extended
    bool gzipped = false;       // --gzipped
    bool verbose = false;       // --verbose

    // Options
    bool master_processes_reads = true;
    bool keep_local_results = true;

    // Logging
    LogLevel log_level = LOG_INFO;
    string log_file = "cluster_run.log";
    bool show_progress = true;
};

struct NodeResult {
    string hostname;
    bool success = false;
    string result_file;
    string abundance_file;
    int reads_processed = 0;
    int reads_classified = 0;
    double elapsed_seconds = 0.0;
    string error_message;

    // Serialize to string for MPI transfer
    string serialize() const {
        ostringstream oss;
        oss << hostname << "|"
            << (success ? "1" : "0") << "|"
            << result_file << "|"
            << abundance_file << "|"
            << reads_processed << "|"
            << reads_classified << "|"
            << elapsed_seconds << "|"
            << error_message;
        return oss.str();
    }

    // Deserialize from string
    static NodeResult deserialize(const string& data) {
        NodeResult r;
        istringstream iss(data);
        string token;

        getline(iss, r.hostname, '|');
        getline(iss, token, '|'); r.success = (token == "1");
        getline(iss, r.result_file, '|');
        getline(iss, r.abundance_file, '|');
        getline(iss, token, '|'); r.reads_processed = stoi(token.empty() ? "0" : token);
        getline(iss, token, '|'); r.reads_classified = stoi(token.empty() ? "0" : token);
        getline(iss, token, '|'); r.elapsed_seconds = stod(token.empty() ? "0" : token);
        getline(iss, r.error_message, '|');

        return r;
    }
};

// =============================================================================
// GLOBAL STATE
// =============================================================================

static ClusterConfig g_config;
static ofstream g_logfile;
static int g_rank = 0;
static int g_world_size = 1;

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

static string get_timestamp() {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return string(buf);
}

static void log_message(LogLevel level, const string& message) {
    if (level < g_config.log_level) return;

    static const char* level_names[] = {"DEBUG", "INFO", "WARN", "ERROR"};

    // Only rank 0 logs to console and file
    if (g_rank == 0) {
        string formatted = "[" + get_timestamp() + "] [" + level_names[level] + "] " + message;

        if (level >= LOG_WARN) {
            cerr << (level == LOG_ERROR ? "\033[31m" : "\033[33m") << formatted << "\033[0m" << endl;
        } else if (g_config.show_progress || level == LOG_INFO) {
            cout << formatted << endl;
        }

        if (g_logfile.is_open()) {
            g_logfile << formatted << endl;
            g_logfile.flush();
        }
    }
}

static void log_worker(const string& message) {
    // Workers log with their rank prefix
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    string formatted = "[" + get_timestamp() + "] [WORKER " + to_string(g_rank) +
                       " @ " + hostname + "] " + message;
    cout << formatted << endl;
}

static string trim(const string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

static string shell_escape(const string& s) {
    string result = "'";
    for (char c : s) {
        if (c == '\'') result += "'\"'\"'";
        else result += c;
    }
    result += "'";
    return result;
}

static string get_hostname_str() {
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    return string(hostname);
}

// Split a comma-separated string into trimmed tokens
static vector<string> split_csv(const string& s) {
    vector<string> result;
    istringstream iss(s);
    string token;
    while (getline(iss, token, ',')) {
        string t = trim(token);
        if (!t.empty()) result.push_back(t);
    }
    return result;
}

// =============================================================================
// INI CONFIG PARSER
// =============================================================================

class IniParser {
private:
    // section -> (key -> value)
    map<string, map<string, string>> sections;

public:
    bool parse(const string& filename) {
        ifstream file(filename);
        if (!file) return false;

        string line;
        string current_section;

        while (getline(file, line)) {
            string trimmed = trim(line);

            // Skip empty lines and comments
            if (trimmed.empty() || trimmed[0] == '#') continue;

            // Section header: [section]
            if (trimmed[0] == '[') {
                size_t end = trimmed.find(']');
                if (end == string::npos) continue;
                current_section = trim(trimmed.substr(1, end - 1));
                continue;
            }

            // Key = value (or key: value for backwards compat)
            size_t sep = trimmed.find('=');
            if (sep == string::npos) sep = trimmed.find(':');
            if (sep == string::npos) continue;

            string key = trim(trimmed.substr(0, sep));
            string value = trim(trimmed.substr(sep + 1));

            if (!current_section.empty() && !key.empty()) {
                sections[current_section][key] = value;
            }
        }

        return true;
    }

    string get_string(const string& section, const string& key,
                      const string& default_val = "") const {
        auto sit = sections.find(section);
        if (sit == sections.end()) return default_val;
        auto kit = sit->second.find(key);
        return (kit != sit->second.end()) ? kit->second : default_val;
    }

    int get_int(const string& section, const string& key,
                int default_val = 0) const {
        string val = get_string(section, key);
        if (val.empty()) return default_val;
        try { return stoi(val); }
        catch (...) { return default_val; }
    }

    bool get_bool(const string& section, const string& key,
                  bool default_val = false) const {
        string val = get_string(section, key);
        if (val.empty()) return default_val;
        return (val == "true" || val == "yes" || val == "1");
    }

    // Get all key-value pairs in a section
    map<string, string> get_section(const string& section) const {
        auto sit = sections.find(section);
        if (sit == sections.end()) return {};
        return sit->second;
    }
};

static bool load_config(const string& config_file) {
    IniParser parser;

    if (!parser.parse(config_file)) {
        cerr << "Error: Failed to parse config file: " << config_file << endl;
        return false;
    }

    // Load cluster settings
    g_config.master = parser.get_string("cluster", "master");
    g_config.workers = split_csv(parser.get_string("cluster", "workers"));

    if (g_config.master.empty()) {
        cerr << "Error: Master node not specified in config" << endl;
        return false;
    }

    if (g_config.workers.empty()) {
        cerr << "Error: No worker nodes specified in config" << endl;
        return false;
    }

    // Load paths
    g_config.cuclark_dir = parser.get_string("paths", "cuclark_dir");
    g_config.database = parser.get_string("paths", "database");
    g_config.results_dir = parser.get_string("paths", "results_dir", "results");

    if (g_config.cuclark_dir.empty()) {
        cerr << "Error: cuclark_dir not specified in config" << endl;
        return false;
    }

    if (g_config.database.empty()) {
        cerr << "Error: database path not specified in config" << endl;
        return false;
    }

    // Load per-node reads from [reads] section
    // Format: hostname = path (single-end) or hostname = path1, path2 (paired-end)
    map<string, string> reads_section = parser.get_section("reads");
    for (const auto& kv : reads_section) {
        vector<string> files = split_csv(kv.second);
        if (!files.empty()) {
            g_config.reads[kv.first] = files;
        }
    }

    // Load classification settings
    g_config.kmer_size = parser.get_int("classification", "kmer_size", 31);
    g_config.batch_size = parser.get_int("classification", "batch_size", 32);
    g_config.min_freq_target = parser.get_int("classification", "min_freq_target", -1);
    g_config.num_threads = parser.get_int("classification", "num_threads", -1);
    g_config.num_devices = parser.get_int("classification", "num_devices", -1);
    g_config.gap_iteration = parser.get_int("classification", "gap_iteration", -1);
    g_config.sampling_factor = parser.get_string("classification", "sampling_factor");
    g_config.tsk = parser.get_bool("classification", "tsk", false);
    g_config.extended = parser.get_bool("classification", "extended", false);
    g_config.gzipped = parser.get_bool("classification", "gzipped", false);
    g_config.verbose = parser.get_bool("classification", "verbose", false);

    // Load options
    g_config.master_processes_reads = parser.get_bool("options", "master_processes_reads", true);
    g_config.keep_local_results = parser.get_bool("options", "keep_local_results", true);

    // Load logging settings
    string log_level_str = parser.get_string("logging", "level", "info");
    if (log_level_str == "debug") g_config.log_level = LOG_DEBUG;
    else if (log_level_str == "warn") g_config.log_level = LOG_WARN;
    else if (log_level_str == "error") g_config.log_level = LOG_ERROR;
    else g_config.log_level = LOG_INFO;

    g_config.log_file = parser.get_string("logging", "file", "cluster_run.log");
    g_config.show_progress = parser.get_bool("logging", "show_progress", true);

    return true;
}

// =============================================================================
// CONFIG SERIALIZATION FOR MPI BROADCAST
// =============================================================================

static string serialize_config() {
    ostringstream oss;

    // Basic fields
    oss << g_config.cuclark_dir << "\n";
    oss << g_config.database << "\n";
    oss << g_config.results_dir << "\n";
    oss << g_config.kmer_size << "\n";
    oss << g_config.batch_size << "\n";
    oss << (g_config.master_processes_reads ? "1" : "0") << "\n";
    oss << (g_config.keep_local_results ? "1" : "0") << "\n";

    // New classification fields
    oss << g_config.min_freq_target << "\n";
    oss << g_config.num_threads << "\n";
    oss << g_config.num_devices << "\n";
    oss << g_config.gap_iteration << "\n";
    oss << g_config.sampling_factor << "\n";
    oss << (g_config.tsk ? "1" : "0") << "\n";
    oss << (g_config.extended ? "1" : "0") << "\n";
    oss << (g_config.gzipped ? "1" : "0") << "\n";
    oss << (g_config.verbose ? "1" : "0") << "\n";

    // Reads map: format is "hostname:file1,file2\n"
    oss << g_config.reads.size() << "\n";
    for (const auto& kv : g_config.reads) {
        oss << kv.first << ":";
        for (size_t i = 0; i < kv.second.size(); i++) {
            if (i > 0) oss << ",";
            oss << kv.second[i];
        }
        oss << "\n";
    }

    return oss.str();
}

static void deserialize_config(const string& data) {
    istringstream iss(data);
    string line;

    getline(iss, g_config.cuclark_dir);
    getline(iss, g_config.database);
    getline(iss, g_config.results_dir);
    getline(iss, line); g_config.kmer_size = stoi(line);
    getline(iss, line); g_config.batch_size = stoi(line);
    getline(iss, line); g_config.master_processes_reads = (line == "1");
    getline(iss, line); g_config.keep_local_results = (line == "1");

    // New classification fields
    getline(iss, line); g_config.min_freq_target = stoi(line);
    getline(iss, line); g_config.num_threads = stoi(line);
    getline(iss, line); g_config.num_devices = stoi(line);
    getline(iss, line); g_config.gap_iteration = stoi(line);
    getline(iss, g_config.sampling_factor);
    getline(iss, line); g_config.tsk = (line == "1");
    getline(iss, line); g_config.extended = (line == "1");
    getline(iss, line); g_config.gzipped = (line == "1");
    getline(iss, line); g_config.verbose = (line == "1");

    getline(iss, line);
    int num_reads = stoi(line);

    for (int i = 0; i < num_reads; i++) {
        getline(iss, line);
        size_t colon = line.find(':');
        if (colon != string::npos) {
            string hostname = line.substr(0, colon);
            string files_str = line.substr(colon + 1);

            vector<string> files;
            istringstream fss(files_str);
            string file;
            while (getline(fss, file, ',')) {
                if (!file.empty()) files.push_back(file);
            }
            g_config.reads[hostname] = files;
        }
    }
}

// =============================================================================
// MPI COMMUNICATION HELPERS
// =============================================================================

static void broadcast_config() {
    string config_str;
    int config_len = 0;

    if (g_rank == 0) {
        config_str = serialize_config();
        config_len = config_str.size();
    }

    // Broadcast length first
    MPI_Bcast(&config_len, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Allocate buffer and broadcast data
    if (g_rank != 0) {
        config_str.resize(config_len);
    }
    MPI_Bcast(&config_str[0], config_len, MPI_CHAR, 0, MPI_COMM_WORLD);

    // Workers deserialize
    if (g_rank != 0) {
        deserialize_config(config_str);
    }
}

static void send_result_to_master(const NodeResult& result) {
    string data = result.serialize();
    int len = data.size();

    MPI_Send(&len, 1, MPI_INT, 0, TAG_RESULT_DATA, MPI_COMM_WORLD);
    MPI_Send(data.c_str(), len, MPI_CHAR, 0, TAG_RESULT_DATA, MPI_COMM_WORLD);
}

static NodeResult receive_result_from_worker(int source_rank) {
    int len;
    MPI_Status status;

    MPI_Recv(&len, 1, MPI_INT, source_rank, TAG_RESULT_DATA, MPI_COMM_WORLD, &status);

    vector<char> buffer(len + 1, 0);
    MPI_Recv(buffer.data(), len, MPI_CHAR, source_rank, TAG_RESULT_DATA, MPI_COMM_WORLD, &status);

    return NodeResult::deserialize(string(buffer.data()));
}

// =============================================================================
// WORKER: RUN CLASSIFICATION LOCALLY
// =============================================================================

static NodeResult run_classification_local() {
    NodeResult result;
    result.hostname = get_hostname_str();

    auto start_time = chrono::steady_clock::now();

    log_worker("Starting classification");

    // Find reads for this node
    auto it = g_config.reads.find(result.hostname);
    if (it == g_config.reads.end() || it->second.empty()) {
        result.error_message = "No reads configured for this node";
        log_worker("ERROR: " + result.error_message);
        return result;
    }

    // Ensure results directory exists
    string mkdir_cmd = "mkdir -p " + shell_escape(g_config.cuclark_dir + "/" + g_config.results_dir);
    if (system(mkdir_cmd.c_str()) != 0) {
        log_worker("Warning: Could not create results directory");
    }

    const vector<string>& node_reads = it->second;
    bool is_paired = (node_reads.size() == 2);

    // Generate result filename from first read file
    const string& first_read = node_reads[0];
    size_t last_slash = first_read.find_last_of('/');
    string basename = (last_slash != string::npos) ? first_read.substr(last_slash + 1) : first_read;
    size_t dot_pos = basename.find_last_of('.');
    string result_name = (dot_pos != string::npos) ? basename.substr(0, dot_pos) : basename;

    string result_path = g_config.cuclark_dir + "/" + g_config.results_dir + "/" +
                        result.hostname + "_" + result_name;

    // Verify read files exist
    for (const string& read_file : node_reads) {
        log_worker("Processing: " + read_file);
        struct stat st;
        if (stat(read_file.c_str(), &st) != 0) {
            result.error_message = "Read file not found: " + read_file;
            log_worker("ERROR: " + result.error_message);
            return result;
        }
    }

    // Build arda classify command: ./bin/arda -c [OPTIONS]
    ostringstream cmd_ss;
    cmd_ss << "cd " << shell_escape(g_config.cuclark_dir) << " && ./bin/arda -c";

    // Input files
    if (is_paired) {
        cmd_ss << " -P " << shell_escape(node_reads[0])
               << " " << shell_escape(node_reads[1]);
    } else {
        cmd_ss << " -O " << shell_escape(node_reads[0]);
    }

    // Result path
    cmd_ss << " -R " << shell_escape(result_path);

    // Batch size
    cmd_ss << " -b " << g_config.batch_size;

    // Optional classification parameters
    if (g_config.kmer_size > 0)
        cmd_ss << " -k " << g_config.kmer_size;
    if (g_config.min_freq_target >= 0)
        cmd_ss << " -t " << g_config.min_freq_target;
    if (g_config.num_threads > 0)
        cmd_ss << " -n " << g_config.num_threads;
    if (g_config.num_devices > 0)
        cmd_ss << " -d " << g_config.num_devices;
    if (g_config.gap_iteration >= 0)
        cmd_ss << " -g " << g_config.gap_iteration;
    if (!g_config.sampling_factor.empty())
        cmd_ss << " -s " << shell_escape(g_config.sampling_factor);
    if (g_config.tsk)
        cmd_ss << " --tsk";
    if (g_config.extended)
        cmd_ss << " --extended";
    if (g_config.gzipped)
        cmd_ss << " --gzipped";
    if (g_config.verbose)
        cmd_ss << " --verbose";

    // Always use light mode on Jetson cluster
    cmd_ss << " --light";

    cmd_ss << " 2>&1";

    string cmd = cmd_ss.str();
    log_worker("Running: " + cmd);

    int rc = system(cmd.c_str());
    if (rc != 0) {
        result.error_message = "Classification failed with exit code " + to_string(WEXITSTATUS(rc));
        log_worker("ERROR: " + result.error_message);
        return result;
    }

    result.result_file = result_path + ".csv";
    log_worker("Classification complete: " + result.result_file);

    // Run abundance estimation: ./bin/arda -a <database> <result.csv>
    string abundance_cmd = "cd " + shell_escape(g_config.cuclark_dir) + " && " +
                           "./bin/arda -a " + shell_escape(g_config.database) +
                           " " + shell_escape(result.result_file) + " 2>&1";

    rc = system(abundance_cmd.c_str());
    if (rc == 0) {
        result.abundance_file = result_path + "_abundance.txt";
        log_worker("Abundance estimation complete");
    } else {
        log_worker("Warning: Abundance estimation failed");
    }

    auto end_time = chrono::steady_clock::now();
    result.elapsed_seconds = chrono::duration<double>(end_time - start_time).count();
    result.success = true;

    log_worker("Completed in " + to_string((int)result.elapsed_seconds) + " seconds");

    return result;
}

// =============================================================================
// MASTER: MERGE ABUNDANCE FILES
// =============================================================================

static void merge_abundance_files(const vector<NodeResult>& results) {
    // Collect abundance file paths from successful nodes
    vector<string> abundance_files;
    for (const auto& r : results) {
        if (r.success && !r.abundance_file.empty()) {
            abundance_files.push_back(r.abundance_file);
        }
    }

    if (abundance_files.size() < 2) {
        log_message(LOG_INFO, "Skipping abundance merge (need at least 2 files, have " +
                   to_string(abundance_files.size()) + ")");
        return;
    }

    // Build arda -m command
    ostringstream cmd_ss;
    cmd_ss << "cd " << shell_escape(g_config.cuclark_dir) << " && ./bin/arda -m";
    for (const string& f : abundance_files) {
        cmd_ss << " " << shell_escape(f);
    }

    string merged_path = g_config.results_dir + "/cluster_abundance_merged.txt";
    cmd_ss << " -o " << shell_escape(merged_path);
    cmd_ss << " 2>&1";

    string cmd = cmd_ss.str();
    log_message(LOG_INFO, "Merging abundance files: " + cmd);

    int rc = system(cmd.c_str());
    if (rc == 0) {
        log_message(LOG_INFO, "Merged abundance written to: " + merged_path);
    } else {
        log_message(LOG_WARN, "Abundance merge failed with exit code " +
                   to_string(WEXITSTATUS(rc)));
    }
}

// =============================================================================
// MASTER: AGGREGATE REPORT
// =============================================================================

static void generate_aggregate_report(const vector<NodeResult>& results,
                                       const string& merged_abundance_path) {
    log_message(LOG_INFO, "=== Generating Aggregate Report ===");

    string report_path = g_config.cuclark_dir + "/" + g_config.results_dir + "/cluster_report.txt";
    ofstream report(report_path);

    report << "========================================" << endl;
    report << "  CuCLARK Cluster Classification Report" << endl;
    report << "  Generated: " << get_timestamp() << endl;
    report << "========================================" << endl << endl;

    report << "CLUSTER CONFIGURATION" << endl;
    report << "  Master: " << g_config.master << endl;
    report << "  Workers: ";
    for (size_t i = 0; i < g_config.workers.size(); i++) {
        if (i > 0) report << ", ";
        report << g_config.workers[i];
    }
    report << endl;
    report << "  Database: " << g_config.database << endl;
    report << "  K-mer size: " << g_config.kmer_size << endl;
    report << "  Batch size: " << g_config.batch_size << endl;
    report << "  MPI processes: " << g_world_size << endl;
    report << endl;

    report << "NODE RESULTS" << endl;
    report << string(60, '-') << endl;

    int total_success = 0;
    double total_time = 0.0;
    double max_time = 0.0;

    for (const NodeResult& r : results) {
        report << "  " << r.hostname << ":" << endl;
        report << "    Status: " << (r.success ? "SUCCESS" : "FAILED") << endl;
        if (r.success) {
            report << "    Elapsed: " << fixed << setprecision(1) << r.elapsed_seconds << " seconds" << endl;
            report << "    Result: " << r.result_file << endl;
            if (!r.abundance_file.empty())
                report << "    Abundance: " << r.abundance_file << endl;
            total_success++;
            total_time += r.elapsed_seconds;
            max_time = max(max_time, r.elapsed_seconds);
        } else {
            report << "    Error: " << r.error_message << endl;
        }
        report << endl;
    }

    if (!merged_abundance_path.empty()) {
        report << "MERGED ABUNDANCE" << endl;
        report << string(60, '-') << endl;
        report << "  " << merged_abundance_path << endl;
        report << endl;
    }

    report << "SUMMARY" << endl;
    report << string(60, '-') << endl;
    report << "  Nodes processed: " << total_success << "/" << results.size() << endl;
    report << "  Total CPU time: " << fixed << setprecision(1) << total_time << " seconds" << endl;
    report << "  Wall clock time: " << fixed << setprecision(1) << max_time << " seconds (parallel)" << endl;
    report << "  Speedup: " << fixed << setprecision(2) << (max_time > 0 ? total_time / max_time : 0) << "x" << endl;
    report << endl;

    report.close();

    log_message(LOG_INFO, "Report written to: " + report_path);
}

// =============================================================================
// HOSTFILE GENERATION
// =============================================================================

static string generate_hostfile() {
    string hostfile_path = g_config.cuclark_dir + "/config/mpi_hostfile.txt";
    ofstream hf(hostfile_path);

    // Determine which nodes should participate
    vector<string> nodes;

    // Always include master so it can act as orchestrator (Rank 0)
    nodes.push_back(g_config.master);

    for (const string& worker : g_config.workers) {
        // Only include workers that have reads configured
        if (g_config.reads.find(worker) != g_config.reads.end()) {
            nodes.push_back(worker);
        }
    }

    for (const string& node : nodes) {
        hf << node << " slots=1" << endl;
    }

    hf.close();
    return hostfile_path;
}

// =============================================================================
// LAUNCHER: SELF-INVOKE VIA MPIRUN
// =============================================================================

static int launch_mpi(const string& config_file, bool verbose, int argc, char* argv[]) {
    cout << "=== CuCLARK MPI Cluster Coordinator ===" << endl;
    cout << "Loading configuration from: " << config_file << endl;

    // Load config to generate hostfile
    if (!load_config(config_file)) {
        cerr << "Failed to load configuration" << endl;
        return 1;
    }

    // Generate hostfile
    string hostfile = generate_hostfile();

    // Count nodes
    // Master is always included as rank 0 (orchestrator)
    // Workers with reads are added as additional ranks
    int num_nodes = 1;  // Always include master (rank 0)
    vector<string> active_workers;
    for (const string& worker : g_config.workers) {
        if (g_config.reads.find(worker) != g_config.reads.end()) {
            num_nodes++;
            active_workers.push_back(worker);
        }
    }

    // Check if there's any work to do
    bool master_has_work = g_config.master_processes_reads &&
                           g_config.reads.find(g_config.master) != g_config.reads.end();

    if (active_workers.empty() && !master_has_work) {
        cerr << "Error: No nodes have reads configured" << endl;
        return 1;
    }

    cout << "Nodes to use: " << num_nodes;
    cout << " (master=" << g_config.master;
    cout << ", workers=";
    for (size_t i = 0; i < active_workers.size(); i++) {
        if (i > 0) cout << ",";
        cout << active_workers[i];
    }
    cout << ")" << endl;
    cout << "Hostfile: " << hostfile << endl;

    // --- Pre-launch: verify SSH connectivity to each worker ---
    cout << "\nPre-launch connectivity check:" << endl;
    for (const string& worker : active_workers) {
        string ssh_test = "ssh -o BatchMode=yes -o ConnectTimeout=5 " +
                          worker + " hostname 2>&1";
        FILE* fp = popen(ssh_test.c_str(), "r");
        if (!fp) {
            cerr << "  " << worker << ": FAILED (popen error)" << endl;
            cerr << "Error: Cannot verify connectivity to " << worker << endl;
            return 1;
        }
        char buf[256];
        string ssh_output;
        while (fgets(buf, sizeof(buf), fp)) ssh_output += buf;
        int ssh_rc = pclose(fp);
        ssh_output = trim(ssh_output);

        if (ssh_rc != 0) {
            cerr << "  " << worker << ": FAILED" << endl;
            cerr << "    SSH output: " << ssh_output << endl;
            cerr << "Error: Cannot SSH to " << worker << ". MPI requires passwordless SSH." << endl;
            return 1;
        }
        cout << "  " << worker << ": OK (hostname=" << ssh_output << ")" << endl;
    }

    // --- Pre-launch: verify binary exists on each worker ---
    string exe_path = g_config.cuclark_dir + "/bin/arda-mpi";
    for (const string& worker : active_workers) {
        string check_cmd = "ssh -o BatchMode=yes -o ConnectTimeout=5 " +
                           worker + " test -x " + shell_escape(exe_path) + " 2>&1";
        int check_rc = system(check_cmd.c_str());
        if (WEXITSTATUS(check_rc) != 0) {
            cerr << "Error: Binary not found on " << worker << ": " << exe_path << endl;
            cerr << "Make sure the binary is compiled on all nodes or shared via NFS." << endl;
            return 1;
        }
        cout << "  " << worker << ": binary OK" << endl;
    }

    // Use absolute path for config file
    string abs_config_file = config_file;
    if (config_file[0] != '/') {
        abs_config_file = g_config.cuclark_dir + "/" + config_file;
    }

    // Build mpirun command
    // Detect MPI prefix (needed for orted on remote nodes)
    string mpi_prefix;
    FILE* prefix_fp = popen("dirname $(dirname $(which mpirun)) 2>/dev/null", "r");
    if (prefix_fp) {
        char prefix_buf[512];
        if (fgets(prefix_buf, sizeof(prefix_buf), prefix_fp)) {
            mpi_prefix = trim(string(prefix_buf));
        }
        pclose(prefix_fp);
    }

    ostringstream cmd;
    cmd << "mpirun";
    cmd << " --hostfile " << shell_escape(hostfile);
    cmd << " -np " << num_nodes;
    cmd << " --wdir " << shell_escape(g_config.cuclark_dir);
    cmd << " --map-by node";
    // Restrict MPI TCP communication to eth0 interface
    cmd << " --mca btl_tcp_if_include eth0";
    // Forward environment so remote nodes find the right MPI libs and tools
    cmd << " -x PATH -x LD_LIBRARY_PATH";
    // Specify prefix so mpirun can find orted on remote nodes
    if (!mpi_prefix.empty()) {
        cmd << " --prefix " << shell_escape(mpi_prefix);
    }
    cmd << " " << shell_escape(exe_path);
    cmd << " --mpi-worker";
    cmd << " -c " << shell_escape(abs_config_file);
    if (verbose) cmd << " -v";

    cout << "\nLaunching: " << cmd.str() << endl;
    cout << "========================================" << endl << endl;

    // Execute mpirun
    int rc = system(cmd.str().c_str());
    int exit_code = WEXITSTATUS(rc);

    if (exit_code != 0) {
        cerr << "\nmpirun exited with code " << exit_code << endl;
    }

    return exit_code;
}

// =============================================================================
// MAIN MPI ENTRY POINT (called by mpirun)
// =============================================================================

static int run_mpi_mode(const string& config_file, bool verbose) {
    // Initialize MPI (already called in main)
    MPI_Comm_rank(MPI_COMM_WORLD, &g_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &g_world_size);

    // Sanity check: world_size must be > 1 for cluster operation
    if (g_world_size <= 1) {
        fprintf(stderr,
            "\n"
            "ERROR: MPI World Size is %d (expected >= 2).\n"
            "  MPI launched but processes cannot see each other.\n"
            "  This usually means:\n"
            "    1. orted is not found on remote nodes (fix: ensure OpenMPI is installed\n"
            "       at the same path on all nodes, or use --prefix)\n"
            "    2. Firewall blocks MPI communication ports between nodes\n"
            "    3. Mismatched OpenMPI versions between nodes\n"
            "  Debug: run 'ssh jn01 which orted' and 'ssh jn01 mpirun --version'\n"
            "  to verify the MPI installation on worker nodes.\n"
            "\n",
            g_world_size);
    }

    // Master (rank 0) loads config and broadcasts
    if (g_rank == 0) {
        if (!load_config(config_file)) {
            cerr << "Master failed to load config" << endl;
            MPI_Abort(MPI_COMM_WORLD, 1);
            return 1;
        }

        if (verbose) g_config.log_level = LOG_DEBUG;

        // Setup logging
        string log_path = g_config.cuclark_dir + "/logs/" + g_config.log_file;
        string mkdir_cmd = "mkdir -p " + shell_escape(g_config.cuclark_dir + "/logs");
        if (system(mkdir_cmd.c_str()) != 0) {
            cerr << "Warning: Could not create logs directory" << endl;
        }
        g_logfile.open(log_path, ios::app);

        log_message(LOG_INFO, "========================================");
        log_message(LOG_INFO, "CuCLARK MPI Cluster Run Started");
        log_message(LOG_INFO, "MPI World Size: " + to_string(g_world_size));
        log_message(LOG_INFO, "========================================");
    }

    // Broadcast config to all workers
    broadcast_config();

    // Synchronize before starting work
    MPI_Barrier(MPI_COMM_WORLD);

    if (g_rank == 0) {
        log_message(LOG_INFO, "All nodes synchronized. Starting classification...");
    }

    // Everyone runs classification... unless you are the master and configured not to
    NodeResult my_result;
    bool did_process = false;

    if (g_rank == 0 && !g_config.master_processes_reads) {
        log_message(LOG_INFO, "Master acting as orchestrator only (skipping local classification).");
    }
    else {
        // Workers, or Master (if enabled), run the job
        my_result = run_classification_local();
        did_process = true;
    }

    // Gather results
    vector<NodeResult> all_results;

    if (g_rank == 0) {
        // Master adds its own result
        if (did_process) {
            all_results.push_back(my_result);
            log_message(LOG_INFO, string("Master completed: ") +
                   (my_result.success ? "SUCCESS" : "FAILED"));
        }

        // Receive from all workers
        for (int src = 1; src < g_world_size; src++) {
            NodeResult worker_result = receive_result_from_worker(src);
            all_results.push_back(worker_result);

            log_message(LOG_INFO, worker_result.hostname + ": " +
                       (worker_result.success ? "SUCCESS" : "FAILED") +
                       " (" + to_string((int)worker_result.elapsed_seconds) + "s)");
        }

        // Merge abundance files from all nodes
        merge_abundance_files(all_results);

        string merged_path = g_config.results_dir + "/cluster_abundance_merged.txt";

        // Generate report
        generate_aggregate_report(all_results, merged_path);

        // Summary
        int success_count = 0;
        for (const auto& r : all_results) {
            if (r.success) success_count++;
        }

        log_message(LOG_INFO, "========================================");
        log_message(LOG_INFO, "Cluster Processing Complete");
        log_message(LOG_INFO, "Success: " + to_string(success_count) + "/" +
                   to_string(all_results.size()) + " nodes");
        log_message(LOG_INFO, "========================================");

        if (g_logfile.is_open()) {
            g_logfile.close();
        }
    } else {
        // Workers send their result to master
        send_result_to_master(my_result);
    }

    return 0;
}

// =============================================================================
// PREFLIGHT CHECK
// =============================================================================

static int run_preflight(const string& config_file) {
    cout << "=== Pre-flight Checks ===" << endl;

    if (!load_config(config_file)) {
        cerr << "Failed to load configuration" << endl;
        return 1;
    }

    cout << "Configuration loaded successfully." << endl;
    cout << "Master: " << g_config.master << endl;
    cout << "Workers: ";
    for (const auto& w : g_config.workers) cout << w << " ";
    cout << endl;
    cout << "Database: " << g_config.database << endl;
    cout << "Batch size: " << g_config.batch_size << endl;
    cout << "K-mer size: " << g_config.kmer_size << endl;
    cout << endl;

    // Check reads configuration
    cout << "Reads configuration:" << endl;
    for (const auto& kv : g_config.reads) {
        string mode = (kv.second.size() == 2) ? " (paired-end)" : " (single-end)";
        cout << "  " << kv.first << ": " << kv.second.size() << " file(s)" << mode << endl;
        for (const auto& f : kv.second) {
            cout << "    - " << f << endl;
        }
    }
    cout << endl;

    // Show classification options
    cout << "Classification options:" << endl;
    if (g_config.min_freq_target >= 0)
        cout << "  min_freq_target: " << g_config.min_freq_target << endl;
    if (g_config.num_threads > 0)
        cout << "  num_threads: " << g_config.num_threads << endl;
    if (g_config.num_devices > 0)
        cout << "  num_devices: " << g_config.num_devices << endl;
    if (g_config.gap_iteration >= 0)
        cout << "  gap_iteration: " << g_config.gap_iteration << endl;
    if (!g_config.sampling_factor.empty())
        cout << "  sampling_factor: " << g_config.sampling_factor << endl;
    if (g_config.tsk) cout << "  tsk: true" << endl;
    if (g_config.extended) cout << "  extended: true" << endl;
    if (g_config.gzipped) cout << "  gzipped: true" << endl;
    if (g_config.verbose) cout << "  verbose: true" << endl;
    cout << endl;

    // Generate and show hostfile
    string hostfile = generate_hostfile();
    cout << "Generated hostfile: " << hostfile << endl;

    // Count nodes: master (rank 0) + workers with reads
    int num_nodes = 1;
    for (const string& worker : g_config.workers) {
        if (g_config.reads.find(worker) != g_config.reads.end()) {
            num_nodes++;
        }
    }

    // Try to ping nodes via mpirun
    cout << endl << "Testing MPI connectivity..." << endl;

    string test_cmd = "mpirun --hostfile " + shell_escape(hostfile) +
                      " -np " + to_string(num_nodes) +
                      " --wdir " + shell_escape(g_config.cuclark_dir) +
                      " --mca btl_tcp_if_include eth0" +
                      " hostname 2>&1";

    cout << "Running: " << test_cmd << endl;
    int rc = system(test_cmd.c_str());

    if (rc == 0) {
        cout << "\nMPI connectivity test passed!" << endl;
    } else {
        cout << "\nMPI connectivity test failed!" << endl;
        cout << "Make sure:" << endl;
        cout << "  1. Passwordless SSH is set up between all nodes" << endl;
        cout << "  2. MPI is installed on all nodes" << endl;
        cout << "  3. The arda-mpi binary exists at the same path on all nodes" << endl;
    }

    return rc == 0 ? 0 : 1;
}

// =============================================================================
// USAGE
// =============================================================================

static void print_usage(const char* prog) {
    cout << "CuCLARK MPI Cluster Coordinator" << endl;
    cout << endl;
    cout << "Usage: " << prog << " -c <config_file> [options]" << endl;
    cout << endl;
    cout << "This program automatically launches mpirun internally - no need to" << endl;
    cout << "call mpirun manually. Requires passwordless SSH between nodes." << endl;
    cout << endl;
    cout << "Required:" << endl;
    cout << "  -c, --config <file>   Path to cluster configuration file (INI format)" << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  -p, --preflight       Run pre-flight checks only (test MPI connectivity)" << endl;
    cout << "  -v, --verbose         Enable verbose output" << endl;
    cout << "  -h, --help            Show this help message" << endl;
    cout << endl;
    cout << "Internal (used by mpirun):" << endl;
    cout << "  --mpi-worker          Run in MPI worker mode (do not use manually)" << endl;
    cout << endl;
    cout << "Config file format (INI):" << endl;
    cout << "  [cluster]" << endl;
    cout << "  master = jn00" << endl;
    cout << "  workers = jn01, jn03, jn04" << endl;
    cout << "  " << endl;
    cout << "  [paths]" << endl;
    cout << "  cuclark_dir = /home/pathogen/jn_cuclark" << endl;
    cout << "  database = /home/pathogen/cuclark_db" << endl;
    cout << "  results_dir = results" << endl;
    cout << "  " << endl;
    cout << "  [reads]" << endl;
    cout << "  # hostname = path (single-end) or path1, path2 (paired-end)" << endl;
    cout << "  jn00 = /home/pathogen/reads/sample_00.fastq" << endl;
    cout << "  jn01 = /home/pathogen/reads/s1_R1.fastq, /home/pathogen/reads/s1_R2.fastq" << endl;
    cout << "  " << endl;
    cout << "  [classification]" << endl;
    cout << "  batch_size = 32" << endl;
    cout << "  kmer_size = 31" << endl;
    cout << "  # See arda -c -h for all classification options" << endl;
    cout << endl;
    cout << "Examples:" << endl;
    cout << "  " << prog << " -c config/cluster.conf           # Run cluster classification" << endl;
    cout << "  " << prog << " -c config/cluster.conf -p        # Test cluster setup" << endl;
    cout << "  " << prog << " -c config/cluster.conf -v        # Verbose output" << endl;
    cout << endl;
    cout << "Prerequisites:" << endl;
    cout << "  - Passwordless SSH from master to all worker nodes" << endl;
    cout << "  - OpenMPI installed on all nodes" << endl;
    cout << "  - Same arda-mpi binary path on all nodes" << endl;
    cout << "  - Same database path on all nodes" << endl;
}

// =============================================================================
// MAIN
// =============================================================================

int main(int argc, char* argv[]) {
    // Parse command line arguments
    string config_file;
    bool preflight_only = false;
    bool verbose = false;
    bool mpi_worker_mode = false;

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "-p" || arg == "--preflight") {
            preflight_only = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--mpi-worker") {
            mpi_worker_mode = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (config_file.empty()) {
        cerr << "Error: Config file required" << endl;
        print_usage(argv[0]);
        return 1;
    }

    // Determine mode
    if (mpi_worker_mode) {
        // We were launched by mpirun - run in MPI mode
        int init_rc = MPI_Init(&argc, &argv);

        // Immediate diagnostics from ALL processes (before any rank filtering)
        int diag_rank, diag_size;
        char diag_name[MPI_MAX_PROCESSOR_NAME];
        int diag_name_len;
        MPI_Comm_rank(MPI_COMM_WORLD, &diag_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &diag_size);
        MPI_Get_processor_name(diag_name, &diag_name_len);
        fprintf(stderr, "[MPI DIAG] MPI_Init rc=%d | Rank %d of %d on '%s'\n",
                init_rc, diag_rank, diag_size, diag_name);
        fflush(stderr);

        int result = run_mpi_mode(config_file, verbose);
        MPI_Finalize();
        return result;
    } else if (preflight_only) {
        // Run pre-flight checks
        return run_preflight(config_file);
    } else {
        // Launch mode - we'll call mpirun with ourselves
        return launch_mpi(config_file, verbose, argc, argv);
    }
}
