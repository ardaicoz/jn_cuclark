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

    if (!exists_dir(dbPath) || !is_dir_nonempty(dbPath))
    {
        cerr << "Database directory not found or empty: " << dbPath << endl;
        return 1;
    }

    string customDir = dbPath + "/Custom";
    string taxonomyDir = dbPath + "/taxonomy";
    string taxonFile = dbPath + "/.taxondata";

    // Check required directories
    if (!exists_dir(customDir))
    {
        cerr << "Missing directory: " << customDir << endl;
        return 1;
    }
    if (!exists_dir(taxonomyDir))
    {
        cerr << "Missing directory: " << taxonomyDir << endl;
        return 1;
    }

    // check for fasta files in Custom
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
    {
        cerr << "No fasta files found in " << customDir << endl;
        return 1;
    }

    // Check taxonomy directory contents
    vector<string> taxFiles = {"citations.dmp", "delnodes.dmp", "division.dmp",
                              "gc.prt", "gencode.dmp", "images.dmp",
                              "merged.dmp", "names.dmp", "nodes.dmp",
                              "nucl_accss"};
    for (size_t i = 0; i < taxFiles.size(); ++i)
    {
        string f = taxonomyDir + "/" + taxFiles[i];
        if (!exists_file(f))
        {
            cerr << "Missing file in taxonomy directory: " << f << endl;
            return 1;
        }
    }

    // Ensure .taxondata file exists
    if (!exists_file(taxonFile))
    {
        ofstream ofs(taxonFile.c_str());
        if (!ofs)
        {
            cerr << "Failed to create " << taxonFile << endl;
            return 1;
        }
    }

    return 0;
}


int main() {

}
