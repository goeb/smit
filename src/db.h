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
#include "Project.h"


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


#endif
