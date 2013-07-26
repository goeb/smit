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
#include "logging.h"


#define PROJECT_FILE "project"
#define ENTRIES "entries" // sub-directory of a project where the entries are stored
#define HEAD "_HEAD"

#define K_PARENT "_parent"
#define K_AUTHOR "_author"
#define K_CTIME "_ctime"

Database Database::Db;


int dbInit(const char * pathToRepository)
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
            LOG_DEBUG("d_name=%s", dp->d_name);
            if (0 == strcmp(dp->d_name, ".")) continue;
            if (0 == strcmp(dp->d_name, "..")) continue;
            std::string pathToProject = pathToRepository;
            pathToProject += '/';
            pathToProject += dp->d_name;
            int r = Project::load(pathToProject.c_str(), dp->d_name);
        }
    }
    return Database::Db.projects.size();
}
void Issue::loadHead(const std::string &issuePath)
{
    std::string headPath = issuePath + '/' + HEAD;
    uint8_t *buf = 0;
    int n = loadFile(headPath.c_str(), &buf);

    if (n <= 0) return; // error or empty file

    head.assign(buf, n);
    free(buf);
}

// load in memory the given project
// re-load if it was previously loaded
// @return 0 if success, -1 if failure
int Project::load(const char *path, char *name)
{
    LOG_INFO("Loading project %s...", path);
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
    LOG_INFO("Project %s loaded.", path);

    p->consolidateIssues();

    // store the project in memory
    Database::Db.projects[name] = p;
    return 0;
}


Entry *loadEntry(std::string dir, const char* basename)
{
    // load a given entry
    std::string path = dir + '/' + basename;
    unsigned char *buf = 0;
    int n = loadFile(path.c_str(), &buf);

    if (n <= 0) return 0; // error or empty file

    Entry *e = new Entry;
    e->id = (uint8_t*)basename;

    std::list<std::list<ustring> > lines = parseConfig(buf, n);
    free(buf);

    std::list<std::list<ustring> >::iterator line;
    int lineNum = 0;
    for (line=lines.begin(); line != lines.end(); line++) {
        lineNum++;
        // each line should be a key / value pair
        if (line->size() < 2) {
            LOG_ERROR("Invalid line size %d (%s:%d)", line->size(), path.c_str(), lineNum);
            continue; // ignore this line
        }
        ustring key = line->front();
        ustring value = line->back();

        if (0 == key.compare((uint8_t*)K_CTIME)) {
            e->ctime = atoi((char*)value.c_str());
        } else if (0 == key.compare((uint8_t*)K_PARENT)) e->parent = value;
        else if (0 == key.compare((uint8_t*)K_AUTHOR)) e->author = value;
        else if (line->size() == 2) {
            e->singleProperties[key] = value;
        } else {
            // multi properties
            line->pop_front(); // remove key from tokens
            e->multiProperties[key] = *line;
        }
    }
    return e;
}

