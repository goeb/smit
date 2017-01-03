#ifndef _Project_h
#define _Project_h

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <stdint.h>

#include "utils/ustring.h"
#include "utils/mutexTools.h"
#include "utils/stringTools.h"
#include "View.h"
#include "Issue.h"
#include "ProjectConfig.h"

#define PATH_SMIP ".smip"
#define PATH_REFS        PATH_SMIP "/refs"
#define PATH_OBJECTS     PATH_SMIP "/objects"
#define PATH_PROJECT_TMP PATH_SMIP "/tmp"
#define PATH_TEMPLATES   PATH_SMIP "/" P_TEMPLATES
#define PATH_ISSUES         PATH_REFS "/issues" // sub-directory of a project where the entries are stored
#define PATH_PROJECT_CONFIG PATH_REFS "/project"
#define PATH_VIEWS          PATH_REFS "/views"
#define PATH_TAGS           PATH_REFS "/tags"
#define PATH_TRIGGER        PATH_REFS "/trigger"

#define DELETE_DELAY_S (10*60) // seconds

/** Class for holding project config and some other info
  */
struct ProjectParameters {
    std::string projectPath;
    std::string projectName;
    ProjectConfig pconfig;
    std::map<std::string, PredefinedView> views;
};

class Project {
public:
    static Project *init(const std::string &path, const std::string &repo);
    static bool isProject(const std::string &path);
    static bool containsReservedName(std::string name);
    static bool isReservedName(const std::string &name);

    // methods for handling issues
    void search(const char *fulltextSearch,
                const std::map<std::string, std::list<std::string> > &filterIn,
                const std::map<std::string, std::list<std::string> > &filterOut,
                const char *sortingSpec,
                std::vector<IssueCopy> &returnedIssues) const;
    void searchEntries(const char *sortingSpec, std::vector<Entry> &entries, int limit) const;

    int get(const std::string &issueId, IssueCopy &issue) const;
    void getAllIssues(std::vector<Issue*> &issuesList);
    int storeRefIssue(const std::string &issueId, const std::string &entryId);
    void consolidateAssociations(IssueCopy &issue, bool forward) const;

    // methods for handling entries
    int addEntry(PropertiesMap properties, std::string &iid,
                 Entry *&entry, std::string username, IssueCopy &oldIssue);
    int pushEntry(std::string &issueId, const std::string &entryId,
                  const std::string &user, const std::string &tmpPath);

    int amendEntry(const std::string &entryId, const std::string &msg,
                   Entry *&entryOut, const std::string &username, IssueCopy &oldIssue);

    size_t getNumIssues() const;
    long getLastModified() const;

    // methods for handling project
    inline std::string getName() const { return name; }
    inline std::string getUrlName() const { return urlNameEncode(name); }
    inline static std::string urlNameEncode(const std::string &name) { return urlEncode(name, '%', "/"); }
    inline static std::string urlNameDecode(const std::string &name) { return urlDecode(name, false, '%'); }
    inline std::string getPath() const { return path; }
    inline std::string getTmpDir() const { return path + "/" PATH_PROJECT_TMP; }

    // project config
    ProjectConfig getConfig() const;
    ProjectParameters getProjectParameters() const;

    inline void setConfig(const ProjectConfig &pconfig) { config = pconfig; }
    int modifyConfig(std::list<std::list<std::string> > &tokens, const std::string &author);
    int modifyConfig(ProjectConfig newConfig, const std::string &author);
    static int createProjectFiles(const std::string &repositoryPath, const std::string &projectName,
                                  std::string &resultingPath);
    int reload(); // reload a project from disk storage

    // methods for database access
    inline std::string getObjectsDir() const { return path + '/' + PATH_OBJECTS; }
    inline static std::string getObjectPath(const std::string path, const std::string &oid) {
        return path + '/' + PATH_OBJECTS + '/' + Object::getSubpath(oid);
    }
    inline std::string getIssuesDir() const { return path + '/' + PATH_ISSUES; }
    void getObjects(std::list<std::string> &objects) const;

    // methods for handling attached files
    int addFile(const std::string &objectId);

    // methods for handling views
    PredefinedView getPredefinedView(const std::string &name);
    int setPredefinedView(const std::string &name, const PredefinedView &pv);
    int deletePredefinedView(const std::string &name);
    PredefinedView getDefaultView() const;

    // methods for handling tags
    int toggleTag(const std::string &entryId, const std::string &tagname, const std::string &author);

    // local usage methods (not mutex-protected)
    Entry *getEntry(const std::string &id) const;
    int addNewIssue(Issue &i);
    std::string renameIssue(const std::string &oldId);
    int renameIssue(Issue &i, const std::string &newId);
    int storeEntry(const Entry *e);

    std::string getTriggerCmdline() const;

    Project() : maxIssueId(0) {}

private:
    // private member variables
    std::string name; //< name of the project, plain text, UTF-8 encoded
    std::string path; //< path to the project, in which the basename is the urlencoded name
    uint32_t maxIssueId;
    std::map<std::string, Entry*> entries;
    std::map<std::string, Issue*> issues;
    ProjectConfig config;
    mutable Locker locker; // mutex for issues and entries
    mutable Locker lockerForConfig; // mutext for config
    mutable Locker lockerForViews; // mutext for views

    // associations table
    // { issue : { association-name : [other-issues] } }
    std::map<IssueId, std::map<AssociationId, std::set<IssueId> > > associations;

    // reverse associations table
    std::map<IssueId, std::map<AssociationId, std::set<IssueId> > > reverseAssociations;

    // views
    std::map<std::string, PredefinedView> predefinedViews;

    // tags
    std::string latestTagId;

    long lastModified; // date of latest entry

    static const char *reservedNames[];

    // private member methods
    Issue *createNewIssue();
    std::string allocateNewIssueId();
    void updateMaxIssueId(uint32_t i);
    int addNewEntry(Entry *e);
    int load(); // load a project: config, views, entries, tags
    int loadConfig();
    int loadIssues();
    void loadPredefinedViews();
    void loadTags();
    void computeAssociations();
    void cleanupMultiselect(std::list<std::string> &values,
                            const std::list<std::string> &selectOptions);
    int storeViewsToFile();
    Issue *getIssue(const std::string &id) const;
    int insertEntryInTable(Entry *e);
    int insertIssueInTable(Issue *i);
    void updateAssociations(const Issue *i, const std::string &associationName,
                            const std::list<std::string> &issues);

    void updateLastModified(Entry *e);
    IssueCopy copyIssue(const Issue &issue) const;

};

#endif
