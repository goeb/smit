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

#define PROJECT_FILE "project"
#define PATH_ISSUES "refs/issues" // sub-directory of a project where the entries are stored
#define PATH_PROJECT_CONFIG "refs/project"
#define VIEWS_FILE "views"
#define PATH_TAGS "refs/tags"
#define PATH_OBJECTS "objects"
#define PATH_PROJECT "refs/project"

#define K_MESSAGE "+message" // keyword used for the message
#define K_FILE "+file" // keyword used for uploaded files
#define K_SUMMARY "summary"
#define K_AMEND "+amend"


#define K_PROJECT_TMP "tmp"

#define DELETE_DELAY_S (10*60) // seconds



class Project {
public:
    static Project *init(const char *path); // init and load a project

    // methods for handling issues
    std::vector<const Issue*> search(const char *fulltextSearch,
                               const std::map<std::string, std::list<std::string> > &filterIn,
                               const std::map<std::string, std::list<std::string> > &filterOut,
                               const char *sortingSpec) const;
    int get(const std::string &issueId, Issue &issue) const;
    void getAllIssues(std::vector<Issue*> &issuesList);
    std::map<std::string, std::set<std::string> > getReverseAssociations(const std::string &issue) const;
    int storeRefIssue(const std::string &issueId, const std::string &entryId);

    // methods for handling entries
    int addEntry(PropertiesMap properties, const std::string &iid, Entry *&entry, std::string username);
    int pushEntry(std::string &issueId, const std::string &entryId,
                  const std::string &user, const std::string &tmpPath);

    int deleteEntry(const std::string &entryId, const std::string &username);
    int getNumIssues() const;

    // methods for handling project
    inline std::string getName() const { return name; }
    inline std::string getUrlName() const { return urlNameEncode(name); }
    inline static std::string urlNameEncode(const std::string &name) { return urlEncode(name, '=', "._-"); }
    inline static std::string urlNameDecode(const std::string &name) { return urlDecode(name, false, '='); }
    inline std::string getPath() const { return path; }
    inline std::string getTmpDir() const { return path + "/" K_PROJECT_TMP; }

    // project config
    ProjectConfig getConfig() const;
    std::map<std::string, PredefinedView> getViews() const;

    inline void setConfig(ProjectConfig pconfig) { config = pconfig; }
    int modifyConfig(std::list<std::list<std::string> > &tokens, const std::string &author);
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
    int toggleTag(const std::string &entryId, const std::string &tagid);

    // local usage methods (not mutex-protected)
    Entry *getEntry(const std::string &id) const;
    int addNewIssue(Issue &i);
    std::string renameIssue(const std::string &oldId);
    int renameIssue(Issue &i, const std::string &newId);
    int storeEntry(const Entry *e);


private:
    // private member variables
    std::string name; //< name of the project, plain text
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
