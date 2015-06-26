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
#include "ProjectConfig.h"

#define PATH_SMIP ".smip"
#define PATH_REFS        PATH_SMIP "/refs"
#define PATH_OBJECTS     PATH_SMIP "/objects"
#define PATH_PROJECT_TMP PATH_SMIP "/tmp"
#define PATH_TEMPLATES   PATH_SMIP "/templates"
#define PATH_ISSUES         PATH_REFS "/issues" // sub-directory of a project where the entries are stored
#define PATH_PROJECT_CONFIG PATH_REFS "/project"
#define PATH_VIEWS          PATH_REFS "/views"
#define PATH_TAGS           PATH_REFS "/tags"
#define PATH_TRIGGER        PATH_REFS "/trigger"



#define DELETE_DELAY_S (10*60) // seconds



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
                std::vector<Issue> &returnedIssues) const;
    int get(const std::string &issueId, Issue &issue) const;
    void getAllIssues(std::vector<Issue*> &issuesList);
    std::map<std::string, std::set<std::string> > getReverseAssociations(const std::string &issue) const;
    int storeRefIssue(const std::string &issueId, const std::string &entryId);

    // methods for handling entries
    int addEntry(PropertiesMap properties, std::string &iid, Entry *&entry, std::string username);
    int pushEntry(std::string &issueId, const std::string &entryId,
                  const std::string &user, const std::string &tmpPath);

    Entry *amendEntry(const std::string &entryId, const std::string &username, const std::string &msg);

    int getNumIssues() const;

    // methods for handling project
    inline std::string getName() const { return name; }
    inline std::string getUrlName() const { return urlNameEncode(name); }
    inline static std::string urlNameEncode(const std::string &name) { return urlEncode(name, '%', "/"); }
    inline static std::string urlNameDecode(const std::string &name) { return urlDecode(name, false, '%'); }
    inline std::string getPath() const { return path; }
    inline std::string getTmpDir() const { return path + "/" PATH_PROJECT_TMP; }

    // project config
    ProjectConfig getConfig() const;
    std::map<std::string, PredefinedView> getViews() const;

    inline void setConfig(ProjectConfig pconfig) { config = pconfig; }
    int modifyConfig(std::list<std::list<std::string> > &tokens, const std::string &author);
    int modifyConfig(ProjectConfig newConfig, const std::string &author);
    static int createProjectFiles(const std::string &repositoryPath, const std::string &projectName,
                                  std::string &resultingPath);
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
    int toggleTag(const std::string &entryId, const std::string &tagname, const std::string &author);

    // local usage methods (not mutex-protected)
    Entry *getEntry(const std::string &id) const;
    int addNewIssue(Issue &i);
    std::string renameIssue(const std::string &oldId);
    int renameIssue(Issue &i, const std::string &newId);
    int storeEntry(const Entry *e);


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
    std::map<std::string, std::map<std::string, std::list<std::string> > > associations;

    // reverse associations table
    std::map<std::string, std::map<std::string, std::set<std::string> > > reverseAssociations;

    // views
    std::map<std::string, PredefinedView> predefinedViews;

    // tags
    std::string latestTagId;

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

};



#endif
