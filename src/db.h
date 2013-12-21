#ifndef _db_h
#define _db_h

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <stdint.h>

#include "ustring.h"
#include "mutexTools.h"
#include "stringTools.h"

#define K_MESSAGE "+message" // keyword used for the message. Could be changed to _message ? TODO ?
#define K_FILE "+file" // keyword used for the message. Could be changed to _message ? TODO ?
#define K_SUMMARY "summary"
#define K_UPLOADED_FILES_DIR "files"

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
    Entry() : ctime(0), next(0), prev(0) {}

    // chainlist pointers
    struct Entry *next; // child
    struct Entry *prev; // parent

};

// Issue
// An issue is consolidated over all its entries
struct Issue {
    std::string id; // same as the first entry
    Entry *latest; // the latest entry
    int ctime; // creation time (the one of the first entry)
    int mtime; // modification time (the one of the last entry)
    std::map<std::string, std::list<std::string> > properties;
    Issue() : latest(0), ctime(0), mtime(0) {}

    // the properties of the issue is the consolidation of all the properties
    // of its entries. For a given key, the most recent value has priority.
    std::string getSummary() const;
    bool lessThan(const Issue *other, const std::list<std::pair<bool, std::string> > &sortingSpec) const;
    bool isInFilter(const std::map<std::string, std::list<std::string> > &filter);
    std::map<std::string, Entry*> entries;

    int computeLatestEntry();
    void consolidate();
    void consolidateIssueWithSingleEntry(Entry *e, bool overwrite);
    bool searchFullText(const char *text) const;



};

enum PropertyType { F_TEXT, F_SELECT, F_MULTISELECT, F_SELECT_USER};
int strToPropertyType(const std::string &s, PropertyType &out);

struct PropertySpec {
    std::string name;
    std::string label;
    enum PropertyType type;
    std::list<std::string> selectOptions; // for F_SELECT and F_MULTISELECT only
};

struct PredefinedView {
    std::string name;
    std::map<std::string, std::list<std::string> > filterin;
    std::map<std::string, std::list<std::string> > filterout;
    std::string colspec;
    std::string sort;
    std::string search;
    bool isDefault; // indicate if this view should be chosen by default when query string is empty

    PredefinedView() : isDefault(false) {}
    static std::string getDirectionName(bool d);
    static std::string getDirectionSign(const std::string &text);
    std::string generateQueryString() const;
    std::string serialize() const;
    static std::map<std::string, PredefinedView> parsePredefinedViews(std::list<std::list<std::string> > lines);

};

struct ProjectConfig {
    std::map<std::string, PropertySpec> properties;
    std::list<std::string> orderedProperties;
    std::map<std::string, std::string> propertyLabels;
    std::map<std::string, PredefinedView> predefinedViews;

};




class Project {
public:
    static int load(const char *path, char *basename); // load a project
    int loadConfig(const char *path);
    int loadEntries(const char *path);
    void loadPredefinedViews(const char *path);

    void consolidateIssues();
    std::vector<Issue*> search(const char *fulltextSearch,
                             const std::map<std::string, std::list<std::string> > &filterIn,
                             const std::map<std::string, std::list<std::string> > &filterOut,
                             const char *sortingSpec);
    int get(const char *issueId, Issue &issue);
    int addEntry(std::map<std::string, std::list<std::string> > properties, std::string &iid, std::string &eid, std::string username);
    Issue *getIssue(const std::string &id) const;
    Entry *getEntry(const std::string &id) const;
    std::string getLabelOfProperty(const std::string &propertyName) const;

    inline std::string getName() const { return name; }
    inline std::string getUrlName() const { return urlNameEncode(name); }
    inline static std::string urlNameEncode(const std::string &name) { return urlEncode(name, '=', "._-"); }
    inline static std::string urlNameDecode(const std::string &name) { return urlDecode(name, false, '='); }
    inline std::string getPath() const { return path; }
    inline ProjectConfig getConfig() const { return config; }
    int deleteEntry(const std::string &issueId, const std::string &entryId, const std::string &username);
    std::list<std::string> getReservedProperties() const;
    std::list<std::string> getPropertiesNames() const;
    int modifyConfig(std::list<std::list<std::string> > &tokens);
    PredefinedView getPredefinedView(const std::string &name);
    int setPredefinedView(const std::string &name, const PredefinedView &pv);
    int deletePredefinedView(const std::string &name);
    PredefinedView getDefaultView();
    static int createProject(const char *repositoryPath, const char *projectName);

private:
    void consolidateIssue(Issue *i);
    int storeViewsToFile();

    ProjectConfig config;
    std::map<std::string, Issue*> issues;
    Locker locker;
    Locker lockerForConfig;
    std::string name; //< name of the project, plain text
    std::string path; //< path to the project, in which the basename is the urlencoded name
    int maxIssueId;
};


class Database {
public:
    static Database Db;
    std::map<std::string, Project*> projects;
    static Project *getProject(const std::string &projectName);
    std::string pathToRepository;
    inline static std::string getRootDir() { return Db.pathToRepository; }
    static std::list<std::string> getProjects();
};


// Functions
int dbLoad(const char * pathToRepository); // initialize the given repository
std::list<std::pair<bool, std::string> > parseSortingSpec(const char *sortingSpec);


#endif
