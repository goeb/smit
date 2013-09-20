#ifndef _db_h
#define _db_h

#include <string>
#include <map>
#include <list>
#include <set>
#include <pthread.h>
#include <stdint.h>

#include "ustring.h"

#define K_MESSAGE "message" // keyword used for the message. Could be changed to _message ? TODO ?
#define K_TITLE "title"

// Data types

// Entry
struct Entry {
    std::string parent; // id of the parent entry, empty if top-level
    std::string id; // unique id of this entry
    long ctime; // creation time
    std::string author;
    std::map<std::string, std::list<std::string> > properties;
    std::string serialize();
    int getCtime() const;
    std::string getStringifiedProperty(const std::string &propertyName);
    Entry() : ctime(0) {};

};

// Issue
// An issue is consolidated over all its entries
struct Issue {
    std::string id; // same as the first entry
    std::string head; // the latest entry
    int ctime; // creation time (the one of the first entry)
    int mtime; // modification time (the one of the last entry)
    std::map<std::string, std::list<std::string> > properties;
    Issue() : ctime(0), mtime(0) {};

    // the properties of the issue is the consolidation of all the properties
    // of its entries. For a given key, the most recent value has priority.
    void loadHead(const std::string &issuePath);
    std::string getTitle() const;
};

enum FieldType { F_TEXT, F_SELECT, F_MULTISELECT, F_SELECT_USER};
typedef struct FieldSpec {
    std::string name;
    enum FieldType type;
    std::list<std::string> selectOptions; // for F_SELECT and F_MULTISELECT only
} FieldSpec;

// Project config
struct ProjectConfig {
    std::map<std::string, FieldSpec> fields;
    std::list<std::string> orderedFields;
    std::map<std::string, std::string> customDisplays;
    std::string defaultDislpay; // one of customDisplays
    
};



class Locker {
public:
    Locker();
    ~Locker();
    void lockForWriting();
    void unlockForWriting();
    void lockForReading();
    void unlockForReading();

private:
    pthread_mutex_t readOnlyMutex;
    pthread_mutex_t readWriteMutex;
    int nReaders; // number of concurrent readers
};


enum LockMode { LOCK_READ_ONLY, LOCK_READ_WRITE };

class AutoLocker {
public:
    inline AutoLocker(Locker & L, enum LockMode m) : locker(L), mode(m) {
        if (mode == LOCK_READ_ONLY) locker.lockForReading();
        else locker.lockForWriting();
    }
    inline ~AutoLocker() {
        if (mode == LOCK_READ_ONLY) locker.unlockForReading();
        else locker.unlockForWriting();
    }

private:
    Locker & locker;
    enum LockMode mode;
};


class Project {
public:
    static int load(const char *path, char *name); // load a project
    int loadConfig(const char *path);
    int loadEntries(const char *path);
    void consolidateIssues();
    std::list<Issue*> search(const char *fulltext, const char *filterSpec, const char *sortingSpec);
    inline std::list<std::string> getDefaultColspec() { return defaultColspec; }
    int get(const char *issueId, Issue &issue, std::list<Entry*> &Entries);
    int addEntry(std::map<std::string, std::list<std::string> > properties, std::string &issueId);
    Issue *getIssue(const std::string &id);

    inline std::string getName() const { return name; }
    inline std::string getPath() const { return path; }
    inline ProjectConfig getConfig() const { return config; }

private:
    ProjectConfig config;
    std::map<std::string, Issue*> issues;
    std::map<std::string, Entry*> entries;
    std::list<std::string> defaultColspec;
    Locker locker;
    std::string name;
    std::string path;
};


class Database {
public:
    static Database Db;
    std::map<std::string, Project*> projects;
    static Project *getProject(const std::string &projectName);
    std::string pathToRepository;

    static std::list<std::string> getDefautlColspec(const char *project);

};


// Functions
int dbInit(const char * pathToRepository); // initialize the given repository

// load in memory the given project
// re-load if it was previously loaded
int loadProject(const char *path);


// search
//  project: name of project where the search should be conducted
//  fulltext: text that is searched (optional: 0 for no fulltext search)
//  filterSpec: "status:open+label:v1.0+xx:yy"
//  sortingSpec: "id+title-owner" (+ for ascending, - for descending order)
// @return number of issues 
//  When fulltext search is enabled (fulltext != 0) then the search is done
//  through all entries.
std::list<struct Issue*> search(const char * project, const char *fulltext, const char *filterSpec, const char *sortingSpec);


// add an entry in the database
int add(const char *project, const char *issueId, const Entry &entry);

// Get a given issue and all its entries
int get(const char *project, const char *issueId, Issue &issue, std::list<Entry*> &Entries, ProjectConfig &config);


// Deleting an entry is only possible if:
//     - this entry is the HEAD (has no child)
//     - the deleting happens less than 5 minutes after creation of the entry
// @return TODO
int deleteEntry(std::string entry);

std::string bin2hex(const ustring & in);
std::string toString(const std::list<std::string> &values);

std::list<std::string> getProjectList();


#endif
