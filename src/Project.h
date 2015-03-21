#ifndef _Project_h
#define _Project_h

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <stdint.h>

#include "ustring.h"
#include "mutexTools.h"
#include "stringTools.h"
#include "View.h"
#include "Issue.h"

#define PROJECT_FILE "project"
#define PATH_ISSUES "refs/issues" // sub-directory of a project where the entries are stored
#define VIEWS_FILE "views"
#define PATH_TAGS "refs/tags"
#define PATH_OBJECTS "objects"

#define K_MESSAGE "+message" // keyword used for the message
#define K_FILE "+file" // keyword used for uploaded files
#define K_SUMMARY "summary"
#define K_AMEND "+amend"

#define K_PROJECT_TMP "tmp"

#define DELETE_DELAY_S (10*60) // seconds


struct TagSpec {
    TagSpec(): display(false) {}
    std::string id;
    std::string label; // UTF-8 text
    bool display; // status should be displayed in issue header
};

int strToPropertyType(const std::string &s, PropertyType &out);
std::list<std::pair<bool, std::string> > parseSortingSpec(const char *sortingSpec);

struct PropertySpec {
    std::string name;
    std::string label;
    enum PropertyType type;
    std::list<std::string> selectOptions; // for F_SELECT and F_MULTISELECT only
    std::string reverseLabel; // for F_RELATIONSHIP
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
    static bool isValidProjectName(const std::string &name);
};

class Project {
public:
    static Project *init(const char *path); // init and load a project

    // methods for handling issues
    std::vector<const Issue*> search(const char *fulltextSearch,
                               const std::map<std::string, std::list<std::string> > &filterIn,
                               const std::map<std::string, std::list<std::string> > &filterOut,
                               const char *sortingSpec) const;
    int get(const std::string &issueId, Issue &issue) const;
    Issue *createNewIssue();
    std::string allocateNewIssueId();
    void updateMaxIssueId(uint32_t i);
    std::map<std::string, std::set<std::string> > getReverseAssociations(const std::string &issue) const;
    int storeRefIssue(const std::string &issueId, const std::string &entryId);
    std::string renameIssue(const std::string &oldId);
    int renameIssue(Issue &i, const std::string &newId);
    Issue *getNextIssue(Issue *i);
    int addNewIssue(Issue &i);

    // methods for handling entries
    int storeEntry(const Entry *e);
    int addEntry(PropertiesMap properties, std::string &iid, std::string &eid, std::string username);
    int addNewEntry(Entry *e);
    int pushEntry(std::string &issueId, const std::string &entryId,
                  const std::string &user, const std::string &tmpPath);
    Entry *getEntry(const std::string &id) const;

    int deleteEntry(const std::string &entryId, const std::string &username);
    int getNumIssues() const;

    // methods for handling project
    inline std::string getName() const { return name; }
    inline std::string getUrlName() const { return urlNameEncode(name); }
    inline static std::string urlNameEncode(const std::string &name) { return urlEncode(name, '=', "._-"); }
    inline static std::string urlNameDecode(const std::string &name) { return urlDecode(name, false, '='); }
    inline std::string getPath() const { return path; }
    inline std::string getTmpDir() const { return path + "/" K_PROJECT_TMP; }
    ProjectConfig getConfig() const;
    inline void setConfig(ProjectConfig pconfig) { config = pconfig; }
    int modifyConfig(std::list<std::list<std::string> > &tokens);
    int createProject(std::string name, std::list<std::list<std::string> > &tokens);
    static int createProjectFiles(const char *repositoryPath, const char *projectName, std::string &resultingPath);
    int reload(); // reload a project from disk storage

    // methods for database access
    inline std::string getObjectsDir() const { return path + '/' + PATH_OBJECTS; }
    inline std::string getIssuesDir() const { return path + '/' + PATH_ISSUES; }

    // methods for handling attached files
    int addFile(const std::string &objectId);

    // methods for handling views
    PredefinedView getPredefinedView(const std::string &name);
    int setPredefinedView(const std::string &name, const PredefinedView &pv);
    int deletePredefinedView(const std::string &name);
    PredefinedView getDefaultView() const;

    // methods for handling tags
    int toggleTag(const std::string &issueId, const std::string &entryId, const std::string &tagid);

private:
    int load(); // load a project: config, views, entries, tags
    int loadConfig();
    int loadIssues();
    void loadPredefinedViews();
    void loadTags();
    void computeAssociations();

    std::map<std::string, Entry*> entries;

    void cleanupMultiselect(std::list<std::string> &values, const std::list<std::string> &selectOptions);

    int storeViewsToFile();
    Issue *getIssue(const std::string &id) const;
    int insertEntryInTable(Entry *e);
    int insertIssueInTable(Issue *i);

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



#endif
