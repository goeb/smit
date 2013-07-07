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

#include "db.h"

#define LOG_ERROR(_format, ...) printf("ERROR: " _format, __VA_ARGS__)
#define LOG_DEBUG(_format, ...) printf("DEBUG: " _format, __VA_ARGS__)

#define PROJECT_FILE "project"

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
    // then, load all pathToRepository/p/issues/*/*

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
    Project *p = new Project;

    int r = p->loadConfig(path);
    if (r == -1) {
        delete p;
        return -1;
    }

    r = p->loadEntries(path);
    if (r == -1) {
        delete p;
        return -1;
    }
    return 0;
}

int Project::loadEntries(const char *path)
{
}

std::list<std::list<ustring> > parseConfig(const unsigned char *buf, size_t len)
{
    std::list<std::list<ustring> > linesOftokens;
    size_t i = 0;
    enum State {P_READY, P_IN_DOUBLE_QUOTES, P_IN_BACKSLASH, P_IN_COMMENT, P_IN_BACKSLASH_IN_DOUBLE_QUOTES};
    enum State state = P_READY;
    ustring token; // current token
    std::list<ustring> line; // current line
    for (i=0; i<len; i++) {
        unsigned char c = buf[i];
        switch (state) {
        case P_IN_BACKSLASH:
            if (c == '\n') { // new line escaped
                // nothing particular here
            } else {
                token += c;
            }
            state = P_READY;
            break;

        case P_IN_DOUBLE_QUOTES:
            if (c == '\\') {
                state = P_IN_BACKSLASH_IN_DOUBLE_QUOTES;
            } else if (c == '"') {
                state = P_READY; // end of double-quoted string
            } else token += c;
            break;
        case P_IN_BACKSLASH_IN_DOUBLE_QUOTES:
            token += c;
            state = P_IN_DOUBLE_QUOTES;
            break;
        case P_READY:
        default:
            if (c == '\n') {
                if (token.size() > 0) { line.push_back(token); token.clear(); }
                if (line.size() > 0) { linesOftokens.push_back(line); line.clear(); }
            } else if (c == ' ' || c == '\t' || c == '\r') {
                // current toke is done (because c is a token delimiter)
                if (token.size() > 0) { line.push_back(token); token.clear(); }
            } else if (c == '#') {
                if (token.size() > 0) { line.push_back(token); token.clear(); }
                state = P_IN_COMMENT;
            } else if (c == '\\') {
                state = P_IN_BACKSLASH;
            } else if (c == '"') {
                state = P_IN_DOUBLE_QUOTES;
            } else {
                token += c;
            }
            break;
        }
    }
    // purge remaininig token and line
    if (token.size() > 0) line.push_back(token);
    if (line.size() > 0) linesOftokens.push_back(line);

    return linesOftokens;
}

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
    std::list<std::list<ustring> > tokens = parseConfig(buf, n);



        std::set<FieldSpec> fields;
        std::map<ustring, ustring> customDisplays;
        ustring defaultDislpay; // one of customDisplays

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
