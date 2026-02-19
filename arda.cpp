#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace std;

static bool is_dir_nonempty(const string &dir)
{
    DIR *d = opendir(dir.c_str());
    if (!d)
        return false;
    struct dirent *ent;
    bool empty = true;
    while ((ent = readdir(d)) != NULL)
    {
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0)
        {
            empty = false;
            break;
        }
    }
    closedir(d);
    return !empty;
}

static bool exists_file(const string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static bool exists_dir(const string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static string resolve_database_path(const string &path)
{
    if (path.empty())
        return path;
    const char *home = getenv("HOME");
    if (path[0] == '~')
    {
        if (home)
            return string(home) + path.substr(1);
        return path;
    }
    if (path[0] != '/' && home)
        return string(home) + string("/") + path;
    return path;
}

static string shell_quote(const string &value)
{
    string quoted = "'";
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '\'')
            quoted += "'\"'\"'";
        else
            quoted += value[i];
    }
    quoted += "'";
    return quoted;
}

static bool parse_positive_int(const string &text, int &value)
{
    if (text.empty())
        return false;
    char *end = NULL;
    long parsed = strtol(text.c_str(), &end, 10);
    if (*end != '\0' || parsed <= 0 || parsed > INT_MAX)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

static bool parse_double(const string &text, double &value)
{
    if (text.empty() || text == "-")
        return false;
    char *end = NULL;
    value = strtod(text.c_str(), &end);
    if (*end != '\0')
        return false;
    return true;
}

static string format_percentage(double value)
{
    ostringstream oss;
    oss << fixed << setprecision(2) << value;
    return oss.str();
}
int check_database(string path)
{
    vector<string> errors;
    // expand path relative to HOME if needed
    string dbPath = resolve_database_path(path);

    bool dbExists = exists_dir(dbPath);
    bool dbNonEmpty = dbExists && is_dir_nonempty(dbPath);
    if (!dbExists)
        errors.push_back(string("Database directory not found: ") + dbPath);
    else if (!dbNonEmpty)
        errors.push_back(string("Database directory is empty: ") + dbPath);

    string customDir = dbPath + "/Custom";
    string taxonomyDir = dbPath + "/taxonomy";
    string taxonFile = dbPath + "/.taxondata";

    // Check required directories
    bool customExists = exists_dir(customDir);
    if (!customExists)
        errors.push_back(string("Missing directory: ") + customDir);
    bool taxonomyExists = exists_dir(taxonomyDir);
    if (!taxonomyExists)
        errors.push_back(string("Missing directory: ") + taxonomyDir);

    // check for fasta files in Custom (only if Custom exists)
    if (customExists)
    {
        bool hasFasta = false;
        DIR *cd = opendir(customDir.c_str());
        if (cd)
        {
            struct dirent *ent;
            while ((ent = readdir(cd)) != NULL)
            {
                string name = ent->d_name;
                if (name == "." || name == "..")
                    continue;
                size_t pos = name.find_last_of('.');
                if (pos != string::npos)
                {
                    string ext = name.substr(pos + 1);
                    if (ext == "fa" || ext == "fna" || ext == "fasta")
                    {
                        hasFasta = true;
                        break;
                    }
                }
            }
            closedir(cd);
        }
        if (!hasFasta)
            errors.push_back(string("No fasta files found in ") + customDir);
    }

    // Check taxonomy directory contents
    if (taxonomyExists)
    {
        vector<string> taxFiles = {"citations.dmp", "delnodes.dmp", "division.dmp",
                                   "gc.prt", "gencode.dmp", "images.dmp",
                                   "merged.dmp", "names.dmp", "nodes.dmp",
                                   "nucl_accss"};
        for (size_t i = 0; i < taxFiles.size(); ++i)
        {
            string f = taxonomyDir + "/" + taxFiles[i];
            if (!exists_file(f))
                errors.push_back(string("Missing file in taxonomy directory: ") + f);
        }
    }

    // Ensure .taxondata file exists
    if (!exists_file(taxonFile))
    {
        ofstream ofs(taxonFile.c_str());
        if (!ofs)
            errors.push_back(string("Failed to create ") + taxonFile);
    }

    if (!errors.empty())
    {
        cerr << "Database check found issues:" << endl;
        for (size_t i = 0; i < errors.size(); ++i)
            cerr << " - " << errors[i] << endl;
        return 1;
    }

    return 0;
}

static int handle_verify()
{
    cout << "========================================" << endl;
    cout << "  CuCLARK Installation Verification" << endl;
    cout << "========================================" << endl << endl;

    bool allOk = true;

    // Check 1: Binaries exist
    cout << "1. Checking binaries..." << endl;
    vector<string> required_bins;
    required_bins.push_back("bin/arda");
    required_bins.push_back("bin/cuCLARK");
    required_bins.push_back("bin/cuCLARK-l");
    required_bins.push_back("bin/getTargetsDef");
    required_bins.push_back("bin/getAccssnTaxID");
    required_bins.push_back("bin/getfilesToTaxNodes");
    required_bins.push_back("bin/getAbundance");

    for (size_t i = 0; i < required_bins.size(); i++)
    {
        if (exists_file(required_bins[i]))
        {
            cout << "   \u2713 " << required_bins[i] << endl;
        }
        else
        {
            cout << "   \u2717 " << required_bins[i] << " (missing)" << endl;
            allOk = false;
        }
    }
    cout << endl;

    // Check 2: Directory structure
    cout << "2. Checking directory structure..." << endl;
    vector<string> required_dirs;
    required_dirs.push_back("bin");
    required_dirs.push_back("logs");
    required_dirs.push_back("results");
    required_dirs.push_back("scripts");

    for (size_t i = 0; i < required_dirs.size(); i++)
    {
        if (exists_dir(required_dirs[i]))
        {
            cout << "   \u2713 " << required_dirs[i] << "/" << endl;
        }
        else
        {
            cout << "   \u2717 " << required_dirs[i] << "/ (missing)" << endl;
            allOk = false;
        }
    }
    cout << endl;

    // Check 3: Installation status
    cout << "3. Checking installation status..." << endl;
    const string logFile = "logs/ardacpp_log.txt";
    ifstream logIn(logFile.c_str());
    if (logIn)
    {
        string line;
        if (getline(logIn, line) && line == "INSTALLED=1")
        {
            cout << "   \u2713 Installation marker found" << endl;
        }
        else
        {
            cout << "   \u26A0 Installation incomplete or not verified" << endl;
            allOk = false;
        }
        logIn.close();
    }
    else
    {
        cout << "   \u2717 Installation log not found" << endl;
        allOk = false;
    }
    cout << endl;

    // Check 4: Database setup
    bool dbReady = false;
    cout << "4. Checking database setup..." << endl;
    if (exists_file("scripts/.settings"))
    {
        cout << "   \u2713 Database configured (scripts/.settings exists)" << endl;
        dbReady = true;
    }
    else
    {
        cout << "   \u26A0 Database not configured (run: arda -d <database_path>)" << endl;
    }
    cout << endl;

    // Summary
    cout << "========================================" << endl;
    if (allOk && dbReady)
    {
        cout << "Status: READY \u2713" << endl;
        cout << "========================================" << endl;
        return 0;
    }
    else if (allOk && !dbReady)
    {
        cout << "Status: Installation complete, database not ready" << endl;
        cout << "========================================" << endl;
        cout << endl;
        cout << "To set up database, run: arda -d <database_path>" << endl;
        return 1;
    }
    else
    {
        cout << "Status: INCOMPLETE" << endl;
        cout << "========================================" << endl;
        cout << endl;
        cout << "To complete installation, run: ./install.sh" << endl;
        return 1;
    }
}

static int handle_database(const string &dbPath)
{
    if (dbPath.empty())
    {
        cerr << "Database path is empty." << endl;
        return 1;
    }

    // Check if database is already set up
    if (exists_file("scripts/.settings"))
    {
        cerr << "Database is already configured (scripts/.settings exists)." << endl;
        cerr << "To reconfigure, you must first reset the database." << endl;
        return 1;
    }

    string resolvedPath = resolve_database_path(dbPath);
    if (check_database(resolvedPath) != 0)
    {
        cerr << "Database error, exiting the program." << endl;
        return 1;
    }

    const string scriptPath = "./scripts/set_targets.sh";
    if (!exists_file(scriptPath))
    {
        cerr << "Set targets script not found: " << scriptPath << endl;
        return 1;
    }

    // Change directory to scripts/ so the relative paths inside the shell scripts work
    string command = string("cd scripts && ./set_targets.sh ") + shell_quote(resolvedPath) + " custom";
    int rc = system(command.c_str());
    if (rc != 0)
    {
        cerr << "set_targets.sh failed with exit code " << rc << endl;
        return 1;
    }

    cout << "Database is ready." << endl;
    return 0;
}

static int handle_classification(const string &fastqFile, const string &resultFile, int batchSize, bool verbose = false)
{
    const string scriptPath = "./scripts/classify_metagenome.sh";
    if (!exists_file(scriptPath))
    {
        cerr << "Classification script not found: " << scriptPath << endl;
        return 1;
    }

    if (!exists_file(fastqFile))
    {
        cerr << "Input FASTQ file not found: " << fastqFile << endl;
        return 1;
    }

    if (batchSize <= 0)
    {
        cerr << "Batch size must be a positive integer." << endl;
        return 1;
    }

    // Resolve the result path to absolute because the classification script
    // runs from the scripts/ directory (cd scripts), so relative paths would
    // resolve against scripts/ instead of the project root.
    string absResultPath;
    if (!resultFile.empty() && resultFile[0] == '/')
    {
        absResultPath = resultFile;
    }
    else
    {
        char cwdBuf[4096];
        if (!getcwd(cwdBuf, sizeof(cwdBuf)))
        {
            cerr << "Failed to get current working directory." << endl;
            return 1;
        }
        absResultPath = string(cwdBuf) + "/results/" + resultFile;
    }

    string command = string("cd scripts && ./classify_metagenome.sh -O ") +
                     shell_quote(fastqFile) + " -R " + shell_quote(absResultPath) + " -b " +
                     to_string(batchSize) + " --light" + (verbose ? " --verbose" : "");


    int rc = system(command.c_str());
    if (rc != 0)
    {
        cerr << "Classification command failed with exit code " << rc << endl;
        return 1;
    }

    return 0;
}

static int handle_abundance(const string &dbPath, const string &resultFile)
{
    if (dbPath.empty())
    {
        cerr << "Database path is empty." << endl;
        return 1;
    }

    if (resultFile.empty())
    {
        cerr << "Result file path is empty." << endl;
        return 1;
    }

    const string scriptPath = "./scripts/estimate_abundance.sh";

    if (!exists_file(scriptPath))
    {
        cerr << "Abundance script not found: " << scriptPath << endl;
        return 1;
    }

    if (!exists_file(resultFile))
    {
        cerr << "Classification output not found: " << resultFile << endl;
        cerr << "Make sure you provide the correct path to the .csv file produced by classification." << endl;
        return 1;
    }

    string resolvedPath = resolve_database_path(dbPath);
    if (!exists_dir(resolvedPath))
    {
        cerr << "Database directory not found: " << resolvedPath << endl;
        return 1;
    }

    string command = string("./scripts/estimate_abundance.sh -D ") + shell_quote(resolvedPath) +
                     " -F " + shell_quote(resultFile) + string(" > results/abundance_result.txt");

    int rc = system(command.c_str());
    if (rc != 0)
    {
        cerr << "Abundance estimation failed with exit code " << rc << endl;
        return 1;
    }

    cout << "Abundance estimation completed successfully." << endl;
    return 0;
}

static int handle_report()
{
    const string reportFile = "results/abundance_result.txt";
    if (!exists_file(reportFile))
    {
        cerr << "Abundance result file not found: " << reportFile << endl;
        return 1;
    }

    ifstream in(reportFile.c_str());
    if (!in)
    {
        cerr << "Failed to open " << reportFile << endl;
        return 1;
    }

    string header;
    if (!getline(in, header))
    {
        cerr << "Abundance result file is empty." << endl;
        return 1;
    }

    const string outputFile = "results/report.txt";
    ofstream out(outputFile.c_str());
    if (!out)
    {
        cerr << "Failed to open " << outputFile << " for writing." << endl;
        return 1;
    }

    struct Entry
    {
        string name;
        double propAll;
        double propClassified;
    };

    vector<Entry> entries;
    string line;
    while (getline(in, line))
    {
        if (line.empty())
            continue;
        vector<string> parts;
        string part;
        istringstream ss(line);
        while (getline(ss, part, ','))
            parts.push_back(part);
        if (parts.size() < 6)
            continue;

        if (parts[0] == "UNKNOWN")
            continue;

        double propAll = 0.0;
        double propClassified = 0.0;
        if (!parse_double(parts[4], propAll))
            continue;
        if (!parse_double(parts[5], propClassified))
            continue;

        entries.push_back(Entry{parts[0], propAll, propClassified});
    }

    if (entries.empty())
    {
        out << "RESULT" << endl;
        out << "No classified pathogens found in " << reportFile << "." << endl;
        cout << "Report written to " << outputFile << endl;
        return 0;
    }

    sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (a.propClassified == b.propClassified)
            return a.name < b.name;
        return a.propClassified > b.propClassified;
    });

    out << "RESULT" << endl;
    out << "Your read contains these pathogens, the percentage of all input reads (including unclassified) "
           "that hit this taxon and the percentage among only the reads that got classified that hit this taxon."
        << endl;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const Entry &e = entries[i];
        out << "- " << e.name << ": " << format_percentage(e.propAll) << "% among all, "
            << format_percentage(e.propClassified) << "% among classified" << endl;
    }

    cout << "Report written to " << outputFile << endl;
    return 0;
}


