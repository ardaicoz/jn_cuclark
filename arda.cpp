#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
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

static int handle_install()
{
    const string logFile = "ardacpp_log.txt";
    bool installed = false;
    bool statusKnown = false;

    ifstream logIn(logFile.c_str());
    if (logIn)
    {
        string line;
        if (getline(logIn, line))
        {
            if (line == "INSTALLED=1")
            {
                installed = true;
                statusKnown = true;
            }
            else if (line == "INSTALLED=0")
            {
                installed = false;
                statusKnown = true;
            }
        }
        logIn.close();
    }
    else
    {
        ofstream logOut(logFile.c_str());
        if (!logOut)
        {
            cerr << "Failed to create " << logFile << endl;
            return 1;
        }
        logOut << "INSTALLED=0" << endl;
        logOut.close();
        statusKnown = true;
    }

    if (!statusKnown)
    {
        ofstream logOut(logFile.c_str());
        if (!logOut)
        {
            cerr << "Failed to reset " << logFile << endl;
            return 1;
        }
        logOut << "INSTALLED=0" << endl;
        logOut.close();
        installed = false;
    }

    if (installed)
    {
        cout << "Program is already installed." << endl;
        return 0;
    }

    int rc = system("./install.sh");
    if (rc != 0)
    {
        cerr << "Installation failed." << endl;
        return 1;
    }

    ofstream logOut(logFile.c_str());
    if (!logOut)
    {
        cerr << "Failed to update " << logFile << endl;
        return 1;
    }
    logOut << "INSTALLED=1" << endl;
    logOut.close();
    cout << "Installation completed successfully." << endl;
    return 0;
}

static int handle_database(const string &dbPath)
{
    if (dbPath.empty())
    {
        cerr << "Database path is empty." << endl;
        return 1;
    }

    string resolvedPath = resolve_database_path(dbPath);
    if (check_database(resolvedPath) != 0)
    {
        cerr << "Database error, exiting the program." << endl;
        return 1;
    }

    const string scriptPath = "./set_targets.sh";
    if (!exists_file(scriptPath))
    {
        cerr << "Set targets script not found: " << scriptPath << endl;
        return 1;
    }

    string command = string("./set_targets.sh ") + shell_quote(resolvedPath) + " custom";
    int rc = system(command.c_str());
    if (rc != 0)
    {
        cerr << "set_targets.sh failed with exit code " << rc << endl;
        return 1;
    }

    cout << "Database is ready." << endl;
    return 0;
}

static int handle_classification(const string &fastqFile, const string &resultFile, int batchSize)
{
    const string scriptPath = "./classify_metagenome.sh";
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

    string command = string("./classify_metagenome.sh -O ") +
                     shell_quote(fastqFile) + " -R " +
                     shell_quote(resultFile) + " -b " +
                     to_string(batchSize) + " --light";

    int rc = system(command.c_str());
    if (rc != 0)
    {
        cerr << "Classification command failed with exit code " << rc << endl;
        return 1;
    }

    cout << "Classification completed successfully." << endl;
    return 0;
}


int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cerr << "Usage: " << argv[0] << " -i | -d <database_path> | -c <fastq_file> <result_file> [batch_size]" << endl;
        return 1;
    }

    string arg = argv[1];
    if (arg == "-i")
    {
        return handle_install();
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
        if (argc >= 5)
        {
            if (!parse_positive_int(argv[4], batchSize))
            {
                cerr << "Invalid batch size. Provide a positive integer value." << endl;
                return 1;
            }
        }
        return handle_classification(argv[2], argv[3], batchSize);
    }

    cerr << "Unknown argument: " << arg << endl;
    cerr << "Usage: " << argv[0] << " -i | -d <database_path> | -c <fastq_file> <result_file> [batch_size]" << endl;
    return 1;
}
