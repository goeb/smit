#ifndef _db_h
#define _db_h

#include <string>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <stdint.h>

#include "utils/ustring.h"
#include "utils/mutexTools.h"
#include "utils/stringTools.h"
#include "project/Project.h"

#define PATH_REPO ".smit"
#define P_TEMPLATES ".templates"
#define PATH_REPO_TEMPLATES "/public/" P_TEMPLATES

class ProjectSummary {
public:
    std::string name;
    std::string triggerCmdline;
    std::string myRole;
    size_t nIssues;
    long lastModified;

    ProjectSummary() : nIssues(0), lastModified(-1) {}
};

class Database {
public:
    static Database Db;
    Database() : maxIssueId(0),
        editDelay(10*60), // default 10 minutes
        sessionDuration(60*60*36) // default 1.5 days
        {}
    static Project *lookupProject(std::string &resource);
    static void lookupProjectsWildcard(std::string &resource, const std::list<std::string> &projects,
                                std::list<Project *> &result);
    static Project *getProject(const std::string &projectName);
    std::string pathToRepository; // path, possibly local
    std::string absolutePath; // absolute full path
    inline static std::string getRootDir() { return Db.pathToRepository; }
    inline static std::string getRootDirAbsolute() { return Db.absolutePath; }
    static std::list<std::string> getProjects();
    static Project *loadProject(const std::string &path); // load a project
    static Project *createProject(const std::string &projectName, const std::string &author);
    static std::string allocateNewIssueId(const std::string &realm);
    static void updateMaxIssueId(const std::string &realm, uint32_t i);
    inline static uint32_t getMaxIssueId() { return Db.maxIssueId; }
    inline size_t getNumProjects() const { return projects.size(); }
    Project *getNextProject(const Project *p) const;
    static int loadProjects(const std::string &path, bool recurse);
    int loadConfig(const std::string &path);
    int reloadConfig();
    static inline int getEditDelay() { return Db.editDelay; }
    static inline int getSessionDuration() { return Db.sessionDuration; }

    static inline Locker &getLocker() { return Db.locker; }

private:
    std::map<std::string, Project*> projects;
    Locker locker;
    uint32_t maxIssueId;
    std::map<std::string, uint32_t> allocatedIds;
    int editDelay; //< delay after which a message cannot be amended (seconds)
    int sessionDuration; //< duration of a user session (seconds)
};


// Functions
int dbLoad(const char * path); // initialize the given repository


#endif