int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cerr << "Usage: " << argv[0] << " [OPTIONS]" << endl;
        cerr << "Options: -h, --help, -v/--verify, -d <database_path>, -c <fastq> <result> [batch] [--verbose], -a <database> <result>, -r" << endl;
        return 1;
    }

    string arg = argv[1];
    if (arg == "-h" || arg == "--help")
    {
        cout << "Usage: " << argv[0] << " [OPTIONS]" << endl;
        cout << endl;
        cout << "Options:" << endl;
        cout << "  -v, --verify              Verify installation status" << endl;
        cout << "  -d <database_path>        Setup database targets" << endl;
        cout << "  -c <fastq> <result> [batch] [--verbose]  Classify reads (default batch=32)" << endl;
        cout << "  -a <database> <result>    Estimate abundance" << endl;
        cout << "  -r                        Generate report" << endl;
        cout << "  -h, --help                Show this help" << endl;
        return 0;
    }

    if (arg == "-v" || arg == "--verify")
    {
        return handle_verify();
    }

    if (arg == "-d")
    {
        if (argc < 3)
        {
            cerr << "Missing database path for -d option." << endl;
            return 1;
        }
        return handle_database(argv[2]);
    }

    if (arg == "-c")
    {
        if (argc < 4)
        {
            cerr << "Classification requires a FASTQ input file and a result output file." << endl;
            return 1;
        }

        int batchSize = 32;
        bool verbose = false;
        for (int i = 4; i < argc; i++)
        {
            string a(argv[i]);
            if (a == "--verbose")
                verbose = true;
            else if (!parse_positive_int(argv[i], batchSize))
            {
                cerr << "Usage: " << argv[0] << " -c <fastq> <result> [batch] [--verbose]" << endl;
                return 1;
            }
        }
        return handle_classification(argv[2], argv[3], batchSize, verbose);
    }

    if (arg == "-a")
    {
        if (argc < 4)
        {
            cerr << "Usage: " << argv[0] << " -a <database_path> <result_file>" << endl;
            cerr << "  <result_file> is the .csv file produced by classification (e.g. results/result.csv)" << endl;
            return 1;
        }
        return handle_abundance(argv[2], argv[3]);
    }

    if (arg == "-r")
    {
        if (argc > 2)
        {
            cerr << "-r option does not take additional arguments." << endl;
            return 1;
        }
        return handle_report();
    }

    cerr << "Unknown argument: " << arg << endl;
    cerr << "Usage: " << argv[0] << " -v | -d <database_path> | -c <fastq_file> <result_file> [batch_size] | -a <database_path> <result_file> | -r" << endl;
    return 1;
}
