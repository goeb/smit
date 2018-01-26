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
#include "utils/parseConfig.h"
#include "utils/logging.h"
#include "utils/identifiers.h"
#include "utils/stringTools.h"
#include "utils/filesystem.h"
#include "global.h"
#include "mg_win32.h"
#include "project/Project.h"
#include "fnmatch.h"


#define REPO_CONFIG "config"

// global var Db
Database Database::Db;

int dbLoad(const char *path)
{
    Database::Db.pathToRepository = path;
    Database::Db.absolutePath = getAbsolutePath(path);
    int ret = Database::Db.loadConfig(path);
    int n = Database::Db.loadProjects(path, true);
    return n;
}

/** Load configuration of a repository
 *
 */
int Database::loadConfig(const std::string &path)
{
    int err = 0;
    std::string buf;
    std::string configPath = path + "/" PATH_REPO "/" REPO_CONFIG;
    int n = loadFile(configPath.c_str(), buf);

    if (n < 0) {
        // error loading the file
        LOG_INFO("Cannot load configuration of repository '%s': %s", configPath.c_str(), strerror(errno));
        err = -1;

    } else {
        // file successfully loaded into 'buf'
        std::list<std::list<std::string> > lines = parseConfigTokens(buf.c_str(), buf.size());

        std::list<std::list<std::string> >::iterator line;
        FOREACH (line, lines) {
            // editDelay
            // sessionDuration
            if (line->size() != 2) continue;
            std::string key = line->front();
            std::string value = line->back();
            if (key == "editDelay") editDelay = atoi(value.c_str());
            else if (key == "sessionDuration") sessionDuration = atoi(value.c_str());
            else {
                LOG_ERROR("Invalid key in configuration of repository: %s", key.c_str());
            }
        }
    }
    LOG_INFO("Repository config: editDelay=%ds, sessionDuration=%ds", editDelay, sessionDuration);
    return err;
}

int Database::reloadConfig()
{
    return loadConfig(pathToRepository);
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
                if (f[0] == '.') continue; // do not look through hidden files
                std::string subpath = path + "/" + f;
                result += loadProjects(subpath, recurse); // recurse
            }
            closedir(dirp);
        }
    }

    return result;
}


Project *Database::createProject(const std::string &name, const std::string &author)
{
    std::string resultingPath;
    LOG_DIAG("createProject %s", name.c_str());
    int r = Project::createProjectFiles(Db.getRootDir().c_str(), name.c_str(), resultingPath, author);
    if (r != 0) return 0;

    Project *p = loadProject(resultingPath.c_str());
    return p;
}

Project *Database::loadProject(const std::string &path)
{
    Project *p = Project::init(path, Db.pathToRepository);
    if (!p) return 0;

    {
        ScopeLocker scopeLocker(Db.locker, LOCK_READ_WRITE);
        // store the project in memory
        Db.projects[p->getName()] = p;
    }
    return p;
}

/** Look a project up after a uri resource
  *
  * @param resource[out]
  *    The part of the resource that indicates the project name
  *    is consumed by the method.
  *
  * When the URI is like 'a/b/c/issues/'
  *   (in parenthesis an example of response)
  *   - first look if 'a' is a known project (yes)
  *   - then 'a/b'                           (no)
  *   - then 'a/b/c'                         (yes)
  *   - then 'a/b/c/issues/'                 (no)
  * In this example, the project found is 'a/b/c'.
  */
Project *Database::lookupProject(std::string &resource)
{
    ScopeLocker scopeLocker(Db.locker, LOCK_READ_ONLY);

    std::string localResource = resource;
    std::string projectUrl;
    Project *foundProject = 0;

    while (!localResource.empty()) {
        projectUrl += popToken(localResource, '/');
        std::string projectName = Project::urlNameDecode(projectUrl);

        std::map<std::string, Project*>::iterator p = Database::Db.projects.find(projectName);
        if (p == Database::Db.projects.end()) {
            // not found
        } else {
            foundProject = p->second;
            resource = localResource;
        }

        projectUrl += "/"; // prepare for next iteration
    }

    return foundProject;
}
/** Look a project up after a wildcard uri resource
  *
  * @param resource[out]
  *    The part of the resource that indicates the project wildcard
  *    is consumed by the method.
  *
  * When the URI is like 'a*x/b/c/issues/'
  *   (in parenthesis an example of response)
  *   - first look if 'a*x' matches a known project (yes)
  *   - then 'a*x/b'                                (no)
  *   - then 'a*x/b/c'                              (yes)
  *   - then 'a*x/b/c/issues/'                      (no)
  * In this example, the project found is 'a*x/b/c'.
  */

void Database::lookupProjectsWildcard(std::string &resource, const std::list<std::string> &projects,
                                      std::list<Project *> &result)
{
    ScopeLocker scopeLocker(Db.locker, LOCK_READ_ONLY);

    std::string localResource = resource;
    std::string projectUrl;

    while (!localResource.empty()) {
        projectUrl += popToken(localResource, '/');
        std::string projectWildcard = Project::urlNameDecode(projectUrl);

        std::list<Project *> tmpResult;

        std::list<std::string>::const_iterator pName;
        FOREACH(pName, projects) {
            if (0 == fnmatch(projectWildcard.c_str(), pName->c_str(), 0)) {
                // match
                std::map<std::string, Project*>::iterator p = Database::Db.projects.find(*pName);

                if (p == Database::Db.projects.end()) {
                    // should not happen, as the list of project should be valid
                    LOG_ERROR("Unexpected invalid project name: %s", pName->c_str());
                    continue;
                }

                tmpResult.push_back(p->second);
                resource = localResource; // update resource for return, the smallest possible
            }
        }

        if (tmpResult.size() > 0) result = tmpResult; // take the longest matching URI (the latest possible)

        projectUrl += "/"; // prepare for next iteration
    }
}


Project *Database::getProject(const std::string & projectName)
{
    ScopeLocker scopeLocker(Db.locker, LOCK_READ_ONLY);

    std::map<std::string, Project*>::iterator p = Database::Db.projects.find(projectName);
    if (p == Database::Db.projects.end()) return 0;
    else return p->second;
}

/** Get all the projects of the repository
  */
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


std::string Database::allocateNewIssueId(const std::string &realm)
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

void Database::updateMaxIssueId(const std::string &realm, uint32_t i)
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