// 1. Walk through all loaded issues and compute the head
//    if it was not found earlier during the loadEntries.
// 2. Once the head is found, walk through all the entries
//    and fulfill the properties of the issue.
void Project::consolidateIssues()
{
    LOG_DEBUG("consolidateIssues()...");
    std::map<ustring, Issue*>::iterator i;
    for (i = issues.begin(); i != issues.end(); i++) {
        Issue *currentIssue = i->second;
        LOG_DEBUG("consolidateIssues() issue %s...", (char*)currentIssue->id.c_str());

        if (currentIssue->head.size() == 0) {
            // repair the head
            LOG_INFO("Missing head for issue '%s'. Repairing TODO...", currentIssue->id.c_str());
        }
        // now that the head is found, walk through all entries
        // following the _parent properties.

        ustring parentId = currentIssue->head;
        LOG_DEBUG("parentId=%s", parentId.c_str());
        while (0 != parentId.compare((uint8_t*)"null")) {
            std::map<ustring, Entry*>::iterator e = entries.find(parentId);
            if (e == entries.end()) {
                LOG_ERROR("Broken chain of entries: missing reference '%s'",
                          parentId.c_str());
                break; // abort the loop
            }
            Entry *currentEntry = e->second;
            // for each property of the parent,
            // create the same property in the issue, if not already existing
            // (in order to have only most recent properties)

            // singleProperties
            std::map<ustring, ustring>::iterator p;
            for (p = currentEntry->singleProperties.begin(); p != currentEntry->singleProperties.end(); p++) {
                if (currentIssue->singleProperties.count(p->first) == 0) {
                    // not already existing. Create it.
                    currentIssue->singleProperties[p->first] = p->second;
                }
            }

            // multiProperties
            std::map<ustring, std::list<ustring> >::iterator mp;
            for (mp = currentEntry->multiProperties.begin(); mp != currentEntry->multiProperties.end(); mp++) {
                if (currentIssue->multiProperties.count(mp->first) == 0) {
                    // not already existing. Create it.
                    currentIssue->multiProperties[mp->first] = mp->second;
                }
            }
            parentId = currentEntry->parent;
            LOG_DEBUG("Next parent: %s", parentId.c_str());
        }
    }
    LOG_DEBUG("consolidateIssues() done.");
}
int Project::loadEntries(const char *path)
{
    // load files path/entries/*/*
    // The tree of entries is as follows:
    // myproject/entries/XXX/XXX (XXX is the issue id)
    // myproject/entries/XXX/ABCDEF012345
    // myproject/entries/XXX/123356AF
    // myproject/entries/XXX/_HEAD
    // myproject/entries/YYYYYY/YYYYYY (YYYYYY is another issue id)
    // myproject/entries/YYYYYY/_HEAD
    // etc.
    std::string pathToEntries = path;
    pathToEntries = pathToEntries + '/' + ENTRIES;
    DIR *enriesDirHandle;
    if ((enriesDirHandle = opendir(pathToEntries.c_str())) == NULL) {
        LOG_ERROR("Cannot open directory '%s'", pathToEntries.c_str());
        return -1;

    } else {
        struct dirent *issueDir;

        while ((issueDir = readdir(enriesDirHandle)) != NULL) {
            if (0 == strcmp(issueDir->d_name, ".")) continue;
            if (0 == strcmp(issueDir->d_name, "..")) continue;

            std::string issuePath = pathToEntries;
            issuePath = issuePath + '/' + issueDir->d_name;
            // open this subdir and look for all files of this subdir
            DIR *issueDirHandle;
            if ((issueDirHandle = opendir(issuePath.c_str())) == NULL) continue; // not a directory
            else {
                // we are in a issue directory
                Issue *issue = new Issue();
                issue->id.assign((uint8_t*)issueDir->d_name);
                issues[issue->id] = issue;

                struct dirent *entryFile;
                while ((entryFile = readdir(issueDirHandle)) != NULL) {
                    if (0 == strcmp(entryFile->d_name, ".")) continue;
                    if (0 == strcmp(entryFile->d_name, "..")) continue;
                    if (0 == strcmp(entryFile->d_name, HEAD)) {
                        // this is the HEAD, ie: the lastest entry.
                        issue->loadHead(issuePath);
                        continue;
                    }
                    std::string filePath = issuePath + '/' + entryFile->d_name;
                    Entry *e = loadEntry(issuePath, entryFile->d_name);
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
    pathToProjectFile = pathToProjectFile + '/' + PROJECT_FILE;
    FILE *f = fopen(pathToProjectFile.c_str(), "r");
    if (NULL == f) {
        LOG_DEBUG("Could not open file %s, %s", pathToProjectFile.c_str(), strerror(errno));
        return -1;
    }
    // else continue and parse the file

    config.name = (unsigned char *)path; // name of the project

    int r = fseek(f, 0, SEEK_END); // go to the end of the file
    if (r != 0) {
        LOG_ERROR("could not fseek(%s): %s", pathToProjectFile.c_str(), strerror(errno));
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
        LOG_ERROR("fread(%s): short read. feof=%d, ferror=%d", pathToProjectFile.c_str(), feof(f), ferror(f));
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
            LOG_ERROR("addListDisplay not implemented");
        } else if (0 == token.compare((uint8_t*)"setDefaultColspec")) {
            // TODO
            LOG_ERROR("setDefaultColspec not implemented");
        } else if (0 == token.compare((uint8_t*)"setDefaultFilter")) {
            // TODO
            LOG_ERROR("setDefaultFilter not implemented");
         } else if (0 == token.compare((uint8_t*)"setDefaultSorting")) {
            // TODO
            LOG_ERROR("setDefaultSorting not implemented");
        } else {
            LOG_ERROR("Unknown function '%s'", token.c_str());

        }
    }

    fclose(f);
    return 0;
}


// search
//   project: name of project where the search should be conduected
//   fulltext: text that is searched (optional: 0 for no fulltext search)
//   filterSpec: "status:open+label:v1.0+xx:yy"
//   sortingSpec: "id+title-owner" (+ for ascending, - for descending order)
//
// @return number of issues
//
//   When fulltext search is enabled (fulltext != 0) then the search is done
//   through all entries.
std::list<struct Issue*> search(const char * project, const char *fulltext, const char *filterSpec, const char *sortingSpec)
{
    std::map<std::string, Project*>::iterator p = Database::Db.projects.find(project);
    if (p == Database::Db.projects.end()) {
        LOG_ERROR("Invald project: %s", project);
        std::list<struct Issue*> result;
        return result; // return empty list
    } else {
        if (!p->second) {
            LOG_ERROR("Invalid null pointer for project '%s'", project);
            return std::list<struct Issue*>(); // empty list
        }
        return p->second->search(fulltext, filterSpec, sortingSpec);


    }
}
std::list<Issue*> Project::search(const char *fulltext, const char *filterSpec, const char *sortingSpec)
{
    std::list<struct Issue*> result;
    std::map<ustring, Issue*>::iterator i;
    for (i=issues.begin(); i!=issues.end(); i++) {
        result.push_back(i->second);
    }
    return result;
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

bool Database::hasProject(const std::string & projectName)
{
    if (1 == Database::Db.projects.count(projectName)) {
        return true;
    } else return false;
}

std::list<std::string> getProjectList()
{
    std::list<std::string> pList;
    std::map<std::string, Project*>::iterator p;
    for (p = Database::Db.projects.begin(); p!=Database::Db.projects.end(); p++) {
        pList.push_back(p->first);
    }
    return pList;
}
