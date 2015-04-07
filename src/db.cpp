/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License v2 as published by
 *   the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
#include "config.h"

#include <string>
#include <sstream>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <algorithm>
#include <sys/types.h>
#include <unistd.h>

#include "db.h"
#include "logging.h"
#include "identifiers.h"
#include "global.h"
#include "stringTools.h"
#include "mg_win32.h"
#include "filesystem.h"
#include "Project.h"


// global var Db
Database Database::Db;

int dbLoad(const char * path)
{
    Database::Db.pathToRepository = path;
    int n = Database::Db.loadProjects(path, true);
    return n;
}

/** Recursively load all projects under the given path
  *
  * @return
  *    number of projects found
  */
int Database::loadProjects(const std::string &path, bool recurse)
{
    LOG_DEBUG("loadProjects(%s)", path.c_str());

    int result = 0;

    if (Project::isProject(path)) {
        Project *p = Database::loadProject(path);
        if (p) result += 1;
    }

    if (recurse) {
        DIR *dirp;
        if ((dirp = openDir(path)) == NULL) {
            return 0;

        } else {
            std::string f;
            while ((f = getNextFile(dirp)) != "") {
                std::string subpath = path + "/" + f;
                if (isDir(subpath)) result += loadProjects(subpath, recurse); // recurse
            }
            closedir(dirp);
        }
    }

    return result;
}


Project *Database::createProject(const std::string &name)
{
    std::string resultingPath;
    int r = Project::createProjectFiles(Db.getRootDir().c_str(), name.c_str(), resultingPath);
    if (r != 0) return 0;

    Project *p = loadProject(resultingPath.c_str());
    return p;
}

Project *Database::loadProject(const std::string &path)
{
    Project *p = Project::init(path);
    if (!p) return 0;

    {
        ScopeLocker scopeLocker(Db.locker, LOCK_READ_WRITE);
        // store the project in memory
        Db.projects[p->getName()] = p;
    }
    return p;
}


Project *Database::getProject(const std::string & projectName)
{
    ScopeLocker scopeLocker(Db.locker, LOCK_READ_ONLY);

    std::map<std::string, Project*>::iterator p = Database::Db.projects.find(projectName);
    if (p == Database::Db.projects.end()) return 0;
    else return p->second;
}

std::list<std::string> Database::getProjects()
{
    ScopeLocker scopeLocker(Db.locker, LOCK_READ_ONLY);
    std::list<std::string> result;
    std::map<std::string, Project*>::iterator p;
    FOREACH(p, Database::Db.projects) {
        result.push_back(p->first);
    }

    return result;
}


std::string Database::allocateNewIssueId()
{
    ScopeLocker scopeLocker(Db.locker, LOCK_READ_WRITE);

    Db.maxIssueId++;
    if (Db.maxIssueId == 0) LOG_ERROR("Database: max issue id zero: wrapped");
    LOG_DEBUG("allocateNewIssueId: %u", Db.maxIssueId);

    const int SIZ = 25;
    char buffer[SIZ];
    snprintf(buffer, SIZ, "%u", Db.maxIssueId);

    return std::string(buffer);
}

void Database::updateMaxIssueId(uint32_t i)
{
    if (i > Db.maxIssueId) Db.maxIssueId = i;
}

Project *Database::getNextProject(const Project *p) const
{
    std::map<std::string, Project*>::const_iterator pit;

    if (!p) {
        // get the first item
        pit = projects.begin();

    } else {
        pit = projects.find(p->getName());
        if (pit == projects.end()) return 0;
        pit++; // get next
    }

    if (pit == projects.end()) return 0;
    else return pit->second;
}


