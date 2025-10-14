#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <cstring>

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

int check_database(string path)
{
    vector<string> errors;
    // expand path relative to HOME if needed
    string dbPath = path;
    const char *home = getenv("HOME");
    if (!dbPath.empty() && dbPath[0] == '~')
    {
        if (home)
            dbPath = string(home) + dbPath.substr(1);
    }
    else if (!dbPath.empty() && dbPath[0] != '/' && home)
    {
        dbPath = string(home) + string("/") + dbPath;
    }

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


int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cerr << "Invalid arguments" << endl;
        return 1;
    }

    string arg = argv[1];
    if (arg == "-i")
    {
        return handle_install();
    }

    if (arg == "-d" && check_database(argv[2]) == 1) {
        cerr << "Database error, exiting the program." << endl;
        return 1;
    }
    cout << "Database is ready." << endl;

    return 0;
}
