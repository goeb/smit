#include <string>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdint.h>

#include "db.h"
#include "parseConfig.h"

#define LOG_ERROR(...) { printf("ERROR "); printf(__VA_ARGS__); printf("\n"); }
#define LOG_INFO(...)  { printf("INFO  "); printf(__VA_ARGS__); printf("\n"); }
#define LOG_DEBUG(...) { printf("DEBUG "); printf(__VA_ARGS__); printf("\n"); }

#define PROJECT_FILE "project"
#define ENTRIES "entries" // sub-directory of a project where the entries are stored

class Project {
public:
    static int load(const char *path); // load a project
    int loadConfig(const char *path);
    int loadEntries(const char *path);
private:
    ProjectConfig config;
    std::map<ustring, Issue*> issues;
    std::map<ustring, Entry*> entries;
};

class Database {
public:
    static Database Db;
    std::map<ustring, Project*> projects;
};
Database Database::Db;


int init(const char * pathToRepository)
{
    // look for all files "pathToRepository/p/project"
    // and parse them
    // then, load all pathToRepository/p/entries/*/*

    DIR *dirp;

    if ((dirp = opendir(pathToRepository)) == NULL) {
        return -1;

    } else {
        struct dirent *dp;

        while ((dp = readdir(dirp)) != NULL) {
            // Do not show current dir and hidden files
            if (0 == strcmp(dp->d_name, ".")) continue;
            if (0 == strcmp(dp->d_name, "..")) continue;
            std::string pathToProject = pathToRepository;
            pathToProject += '/' + dp->d_name;
            int r = Project::load(pathToProject.c_str());
        }
    }
    return Database::Db.projects.size();
}

// load in memory the given project
// re-load if it was previously loaded
// @return 0 if success, -1 if failure
int Project::load(const char *path)
{
    LOG_INFO("Loadingh project %s...", path);
    Project *p = new Project;

    int r = p->loadConfig(path);
    if (r == -1) {
        delete p;
        LOG_ERROR("Project '%s' not loaded because of errors while reading the config.", path);
        return -1;
    }

    r = p->loadEntries(path);
    if (r == -1) {
        LOG_ERROR("Project '%s' not loaded because of errors while reading the entries.", path);
        delete p;
        return -1;
    }
    return 0;
}


Entry *loadEntry(std::string dir, const char* basename)
{
    // load a given entry
    std::string path = dir + '/' + basename;
    FILE *f = fopen(path.c_str(), "r");
    if (NULL == f) {
        printf("debug: could not open file '%s', %s", path.c_str(), strerror(errno));
        return 0;
    }
    // else continue and parse the file

    int r = fseek(f, 0, SEEK_END); // go to the end of the file
    if (r != 0) {
        LOG_ERROR("could not fseek(%s): %s", path.c_str(), strerror(errno));
        fclose(f);
        return 0;
    }
    long filesize = ftell(f);
    if (filesize > 4*1024*1024) { // max 1 MByte
        LOG_ERROR("loadConfig: file %s over-sized (%ld bytes)", path.c_str(), filesize);
        fclose(f);
        return 0;
    }
    rewind(f);
    unsigned char *buf = (unsigned char *)malloc(filesize);
    size_t n = fread(buf, 1, filesize, f);
    if (n != filesize) {
        LOG_ERROR("fread(%s): short read. feof=%d, ferror=%d", path.c_str(), feof(f), ferror(f));
        fclose(f);
        return 0;
    }

    Entry *e = new Entry;
    e->id = (uint8_t*)basename;


    std::list<std::list<ustring> > lines = parseConfig(buf, n);
    return e;
}

int Project::loadEntries(const char *path)
{
    // load files path/entries/*/*
    DIR *dirp;
    std::string pathToEntries = path;
    pathToEntries += "/";
    pathToEntries += ENTRIES;
    if ((dirp = opendir(pathToEntries.c_str())) == NULL) {
        return -1;

    } else {
        struct dirent *dp;

        while ((dp = readdir(dirp)) != NULL) {
            // Do not show current dir and hidden files
            if (0 == strcmp(dp->d_name, ".")) continue;
            if (0 == strcmp(dp->d_name, "..")) continue;
            std::string subPath = path;
            subPath += '/' + dp->d_name;

            // open this subdir and look for all files of this subdir
            DIR *subdirp;
            if ((subdirp = opendir(subPath.c_str())) == NULL) continue; // subdir not a directory
            else {
                struct dirent *subdp;
                while ((subdp = readdir(subdirp)) != NULL) {
                    std::string filePath = subPath + '/' + subdp->d_name;
                    Entry *e = loadEntry(subPath, subdp->d_name);
                    if (e) entries[e->id] = e;
                    else LOG_ERROR("Cannot load entry '%s'", filePath.c_str());
                }
            }

        }
    }

}

