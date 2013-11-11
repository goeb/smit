#ifndef _db_h
#define _db_h

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <pthread.h>
#include <stdint.h>

#include "ustring.h"
#include "mutexTools.h"

#define K_MESSAGE "+message" // keyword used for the message. Could be changed to _message ? TODO ?
#define K_SUMMARY "summary"

#define DELETE_DELAY_S (10*60) // seconds

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
    std::string getMessage();
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
    Issue() : ctime(0), mtime(0) {}

    // the properties of the issue is the consolidation of all the properties
    // of its entries. For a given key, the most recent value has priority.
    std::string getSummary() const;
    bool lessThan(const Issue *other, const std::list<std::pair<bool, std::string> > &sortingSpec) const;
    bool isInFilter(const std::map<std::string, std::list<std::string> > &filter);


};

enum PropertyType { F_TEXT, F_SELECT, F_MULTISELECT, F_SELECT_USER};
typedef struct PropertySpec {
    std::string name;
    enum PropertyType type;
    std::list<std::string> selectOptions; // for F_SELECT and F_MULTISELECT only
} FieldSpec;

struct PredefinedView {
    std::string name;
    std::map<std::string, std::list<std::string> > filterin;
    std::map<std::string, std::list<std::string> > filterout;
    std::string colspec;
    std::string sort;
    std::string search;
    static std::string getDirectionName(bool d);
    std::string generateQueryString() const;
};

struct ProjectConfig {
    std::map<std::string, PropertySpec> properties;
    std::list<std::string> orderedProperties;
    std::map<std::string, std::string> propertyLabels;
    std::map<std::string, PredefinedView> predefinedViews;

};




class Project {
public:
    static int load(const char *path, char *name); // load a project
    int loadConfig(const char *path);
    int loadEntries(const char *path);
    void loadPredefinedViews(const char *path);

    void consolidateIssues();
    std::vector<Issue*> search(const char *fulltextSearch,
                             const std::map<std::string, std::list<std::string> > &filterIn,
                             const std::map<std::string, std::list<std::string> > &filterOut,
                             const char *sortingSpec);
    int get(const char *issueId, Issue &issue, std::list<Entry*> &Entries);
    int addEntry(std::map<std::string, std::list<std::string> > properties, std::string &issueId, std::string username);
    Issue *getIssue(const std::string &id) const;
    Entry *getEntry(const std::string &id) const;
    bool searchFullText(const Issue* issue, const char *text) const;
    std::string getLabelOfProperty(const std::string &propertyName) const;

    inline std::string getName() const { return name; }
    inline std::string getPath() const { return path; }
    inline ProjectConfig getConfig() const { return config; }
    int writeHead(const std::string &issueId, const std::string &entryId);
    int deleteEntry(const std::string &issueId, const std::string &entryId, const std::string &username);
    std::list<std::string> getReservedProperties() const;
    std::list<std::string> getPropertiesNames() const;
    int modifyConfig(std::list<std::list<std::string> > &tokens);
    PredefinedView getPredefinedView(const std::string &name);

private:
    void consolidateIssue(Issue *i);

    ProjectConfig config;
    std::map<std::string, Issue*> issues;
    std::map<std::string, Entry*> entries;
    Locker locker;
    std::string name;
    std::string path;
    int maxIssueId;
};


class Database {
public:
    static Database Db;
    std::map<std::string, Project*> projects;
    static Project *getProject(const std::string &projectName);
    std::string pathToRepository;
    inline static std::string getRootDir() { return Db.pathToRepository; }
};


// Functions
int dbLoad(const char * pathToRepository); // initialize the given repository
std::list<std::pair<bool, std::string> > parseSortingSpec(const char *sortingSpec);


#endif
