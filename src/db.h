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

#define K_MESSAGE "+message" // keyword used for the message
#define K_FILE "+file" // keyword used for uploaded files
#define K_SUMMARY "summary"
#define K_UPLOADED_FILES_DIR "files"
#define K_PROJECT_TMP "tmp"

#define DELETE_DELAY_S (10*60) // seconds

// Data types

// Entry
struct Entry {
    std::string parent; // id of the parent entry, empty if top-level

    /** The id of an entry
      * - must not start by a dot (reserved for hidden file)
      * - must notcontain characters forbidden for file names (Linux and Windows):
      *        <>:"/\|?*
      * - must be unique case insensitively (as HTML identifiers are case insensitive)
      *
      * The ids of entries in the current implementation contain: lower case letters and digits
      */
    std::string id; // unique id of this entry
    long ctime; // creation time
    std::string author;
    std::map<std::string, std::list<std::string> > properties;
    std::string serialize() const;
    int getCtime() const;
    std::string getMessage() const;
    // chainlist pointers
    struct Entry *next; // child
    struct Entry *prev; // parent
    std::set<std::string> tags;
    Entry() : ctime(0), next(0), prev(0) {}
};

// Issue
// An issue is consolidated over all its entries
struct Issue {
    std::string id; // same as the first entry
    std::string path; // path of the directory where the issue is stored
    Entry *latest; // the latest entry
    int ctime; // creation time (the one of the first entry)
    int mtime; // modification time (the one of the last entry)
    std::map<std::string, std::list<std::string> > properties;

    /** { association-name : [related issues] } */
    Issue() : latest(0), ctime(0), mtime(0) {}

    // the properties of the issue is the consolidation of all the properties
    // of its entries. For a given key, the most recent value has priority.
    std::string getSummary() const;
    bool lessThan(const Issue *other, const std::list<std::pair<bool, std::string> > &sortingSpec) const;
    bool isInFilter(const std::map<std::string, std::list<std::string> > &filter) const;
    std::map<std::string, Entry*> entries;
    Entry* mergePending; // null if no merge-pending entry

    int computeLatestEntry();
    void consolidate();
    void consolidateIssueWithSingleEntry(Entry *e, bool overwrite);
    bool searchFullText(const char *text) const;
    int getNumberOfTaggedIEntries(const std::string &tagId) const;
    Entry *getEntry(const std::string id);

    int load(const std::string &issuePath);

};


struct TagSpec {
    TagSpec(): display(false) {}
    std::string id;
    std::string label; // UTF-8 text
    bool display; // status should be displayed in issue header
};

enum PropertyType {
	F_TEXT,
	F_SELECT,
	F_MULTISELECT,
	F_SELECT_USER,
	F_TEXTAREA,
	F_TEXTAREA2,
    F_ASSOCIATION
};
int strToPropertyType(const std::string &s, PropertyType &out);
std::string propertyTypeToStr(PropertyType type);

struct PropertySpec {
    std::string name;
    std::string label;
    enum PropertyType type;
    std::list<std::string> selectOptions; // for F_SELECT and F_MULTISELECT only
    std::string reverseLabel; // for F_RELATIONSHIP
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
    static PredefinedView loadFromQueryString(const std::string &q);
    std::string serialize() const;
    static std::map<std::string, PredefinedView> parsePredefinedViews(std::list<std::list<std::string> > lines);

};

struct ProjectConfig {
    ProjectConfig() : numberIssueAcrossProjects(false) {}

    // properties
    std::list<PropertySpec> properties; // user defined properties
    std::map<std::string, std::string> propertyLabels;
    std::map<std::string, std::string> propertyReverseLabels;
    std::map<std::string, PredefinedView> predefinedViews;
    std::map<std::string, TagSpec> tags;
    bool numberIssueAcrossProjects; // accross project

    // methods
    const PropertySpec *getPropertySpec(const std::string name) const;
    std::list<std::string> getPropertiesNames() const;
    static std::list<std::string> getReservedProperties();
    static bool isReservedProperty(const std::string &name);
    std::string getLabelOfProperty(const std::string &propertyName) const;
    std::string getReverseLabelOfProperty(const std::string &propertyName) const;
    bool isValidPropertyName(const std::string &name) const;
};