FieldSpec parseFieldSpec(std::list<ustring> & tokens)
{
    // Supported syntax:
    // name type params ...
    // type = text | select | multiselect
    FieldSpec field;
    if (tokens.size() >= 2) {
        field.name = tokens.front();
        tokens.pop_front();
        ustring type = tokens.front();
        tokens.pop_front();
        if (0 == type.compare((uint8_t*)"text")) field.type = F_TEXT;
        else if (0 == type.compare((uint8_t*)"selectUser")) field.type = F_SELECT_USER;
        else if (0 == type.compare((uint8_t*)"select")) field.type = F_SELECT;
        else if (0 == type.compare((uint8_t*)"multiselect")) field.type = F_MULTISELECT;
        else { // error, unknown type
            LOG_ERROR("Unkown field type '%s'", type.c_str());
            field.name.clear();
            return field; // error, indicated to caller by empty name of field
        }

        if (F_SELECT == field.type || F_MULTISELECT == field.type) {
            // populate the allowed values
            while (tokens.size() > 0) {
                ustring value = tokens.front();
                tokens.pop_front();
                field.selectOptions.push_back(value);
            }
        }
    } else { // not enough tokens
        LOG_ERROR("Not enough tokens");
        return field; // error, indicated to caller by empty name of field
    }
}

// @return 0 if OK, -1 on error
int Project::loadConfig(const char *path)
{
    std::string pathToProjectFile = path;
    pathToProjectFile += '/' + PROJECT_FILE;
    FILE *f = fopen(pathToProjectFile.c_str(), "r");
    if (NULL == f) {
        printf("debug: could not open file %s, %s", pathToProjectFile.c_str(), strerror(errno));
        return -1;
    }
    // else continue and parse the file

    config.name = (unsigned char *)path; // name of the project

    int r = fseek(f, 0, SEEK_END); // go to the end of the file
    if (r != 0) {
        LOG_ERROR("could not fseek(%s): %s", path, strerror(errno));
        fclose(f);
        return -1;
    }
    long filesize = ftell(f);
    if (filesize > 1024*1024) {
        LOG_ERROR("loadConfig: file %s over-sized (%ld bytes)", path, filesize);
        fclose(f);
        return -1;
    }
    rewind(f);
    unsigned char *buf = (unsigned char *)malloc(filesize);
    size_t n = fread(buf, 1, filesize, f);
    if (n != filesize) {
        LOG_ERROR("fread(%s): short read. feof=%d, ferror=%d", path, feof(f), ferror(f));
        fclose(f);
        return -1;
    }
    std::list<std::list<ustring> > lines = parseConfig(buf, n);

    std::list<std::list<ustring> >::iterator line;
    for (line = lines.begin(); line != lines.end(); line++) {
        ustring token = line->front();
        line->pop_front();
        if (0 == token.compare((uint8_t*)"addField")) {
            FieldSpec field = parseFieldSpec(*line);
            if (field.name.size() > 0) config.fields[field.name] = field;
            // else: parse error
        } else if (0 == token.compare((uint8_t*)"addListDisplay")) {
            // TODO
        } else if (0 == token.compare((uint8_t*)"setDefaultColspec")) {
            // TODO
        } else if (0 == token.compare((uint8_t*)"setDefaultFilter")) {
            // TODO
         } else if (0 == token.compare((uint8_t*)"setDefaultSorting")) {
            // TODO
        }
    }

    fclose(f);
    return 0;
}


// search
//  project: name of project where the search should be conduected
//  fulltext: text that is searched (optional: 0 for no fulltext search)
//  filterSpec: "status:open+label:v1.0+xx:yy"
//  sortingSpec: "id+title-owner" (+ for ascending, - for descending order)
// @return number of issues 
//  When fulltext search is enabled (fulltext != 0) then the search is done
//  through all entries.
std::list<struct Issue> search(const char * project, const char *fulltext, const char *filterSpec, const char *sortingSpec)
{
}


// add an entry in the database
int add(const char *project, const char *issueId, const Entry &entry)
{
}

// Get a given issue and all its entries
int get(const char *project, const char *issueId, Issue &issue, std::list<Entry> &Entries)
{
}


// Deleting an entry is only possible if:
//     - this entry is the HEAD (has no child)
//     - the deleting happens less than 5 minutes after creation of the entry
// @return TODO
int deleteEntry(ustring entry)
{
}


void setId(Entry &entry)
{
    // set the id, which is the SAH1 sum of the structure
    unsigned char md[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)&entry, sizeof(entry), md);
    entry.id.assign(md, SHA_DIGEST_LENGTH);
}

ustring bin2hex(const ustring & in)
{
    const char hexTable[] = { '0', '1', '2', '3',
                                     '4', '5', '6', '7',
                                     '8', '9', 'a', 'b',
                                     'c', 'd', 'e', 'f' };
    ustring hexResult;
    size_t i;
    size_t L = in.size();
    for (i=0; i<L; i++) {
        int c = in[i];
        hexResult += hexTable[c >> 4];
        hexResult += hexTable[c & 0x0f];
    }
    return hexResult;
}
