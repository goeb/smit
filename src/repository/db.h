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
#define P_TEMPLATES "templates"
#define PATH_REPO_TEMPLATES PATH_REPO "/" P_TEMPLATES

class ProjectSummary {
public:
    std::string name;
    std::string myRole;
    size_t nIssues;
    long lastModified;

    ProjectSummary() : nIssues(0), lastModified(-1) {}
};

class Database {
public:
    static Database Db;
    Database() : maxIssueId(0) {}
    static Project *lookupProject(std::string &resource);
    static void lookupProjectsWildcard(std::string &resource, const std::list<std::string> &projects,
                                std::list<Project *> &result);
    static Project *getProject(const std::string &projectName);
    std::string pathToRepository;
    inline static std::string getRootDir() { return Db.pathToRepository; }
    static std::list<std::string> getProjects();
    static Project *loadProject(const std::string &path); // load a project
    static Project *createProject(const std::string &projectName);
    static std::string allocateNewIssueId(const std::string &realm);
    static void updateMaxIssueId(const std::string &realm, uint32_t i);
    inline static uint32_t getMaxIssueId() { return Db.maxIssueId; }
    inline size_t getNumProjects() const { return projects.size(); }
    Project *getNextProject(const Project *p) const;
    static int loadProjects(const std::string &path, bool recurse);

private:
    std::map<std::string, Project*> projects;
    Locker locker;
    uint32_t maxIssueId;
    std::map<std::string, uint32_t> allocatedIds;
};


// Functions
int dbLoad(const char * path); // initialize the given repository


#endif
