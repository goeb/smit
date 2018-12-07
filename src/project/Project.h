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
#define PATH_ENTRIES_DB ".git" // considered as bare git repository
#define PATH_TEMPLATES   P_TEMPLATES
#define PATH_PROJECT_CONFIG PATH_SMIP "/config"
#define PATH_VIEWS          PATH_SMIP "/views"
#define PATH_TRIGGER        PATH_SMIP "/trigger"

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
    Project(const std::string &pathToDir);
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

    // methods for handling entries
    int addEntry(PropertiesMap properties, const std::list<AttachedFileRef> &files, std::string &iid,
                 Entry *&entry, std::string username, IssueCopy &oldIssue);

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
    inline std::string getPathEntries() const { return path + "/" PATH_ENTRIES_DB; }

    // project config
    ProjectConfig getConfig() const;
    ProjectParameters getProjectParameters() const;

    inline void setConfig(const ProjectConfig &pconfig) { config = pconfig; }
    int modifyConfig(std::list<std::list<std::string> > &tokens, const std::string &author);
    int modifyConfig(ProjectConfig newConfig, const std::string &author);
    static int createProjectFiles(const std::string &repositoryPath, const std::string &projectName,
                                  std::string &resultingPath, const std::string &author);
    int reload(); // reload a project from disk storage

    // methods for handling attached files
    std::string storeFile(const char *data, size_t len) const;

    // methods for handling views
    PredefinedView getPredefinedView(const std::string &name);
    int setPredefinedView(const std::string &name, const PredefinedView &pv, const std::string &author);
    int deletePredefinedView(const std::string &name, const std::string &author);
    PredefinedView getDefaultView() const;

    // methods for handling tags
    int toggleTag(const std::string &entryId, const std::string &tagname, const std::string &author);

    std::string getTriggerCmdline() const;

    Project() : maxIssueId(0) {}

    inline Locker &getLocker() { return locker; }

private:
    // private member variables
    std::string name; //< name of the project, plain text, UTF-8 encoded
    std::string path; //< path to the project, in which the basename is the urlencoded name
    uint32_t maxIssueId;
    std::map<std::string, Entry*> entries;
    std::map<std::string, Issue*> issues;
    ProjectConfig config;

    mutable Locker locker; // mutex for issues, entries, config, views

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
    int addNewEntry(const std::string &issueId, Entry *e);
    int load(); // load a project: config, views, entries, tags
    int loadConfig();
    Issue *loadIssue(const std::string &issueId);
    int loadIssues();
    void loadPredefinedViews();
    void computeAssociations();
    void cleanupMultiselect(std::list<std::string> &values,
                            const std::list<std::string> &selectOptions);
    int storeViewsToFile(const std::string &author);
    Issue *getIssue(const std::string &id) const;
    int insertEntryInTable(Entry *e);
    int insertIssueInTable(Issue *i);
    void updateAssociations(const Issue *i, const std::string &associationName,
                            const std::list<std::string> &issues);

    void updateLastModified(Entry *e);
    IssueCopy copyIssue(const Issue &issue) const;
    Entry *getEntry(const std::string &id) const;

    void consolidateAssociations(IssueCopy &issue, bool forward) const;

    int setPredefinedView(std::map<std::string, PredefinedView> views, const std::string &author);
};

#endif