class Project {
public:
    static Project *init(const char *path); // init and load a project
    std::vector<const Issue*> search(const char *fulltextSearch,
                               const std::map<std::string, std::list<std::string> > &filterIn,
                               const std::map<std::string, std::list<std::string> > &filterOut,
                               const char *sortingSpec) const;
    int get(const std::string &issueId, Issue &issue) const;
    int addEntry(std::map<std::string, std::list<std::string> > properties, std::string &iid, std::string &eid, std::string username);
    Entry *getEntry(const std::string &id) const;

    inline std::string getName() const { return name; }
    inline std::string getUrlName() const { return urlNameEncode(name); }
    inline static std::string urlNameEncode(const std::string &name) { return urlEncode(name, '=', "._-"); }
    inline static std::string urlNameDecode(const std::string &name) { return urlDecode(name, false, '='); }
    inline std::string getPath() const { return path; }
    inline std::string getTmpDir() const { return path + "/" K_PROJECT_TMP; }
    inline std::string getPathUploadedFiles() const { return path + "/" + K_UPLOADED_FILES_DIR; }
    ProjectConfig getConfig() const;
    inline void setConfig(ProjectConfig pconfig) { config = pconfig; }
    int deleteEntry(const std::string &issueId, const std::string &entryId, const std::string &username);
    int modifyConfig(std::list<std::list<std::string> > &tokens);
    int createProject(std::string name, std::list<std::list<std::string> > &tokens);
    PredefinedView getPredefinedView(const std::string &name);
    int setPredefinedView(const std::string &name, const PredefinedView &pv);
    int deletePredefinedView(const std::string &name);
    PredefinedView getDefaultView() const;
    static int createProjectFiles(const char *repositoryPath, const char *projectName, std::string &resultingPath);
    int toggleTag(const std::string &issueId, const std::string &entryId, const std::string &tagid);
    std::string allocateNewIssueId();
    void updateMaxIssueId(uint32_t i);
    int reload(); // reload a project from disk storage
    int getNumIssues() const;
    std::map<std::string, std::set<std::string> > getReverseAssociations(const std::string &issue) const;
    std::string renameIssue(const std::string &id);

private:
    int load(); // load a project: config, views, entries, tags
    int loadConfig();
    int loadEntries();
    void loadPredefinedViews();
    void loadTags();
    void computeAssociations();

    void cleanupMultiselect(std::list<std::string> &values, const std::list<std::string> &selectOptions);

    int storeViewsToFile();
    Issue *getIssue(const std::string &id) const;

    ProjectConfig config;
    std::map<std::string, Issue*> issues;
    mutable Locker locker; // mutex for issues
    mutable Locker lockerForConfig; // mutext for config
    std::string name; //< name of the project, plain text
    std::string path; //< path to the project, in which the basename is the urlencoded name
    uint32_t maxIssueId;

    // associations table
    // { issue : { association-name : [other-issues] } }
    std::map<std::string, std::map<std::string, std::list<std::string> > > associations;

    // reverse associations table
    //
    std::map<std::string, std::map<std::string, std::set<std::string> > > reverseAssociations;
    void updateAssociations(const Issue *i, const std::string &associationName, const std::list<std::string> &issues);

};


class Database {
public:
    static Database Db;
    Database() : maxIssueId(0) {}
    static Project *getProject(const std::string &projectName);
    std::string pathToRepository;
    inline static std::string getRootDir() { return Db.pathToRepository; }
    static std::list<std::string> getProjects();
    static Project *loadProject(const char *path); // load a project
    static Project *createProject(const std::string &projectName);
    static std::string allocateNewIssueId();
    static void updateMaxIssueId(uint32_t i);
    inline static uint32_t getMaxIssueId() { return Db.maxIssueId; }
    inline size_t getNumProjects() const { return projects.size(); }
    const Project *getNext(const Project *p) const;
private:
    std::map<std::string, Project*> projects;
    Locker locker;
    uint32_t maxIssueId;
};


// Functions
int dbLoad(const char * pathToRepository); // initialize the given repository
std::list<std::pair<bool, std::string> > parseSortingSpec(const char *sortingSpec);


#endif
