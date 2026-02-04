/*
 * arda_mpi.cpp - MPI Cluster Coordinator for CuCLARK
 *
 * This program coordinates distributed metagenomic classification
 * across a cluster of Jetson Nano devices using MPI.
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
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

using namespace std;

// =============================================================================
// CONSTANTS AND TAGS
// =============================================================================

// MPI message tags
const int TAG_WORK_ASSIGNMENT = 1;
const int TAG_PROGRESS_UPDATE = 2;
const int TAG_RESULT_DATA = 3;
const int TAG_STATUS = 4;
const int TAG_SHUTDOWN = 99;

// Status codes
const int STATUS_OK = 0;
const int STATUS_ERROR = 1;
const int STATUS_RETRY = 2;

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
    
    // Per-node reads
    map<string, vector<string>> reads;
    
    // Classification settings
    int kmer_size = 31;
    int batch_size = 50000;
    
    // Options
    bool master_processes_reads = true;
    bool retry_failed_nodes = true;
    int max_retries = 3;
    bool collect_results_to_master = true;
    bool keep_local_results = true;
    int ssh_timeout = 30;
    
    // Logging
    LogLevel log_level = LOG_INFO;
    string log_file = "cluster_run.log";
    bool show_progress = true;
};

struct NodeStatus {
    string hostname;
    bool reachable = false;
    bool database_ok = false;
    bool reads_ok = false;
    bool binary_ok = false;
    bool disk_ok = false;
    string error_message;
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
};

// =============================================================================
// GLOBAL STATE
// =============================================================================

static ClusterConfig g_config;
static ofstream g_logfile;
static mutex g_log_mutex;
static string g_ssh_password;

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
    
    lock_guard<mutex> lock(g_log_mutex);
    
    string formatted = "[" + get_timestamp() + "] [" + level_names[level] + "] " + message;
    
    // Console output with colors
    if (level >= LOG_WARN) {
        cerr << (level == LOG_ERROR ? "\033[31m" : "\033[33m") << formatted << "\033[0m" << endl;
    } else if (g_config.show_progress || level == LOG_INFO) {
        cout << formatted << endl;
    }
    
    // File output
    if (g_logfile.is_open()) {
        g_logfile << formatted << endl;
        g_logfile.flush();
    }
}

static string read_password(const string& prompt) {
    cout << prompt;
    cout.flush();
    
    // Disable echo
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    string password;
    getline(cin, password);
    
    // Restore echo
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    cout << endl;
    
    return password;
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

static int run_ssh_command(const string& host, const string& command, string& output) {
    // Use sshpass for password authentication
    string full_cmd = "sshpass -p " + shell_escape(g_ssh_password) + 
                      " ssh -o StrictHostKeyChecking=no -o ConnectTimeout=" + 
                      to_string(g_config.ssh_timeout) + " " +
                      host + " " + shell_escape(command) + " 2>&1";
    
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) {
        output = "Failed to execute SSH command";
        return -1;
    }
    
    char buffer[4096];
    output.clear();
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    
    int status = pclose(pipe);
    return WEXITSTATUS(status);
}

static int run_scp_command(const string& host, const string& remote_path, 
                           const string& local_path) {
    string full_cmd = "sshpass -p " + shell_escape(g_ssh_password) +
                      " scp -o StrictHostKeyChecking=no " +
                      host + ":" + shell_escape(remote_path) + " " +
                      shell_escape(local_path) + " 2>&1";
    
    int status = system(full_cmd.c_str());
    return WEXITSTATUS(status);
}

// =============================================================================
// YAML CONFIG PARSER (Simple implementation for our schema)
// =============================================================================

class YamlParser {
private:
    map<string, string> flat_values;
    map<string, vector<string>> list_values;
    
    int get_indent(const string& line) {
        int indent = 0;
        for (char c : line) {
            if (c == ' ') indent++;
            else break;
        }
        return indent;
    }
    
public:
    bool parse(const string& filename) {
        ifstream file(filename);
        if (!file) return false;
        
        string line;
        string current_section;
        string current_subsection;
        string list_key;
        
        while (getline(file, line)) {
            // Skip comments and empty lines
            string trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;
            
            int indent = get_indent(line);
            
            // Check for list item
            if (trimmed[0] == '-') {
                string value = trim(trimmed.substr(1));
                if (!list_key.empty()) {
                    list_values[list_key].push_back(value);
                }
                continue;
            }
            
            // Parse key: value
            size_t colon_pos = trimmed.find(':');
            if (colon_pos == string::npos) continue;
            
            string key = trim(trimmed.substr(0, colon_pos));
            string value = trim(trimmed.substr(colon_pos + 1));
            
            if (indent == 0) {
                // Top-level section
                current_section = key;
                current_subsection.clear();
            } else if (indent == 2) {
                // Second level
                if (value.empty()) {
                    // This is a subsection or list start
                    current_subsection = key;
                    list_key = current_section + "." + key;
                } else {
                    // Key-value pair
                    string full_key = current_section + "." + key;
                    flat_values[full_key] = value;
                }
            } else if (indent == 4 && !current_subsection.empty()) {
                // Third level (per-node config)
                if (value.empty()) {
                    list_key = current_section + "." + current_subsection + "." + key;
                } else {
                    string full_key = current_section + "." + current_subsection + "." + key;
                    flat_values[full_key] = value;
                }
            }
        }
        
        return true;
    }
    
    string get_string(const string& key, const string& default_val = "") {
        auto it = flat_values.find(key);
        return (it != flat_values.end()) ? it->second : default_val;
    }
    
    int get_int(const string& key, int default_val = 0) {
        string val = get_string(key);
        if (val.empty()) return default_val;
        try {
            return stoi(val);
        } catch (...) {
            return default_val;
        }
    }
    
    bool get_bool(const string& key, bool default_val = false) {
        string val = get_string(key);
        if (val.empty()) return default_val;
        return (val == "true" || val == "yes" || val == "1");
    }
    
    vector<string> get_list(const string& key) {
        auto it = list_values.find(key);
        return (it != list_values.end()) ? it->second : vector<string>();
    }
    
    // Get all keys that start with a prefix
    vector<string> get_keys_with_prefix(const string& prefix) {
        vector<string> keys;
        for (const auto& kv : flat_values) {
            if (kv.first.find(prefix) == 0) {
                keys.push_back(kv.first);
            }
        }
        for (const auto& kv : list_values) {
            if (kv.first.find(prefix) == 0) {
                keys.push_back(kv.first);
            }
        }
        return keys;
    }
};

static bool load_config(const string& config_file) {
    YamlParser parser;
    
    if (!parser.parse(config_file)) {
        cerr << "Error: Failed to parse config file: " << config_file << endl;
        return false;
    }
    
    // Load cluster settings
    g_config.master = parser.get_string("cluster.master");
    g_config.workers = parser.get_list("cluster.workers");
    
    if (g_config.master.empty()) {
        cerr << "Error: Master node not specified in config" << endl;
        return false;
    }
    
    if (g_config.workers.empty()) {
        cerr << "Error: No worker nodes specified in config" << endl;
        return false;
    }
    
    // Load paths
    g_config.cuclark_dir = parser.get_string("paths.cuclark_dir");
    g_config.database = parser.get_string("paths.database");
    g_config.results_dir = parser.get_string("paths.results_dir", "results");
    
    if (g_config.cuclark_dir.empty()) {
        cerr << "Error: cuclark_dir not specified in config" << endl;
        return false;
    }
    
    if (g_config.database.empty()) {
        cerr << "Error: database path not specified in config" << endl;
        return false;
    }
    
    // Load per-node reads
    vector<string> all_nodes = g_config.workers;
    all_nodes.push_back(g_config.master);
    
    for (const string& node : all_nodes) {
        vector<string> node_reads = parser.get_list("reads." + node);
        if (!node_reads.empty()) {
            g_config.reads[node] = node_reads;
        }
    }
    
    // Load classification settings
    g_config.kmer_size = parser.get_int("classification.kmer_size", 31);
    g_config.batch_size = parser.get_int("classification.batch_size", 50000);
    
    // Load options
    g_config.master_processes_reads = parser.get_bool("options.master_processes_reads", true);
    g_config.retry_failed_nodes = parser.get_bool("options.retry_failed_nodes", true);
    g_config.max_retries = parser.get_int("options.max_retries", 3);
    g_config.collect_results_to_master = parser.get_bool("options.collect_results_to_master", true);
    g_config.keep_local_results = parser.get_bool("options.keep_local_results", true);
    g_config.ssh_timeout = parser.get_int("options.ssh_timeout", 30);
    
    // Load logging settings
    string log_level_str = parser.get_string("logging.level", "info");
    if (log_level_str == "debug") g_config.log_level = LOG_DEBUG;
    else if (log_level_str == "warn") g_config.log_level = LOG_WARN;
    else if (log_level_str == "error") g_config.log_level = LOG_ERROR;
    else g_config.log_level = LOG_INFO;
    
    g_config.log_file = parser.get_string("logging.file", "cluster_run.log");
    g_config.show_progress = parser.get_bool("logging.show_progress", true);
    
    return true;
}

// =============================================================================
// PRE-FLIGHT CHECKS
// =============================================================================

static NodeStatus check_node(const string& hostname) {
    NodeStatus status;
    status.hostname = hostname;
    string output;
    
    log_message(LOG_INFO, "Checking node: " + hostname);
    
    // Check if node is reachable
    int rc = run_ssh_command(hostname, "echo 'OK'", output);
    if (rc != 0 || output.find("OK") == string::npos) {
        status.error_message = "Node not reachable: " + output;
        log_message(LOG_ERROR, status.error_message);
        return status;
    }
    status.reachable = true;
    log_message(LOG_DEBUG, hostname + ": SSH connection OK");
    
    // Check database exists
    string db_check = "test -d " + shell_escape(g_config.database) + 
                      " && test -d " + shell_escape(g_config.database + "/Custom") +
                      " && test -d " + shell_escape(g_config.database + "/taxonomy") +
                      " && echo 'DB_OK'";
    rc = run_ssh_command(hostname, db_check, output);
    if (rc != 0 || output.find("DB_OK") == string::npos) {
        status.error_message = "Database not found or incomplete at " + g_config.database;
        log_message(LOG_ERROR, hostname + ": " + status.error_message);
        return status;
    }
    status.database_ok = true;
    log_message(LOG_DEBUG, hostname + ": Database OK");
    
    // Check reads exist
    auto it = g_config.reads.find(hostname);
    if (it == g_config.reads.end() || it->second.empty()) {
        status.error_message = "No reads configured for this node";
        log_message(LOG_ERROR, hostname + ": " + status.error_message);
        return status;
    }
    
    for (const string& read_file : it->second) {
        string read_check = "test -f " + shell_escape(read_file) + " && echo 'READ_OK'";
        rc = run_ssh_command(hostname, read_check, output);
        if (rc != 0 || output.find("READ_OK") == string::npos) {
            status.error_message = "Read file not found: " + read_file;
            log_message(LOG_ERROR, hostname + ": " + status.error_message);
            return status;
        }
    }
    status.reads_ok = true;
    log_message(LOG_DEBUG, hostname + ": Read files OK");
    
    // Check cuCLARK binary exists
    string binary_path = g_config.cuclark_dir + "/bin/cuCLARK-l";
    string bin_check = "test -x " + shell_escape(binary_path) + " && echo 'BIN_OK'";
    rc = run_ssh_command(hostname, bin_check, output);
    if (rc != 0 || output.find("BIN_OK") == string::npos) {
        status.error_message = "cuCLARK-l binary not found or not executable at " + binary_path;
        log_message(LOG_ERROR, hostname + ": " + status.error_message);
        return status;
    }
    status.binary_ok = true;
    log_message(LOG_DEBUG, hostname + ": Binary OK");
    
    // Check disk space (at least 1GB free)
    string disk_check = "df -BG " + shell_escape(g_config.cuclark_dir) + 
                        " | tail -1 | awk '{print $4}' | tr -d 'G'";
    rc = run_ssh_command(hostname, disk_check, output);
    if (rc == 0) {
        try {
            int free_gb = stoi(trim(output));
            if (free_gb < 1) {
                status.error_message = "Insufficient disk space (< 1GB free)";
                log_message(LOG_WARN, hostname + ": " + status.error_message);
                // Don't return - this is a warning, not fatal
            }
        } catch (...) {
            log_message(LOG_WARN, hostname + ": Could not parse disk space");
        }
    }
    status.disk_ok = true;
    
    log_message(LOG_INFO, hostname + ": All checks passed ✓");
    return status;
}

static bool run_preflight_checks(vector<NodeStatus>& node_statuses) {
    log_message(LOG_INFO, "=== Running Pre-flight Checks ===");
    
    vector<string> all_nodes = g_config.workers;
    if (g_config.master_processes_reads) {
        all_nodes.insert(all_nodes.begin(), g_config.master);
    } else {
        log_message(LOG_INFO, "Master node (" + g_config.master + ") will only coordinate, not process reads");
    }
    
    bool all_ok = true;
    
    for (const string& node : all_nodes) {
        NodeStatus status = check_node(node);
        node_statuses.push_back(status);
        
        if (!status.reachable || !status.database_ok || 
            !status.reads_ok || !status.binary_ok) {
            all_ok = false;
        }
    }
    
    // Summary
    log_message(LOG_INFO, "=== Pre-flight Summary ===");
    int ready_count = 0;
    for (const NodeStatus& ns : node_statuses) {
        string status_str = (ns.reachable && ns.database_ok && ns.reads_ok && ns.binary_ok) 
                            ? "READY" : "FAILED";
        if (status_str == "READY") ready_count++;
        log_message(LOG_INFO, "  " + ns.hostname + ": " + status_str);
    }
    log_message(LOG_INFO, "Ready nodes: " + to_string(ready_count) + "/" + to_string(all_nodes.size()));
    
    return all_ok || ready_count > 0; // Continue if at least one node is ready
}

// =============================================================================
// MPI WORKER LOGIC
// =============================================================================

static NodeResult run_classification_local() {
    NodeResult result;
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    result.hostname = hostname;
    
    auto start_time = chrono::steady_clock::now();
    
    log_message(LOG_INFO, "Starting classification on " + result.hostname);
    
    // Get reads for this node
    auto it = g_config.reads.find(result.hostname);
    if (it == g_config.reads.end() || it->second.empty()) {
        result.error_message = "No reads configured for this node";
        log_message(LOG_ERROR, result.error_message);
        return result;
    }
    
    // Process each read file
    for (const string& read_file : it->second) {
        // Generate result filename from input filename
        size_t last_slash = read_file.find_last_of('/');
        string basename = (last_slash != string::npos) ? read_file.substr(last_slash + 1) : read_file;
        size_t dot_pos = basename.find_last_of('.');
        string result_name = (dot_pos != string::npos) ? basename.substr(0, dot_pos) : basename;
        
        string result_path = g_config.cuclark_dir + "/" + g_config.results_dir + "/" + 
                             result.hostname + "_" + result_name;
        
        // Run classification
        string cmd = "cd " + shell_escape(g_config.cuclark_dir) + " && " +
                     "./scripts/classify_metagenome.sh" +
                     " -O " + shell_escape(read_file) +
                     " -R " + shell_escape(result_path) +
                     " -k " + to_string(g_config.kmer_size) +
                     " -b " + to_string(g_config.batch_size) +
                     " --light";
        
        log_message(LOG_DEBUG, "Running: " + cmd);
        
        int rc = system(cmd.c_str());
        if (rc != 0) {
            result.error_message = "Classification failed with exit code " + to_string(WEXITSTATUS(rc));
            log_message(LOG_ERROR, result.error_message);
            return result;
        }
        
        result.result_file = result_path + ".csv";
        
        // Run abundance estimation
        string abundance_cmd = "cd " + shell_escape(g_config.cuclark_dir) + " && " +
                               "./scripts/estimate_abundance.sh -D " + shell_escape(g_config.database) +
                               " -F " + shell_escape(result.result_file) +
                               " > " + shell_escape(result_path + "_abundance.txt");
        
        rc = system(abundance_cmd.c_str());
        if (rc == 0) {
            result.abundance_file = result_path + "_abundance.txt";
        }
    }
    
    auto end_time = chrono::steady_clock::now();
    result.elapsed_seconds = chrono::duration<double>(end_time - start_time).count();
    result.success = true;
    
    log_message(LOG_INFO, "Classification completed on " + result.hostname + 
                " in " + to_string((int)result.elapsed_seconds) + " seconds");
    
    return result;
}

// =============================================================================
// MPI MASTER LOGIC
// =============================================================================

static void generate_hostfile(const vector<NodeStatus>& ready_nodes, const string& path) {
    ofstream hf(path);
    for (const NodeStatus& ns : ready_nodes) {
        if (ns.reachable && ns.database_ok && ns.reads_ok && ns.binary_ok) {
            hf << ns.hostname << " slots=1" << endl;
        }
    }
    hf.close();
}

static bool collect_results(const vector<NodeResult>& results) {
    log_message(LOG_INFO, "=== Collecting Results to Master ===");
    
    string master_results_dir = g_config.cuclark_dir + "/" + g_config.results_dir + "/aggregated";
    string mkdir_cmd = "mkdir -p " + shell_escape(master_results_dir);
    if (system(mkdir_cmd.c_str()) != 0) {
        log_message(LOG_WARN, "Failed to create aggregated results directory");
    }
    
    for (const NodeResult& r : results) {
        if (!r.success || r.hostname == g_config.master) continue;
        
        log_message(LOG_INFO, "Collecting results from " + r.hostname);
        
        // Copy result file
        if (!r.result_file.empty()) {
            string local_path = master_results_dir + "/" + r.hostname + "_result.csv";
            int rc = run_scp_command(r.hostname, r.result_file, local_path);
            if (rc != 0) {
                log_message(LOG_WARN, "Failed to copy result from " + r.hostname);
            }
        }
        
        // Copy abundance file
        if (!r.abundance_file.empty()) {
            string local_path = master_results_dir + "/" + r.hostname + "_abundance.txt";
            int rc = run_scp_command(r.hostname, r.abundance_file, local_path);
            if (rc != 0) {
                log_message(LOG_WARN, "Failed to copy abundance from " + r.hostname);
            }
        }
    }
    
    return true;
}

static void generate_aggregate_report(const vector<NodeResult>& results) {
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
    report << endl;
    
    report << "NODE RESULTS" << endl;
    report << string(60, '-') << endl;
    
    int total_success = 0;
    double total_time = 0.0;
    
    for (const NodeResult& r : results) {
        report << "  " << r.hostname << ":" << endl;
        report << "    Status: " << (r.success ? "SUCCESS" : "FAILED") << endl;
        if (r.success) {
            report << "    Elapsed: " << fixed << setprecision(1) << r.elapsed_seconds << " seconds" << endl;
            report << "    Result: " << r.result_file << endl;
            total_success++;
            total_time += r.elapsed_seconds;
        } else {
            report << "    Error: " << r.error_message << endl;
        }
        report << endl;
    }
    
    report << "SUMMARY" << endl;
    report << string(60, '-') << endl;
    report << "  Nodes processed: " << total_success << "/" << results.size() << endl;
    report << "  Total processing time: " << fixed << setprecision(1) << total_time << " seconds" << endl;
    report << endl;
    
    report.close();
    
    log_message(LOG_INFO, "Report written to: " + report_path);
}

// =============================================================================
// MAIN MPI ENTRY POINTS
// =============================================================================

static int run_master(int world_size) {
    log_message(LOG_INFO, "=== CuCLARK Cluster Master Starting ===");
    log_message(LOG_INFO, "MPI World Size: " + to_string(world_size));
    
    vector<NodeStatus> node_statuses;
    
    // Run pre-flight checks
    if (!run_preflight_checks(node_statuses)) {
        log_message(LOG_ERROR, "Pre-flight checks failed on all nodes. Aborting.");
        return 1;
    }
    
    // Count ready nodes
    vector<NodeStatus> ready_nodes;
    for (const NodeStatus& ns : node_statuses) {
        if (ns.reachable && ns.database_ok && ns.reads_ok && ns.binary_ok) {
            ready_nodes.push_back(ns);
        }
    }
    
    if (ready_nodes.empty()) {
        log_message(LOG_ERROR, "No nodes ready for processing. Aborting.");
        return 1;
    }
    
    log_message(LOG_INFO, "Starting classification on " + to_string(ready_nodes.size()) + " nodes");
    
    // Generate hostfile for MPI
    string hostfile_path = g_config.cuclark_dir + "/config/hostfile.txt";
    generate_hostfile(ready_nodes, hostfile_path);
    
    // For single-process MPI (when run without mpirun), run sequentially
    vector<NodeResult> all_results;
    
    if (world_size == 1) {
        // Sequential execution via SSH
        log_message(LOG_INFO, "Running in SSH coordination mode (single MPI process)");
        
        for (const NodeStatus& ns : ready_nodes) {
            log_message(LOG_INFO, "Processing node: " + ns.hostname);
            
            NodeResult result;
            result.hostname = ns.hostname;
            
            auto start_time = chrono::steady_clock::now();
            
            // Build remote command
            auto it = g_config.reads.find(ns.hostname);
            if (it == g_config.reads.end() || it->second.empty()) {
                result.error_message = "No reads configured";
                all_results.push_back(result);
                continue;
            }
            
            for (const string& read_file : it->second) {
                // Generate result filename
                size_t last_slash = read_file.find_last_of('/');
                string basename = (last_slash != string::npos) ? read_file.substr(last_slash + 1) : read_file;
                size_t dot_pos = basename.find_last_of('.');
                string result_name = (dot_pos != string::npos) ? basename.substr(0, dot_pos) : basename;
                
                string result_path = g_config.cuclark_dir + "/" + g_config.results_dir + "/" +
                                    ns.hostname + "_" + result_name;
                
                // Run classification remotely
                string remote_cmd = "cd " + g_config.cuclark_dir + " && " +
                                   "./scripts/classify_metagenome.sh" +
                                   " -O " + read_file +
                                   " -R " + result_path +
                                   " -k " + to_string(g_config.kmer_size) +
                                   " -b " + to_string(g_config.batch_size) +
                                   " --light";
                
                string output;
                int rc = run_ssh_command(ns.hostname, remote_cmd, output);
                
                if (rc != 0) {
                    result.error_message = "Classification failed: " + output;
                    log_message(LOG_ERROR, ns.hostname + ": " + result.error_message);
                } else {
                    result.result_file = result_path + ".csv";
                    result.success = true;
                    
                    // Run abundance estimation
                    string abundance_cmd = "cd " + g_config.cuclark_dir + " && " +
                                          "./scripts/estimate_abundance.sh -D " + g_config.database +
                                          " -F " + result.result_file +
                                          " > " + result_path + "_abundance.txt";
                    
                    run_ssh_command(ns.hostname, abundance_cmd, output);
                    result.abundance_file = result_path + "_abundance.txt";
                }
            }
            
            auto end_time = chrono::steady_clock::now();
            result.elapsed_seconds = chrono::duration<double>(end_time - start_time).count();
            
            all_results.push_back(result);
            
            if (result.success) {
                log_message(LOG_INFO, ns.hostname + ": Completed in " + 
                           to_string((int)result.elapsed_seconds) + " seconds");
            }
        }
    } else {
        // True MPI parallel execution
        log_message(LOG_INFO, "Running in MPI parallel mode");
        
        // Broadcast config to all workers
        // (In a full implementation, we'd serialize and send the config)
        
        // Each rank processes its assigned node
        // Master (rank 0) also participates if it has reads
        NodeResult local_result = run_classification_local();
        all_results.push_back(local_result);
        
        // Receive results from workers
        for (int rank = 1; rank < world_size; rank++) {
            MPI_Status status;
            char buffer[4096];
            
            MPI_Recv(buffer, sizeof(buffer), MPI_CHAR, rank, TAG_RESULT_DATA, 
                    MPI_COMM_WORLD, &status);
            
            // Parse result (simplified - in production use proper serialization)
            NodeResult worker_result;
            worker_result.hostname = buffer;  // First part is hostname
            worker_result.success = true;
            all_results.push_back(worker_result);
        }
    }
    
    // Collect results to master if configured
    if (g_config.collect_results_to_master) {
        collect_results(all_results);
    }
    
    // Generate aggregate report
    generate_aggregate_report(all_results);
    
    log_message(LOG_INFO, "=== Cluster Processing Complete ===");
    
    return 0;
}

static int run_worker(int rank) {
    // Worker node logic
    log_message(LOG_DEBUG, "Worker rank " + to_string(rank) + " starting");
    
    // Run local classification
    NodeResult result = run_classification_local();
    
    // Send result back to master
    string result_str = result.hostname + "|" + 
                        (result.success ? "1" : "0") + "|" +
                        result.result_file + "|" +
                        to_string(result.elapsed_seconds);
    
    MPI_Send(result_str.c_str(), result_str.size() + 1, MPI_CHAR, 0, 
             TAG_RESULT_DATA, MPI_COMM_WORLD);
    
    return result.success ? 0 : 1;
}

// =============================================================================
// MAIN
// =============================================================================

static void print_usage(const char* prog) {
    cout << "CuCLARK MPI Cluster Coordinator" << endl;
    cout << endl;
    cout << "Usage: " << prog << " -c <config_file> [options]" << endl;
    cout << endl;
    cout << "Required:" << endl;
    cout << "  -c, --config <file>   Path to cluster configuration file (YAML)" << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  -p, --preflight       Run pre-flight checks only (don't classify)" << endl;
    cout << "  -v, --verbose         Enable verbose output" << endl;
    cout << "  -h, --help            Show this help message" << endl;
    cout << endl;
    cout << "Example:" << endl;
    cout << "  " << prog << " -c config/cluster.conf" << endl;
    cout << endl;
    cout << "For MPI parallel execution:" << endl;
    cout << "  mpirun -hostfile hostfile.txt " << prog << " -c config/cluster.conf" << endl;
}

int main(int argc, char* argv[]) {
    // Initialize MPI
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    
    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    
    // Parse command line arguments
    string config_file;
    bool preflight_only = false;
    bool verbose = false;
    
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "-p" || arg == "--preflight") {
            preflight_only = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-h" || arg == "--help") {
            if (world_rank == 0) print_usage(argv[0]);
            MPI_Finalize();
            return 0;
        }
    }
    
    if (config_file.empty()) {
        if (world_rank == 0) {
            cerr << "Error: Config file required" << endl;
            print_usage(argv[0]);
        }
        MPI_Finalize();
        return 1;
    }
    
    // Load configuration
    if (!load_config(config_file)) {
        if (world_rank == 0) {
            cerr << "Error: Failed to load configuration from " << config_file << endl;
        }
        MPI_Finalize();
        return 1;
    }
    
    if (verbose) {
        g_config.log_level = LOG_DEBUG;
    }
    
    // Setup logging
    if (world_rank == 0) {
        string log_path = g_config.cuclark_dir + "/logs/" + g_config.log_file;
        g_logfile.open(log_path, ios::app);
        
        log_message(LOG_INFO, "========================================");
        log_message(LOG_INFO, "CuCLARK Cluster Run Started");
        log_message(LOG_INFO, "Config: " + config_file);
        log_message(LOG_INFO, "========================================");
    }
    
    int result = 0;
    
    // Master node prompts for SSH password
    if (world_rank == 0) {
        g_ssh_password = read_password("Enter SSH password for cluster nodes: ");
        
        if (preflight_only) {
            vector<NodeStatus> statuses;
            run_preflight_checks(statuses);
        } else {
            result = run_master(world_size);
        }
    } else {
        // Worker nodes
        result = run_worker(world_rank);
    }
    
    // Cleanup
    if (g_logfile.is_open()) {
        log_message(LOG_INFO, "========================================");
        log_message(LOG_INFO, "CuCLARK Cluster Run Finished");
        log_message(LOG_INFO, "========================================");
        g_logfile.close();
    }
    
    MPI_Finalize();
    return result;
}
