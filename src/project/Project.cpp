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
#include <openssl/sha.h>
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

#include "Project.h"
#include "repository/db.h"
#include "utils/parseConfig.h"
#include "utils/filesystem.h"
#include "utils/logging.h"
#include "utils/identifiers.h"
#include "utils/stringTools.h"
#include "utils/gitTools.h"
#include "global.h"
#include "mg_win32.h"
#include "gitdb.h"
#include "Tag.h"

const char *Project::reservedNames[] = {
    "public", // reserved because 'public' is an existing folder
    "views",  // reserved for REST interface
    "issues", // reserved for REST interface
    "files",  // reserved for REST interface
    "reload", // reserved for REST interface
    "tags",   // reserved for REST interface
    "sm",
    "users",
    "config",
    0
};

Project::Project(const std::string &pathToDir): path(pathToDir)
{
    // nothing
}

/** init and load in memory the given project
  *
  * @param path
  *    Full path where the project is stored
  *
  * @param repo
  *     Path of the repository where the project lies, if known.
  *     If empty string, then the project name is the basename of 'path'.
  *     Else, the project name is 'path' - 'repo' (ie: the relative path
  *     from the repository path)
  *
  * @return
  *    A pointer to the newly created project instance
  *    Null pointer if error
  *
  */

Project *Project::init(const std::string &path, const std::string &repo)
{
    Project *p = new Project;
    LOG_DEBUG("Loading project %s (%p)...", path.c_str(), p);

    if (repo.empty()) {
        p->name = path;
    } else {
        // Substract the repository path, that must be starting at index 0
        if (0 == path.compare(0, repo.size(), repo)) {
            if (path.size() <= repo.size()) {
                LOG_ERROR("Cannot compute project name: too short (path=%s, repo=%s)",
                          path.c_str(), repo.c_str());
                delete p;
                return 0;
            }
            p->name = path.substr(repo.size()); // remove the leading repo part
            trimLeft(p->name, "/");
        } else {
            LOG_ERROR("Unexpected project path: path=%s, repo=%s",
                      path.c_str(), repo.c_str());
            p->name = path;
        }
    }
    LOG_DEBUG("Project name: '%s'", p->name.c_str());

    p->path = path;
    p->maxIssueId = 0;
    p->lastModified = -1;

    int r = p->load();
    if (r != 0) {
        delete p;
        p = 0;
    }
    return p;
}

/** Tell if a path looks like a project
  */
bool Project::isProject(const std::string &path)
{
    return fileExists(path + "/" + PATH_PROJECT_CONFIG);
}

/** Tell if a name contains a reserved part
  *
  * For example: 'a/b/issues/c/d' does contain 'issues', that is reserved.
  */
bool Project::containsReservedName(std::string name)
{
    while (!name.empty()) {
        std::string part = popToken(name, '/');
        if (isReservedName(part)) return true;
    }
    return false;
}

bool Project::isReservedName(const std::string &name)
{
    const char **ptr = reservedNames;
    while (*ptr) {
        if (name == *ptr) return true;
        ptr++;
    }

    // Names starting with a dot are reserved
    if (name.size() > 0 && name[0] == '.') return true;

    return false;
}


/** Load a project: configuration, views, entries, tags
  *
  * @return
  *     0 on success, -1 on error.
  */
int Project::load()
{
    int r = loadConfig();
    if (r == -1) {
        LOG_DEBUG("Project '%s' not loaded because of errors while reading the config.", path.c_str());
        return r;
    }

    loadPredefinedViews();

    r = loadIssues();
    if (r == -1) {
        LOG_ERROR("Project '%s' not loaded because of errors while reading the entries.", path.c_str());
        return r;
    }

    LOG_INFO("Project %s loaded: %ld issues", path.c_str(), L(issues.size()));

    computeAssociations();

    return 0;
}

/** computeAssociations
  * For each issue, look if it has some F_ASSOCIATION properties
  * and if so, then update the associations tables
  */
void Project::computeAssociations()
{
    std::map<std::string, Issue*>::iterator i;
    for (i = issues.begin(); i != issues.end(); i++) {
        Issue *currentIssue = i->second;
        std::list<PropertySpec>::const_iterator pspec;

        FOREACH(pspec, config.properties) {
            if (pspec->type == F_ASSOCIATION) {
                std::map<std::string, std::list<std::string> >::const_iterator p;
                p = currentIssue->properties.find(pspec->name);
                if (p != currentIssue->properties.end()) {
                    updateAssociations(currentIssue, pspec->name, p->second);
                }
            }
        }
    }
}

/** Load a single issue
 */
Issue *Project::loadIssue(const std::string &issueId)
{
    GitIssue elist;
    std::string pathEntries = getPathEntries();
    int ret = elist.open(pathEntries, issueId);
    if (ret) {
        LOG_ERROR("Cannot load issue %s (%s)", issueId.c_str(), pathEntries.c_str());
        return 0;
    }

    Issue *issue = new Issue();
    int error = 0;

    while (1) {
        std::string entryString = elist.getNextEntry();
        if (entryString.empty()) break; // reached the end

        std::string treeid;
        std::list<std::string> tags;
        Entry *e = Entry::loadEntry(entryString, treeid, tags);
        if (!e) {
            LOG_ERROR("Cannot load entry '%s'", entryString.c_str());
            error = 1;
            break; // abort the loading of this issue
        }

        if (!treeid.empty() && treeid != K_EMPTY_TREE) {
            int err = gitdbLsTree(pathEntries, treeid, e->files);
            if (err) {
                LOG_ERROR("Cannot load attached files: tree %s", treeid.c_str());
                error = 1;
                break; // abort the loading of this issue
            }
        }

        issue->insertEntry(e); // store the entry in the chain list

        // set the tags
        std::list<std::string>::const_iterator tagname;
        FOREACH(tagname, tags) {
            issue->addTag(e->id, *tagname);
        }
    }

    elist.close();

    if (error) {
        delete issue;
        issue = 0;

    } else {
        issue->consolidate();
    }

    return issue;
}

int Project::loadIssues()
{
    LOG_DEBUG("Loading issues (%s)", getPathEntries().c_str());
    GitIssueList ilist;
    int ret = ilist.open(getPathEntries());
    if (ret) {
        LOG_ERROR("Cannot load issues of project (%s)", getPathEntries().c_str());
        return -1;
    }

    int localMaxId = 0;

    while (1) {
        std::string issueId = ilist.getNext();
        if (issueId.empty()) break; // reached the end

        Issue *issue = loadIssue(issueId);

        if (!issue) {
            LOG_ERROR("Cannot load issue %s", issueId.c_str());
            continue;
        }

        issue->project = getName();

        // update lastModified
        updateLastModified(issue->mtime);

        // update the maximum id
        int intId = atoi(issueId.c_str());
        if (intId > 0 && intId > localMaxId) localMaxId = intId;

        // store the issue in memory
        issue->id = issueId;
        insertIssueInTable(issue);
    }

    ilist.close();

    updateMaxIssueId(localMaxId);

    return 0;
}

Issue *Project::getIssue(const std::string &id) const
{
    std::map<std::string, Issue*>::const_iterator i;
    i = issues.find(id);
    if (i == issues.end()) return 0;
    else return i->second;
}

/** Consolidate the copy of the issue with the associations
  */
void Project::consolidateAssociations(IssueCopy &issue, bool forward) const
{
    std::map<IssueId, std::map<AssociationId, std::set<IssueId> > >::const_iterator ait;
    std::map<AssociationId, std::set<IssueSummary> > *associationTable;
    if (forward) {
        // Forward Associations
        ait = associations.find(issue.id);
        if (ait == associations.end()) return;
        associationTable = &issue.associations;
    } else {
        // reverse
        ait = reverseAssociations.find(issue.id);
        if (ait == reverseAssociations.end()) return;
        associationTable = &issue.reverseAssociations;
    }

    std::map<AssociationId, std::set<IssueId> >::const_iterator a;
    FOREACH(a, ait->second) {
        AssociationId associationName = a->first;
        const std::set<IssueId> &otherIssues = a->second;
        std::set<IssueId>::const_iterator otherIssue;
        FOREACH(otherIssue, otherIssues) {
            Issue *oi = getIssue(*otherIssue);
            if (!oi) continue; // a bad issue id was fulfilled by a user
            IssueSummary is;
            is.id = oi->id;
            is.summary = oi->getSummary();
            (*associationTable)[associationName].insert(is);
        }
    }
}

/** Return a given issue
  *
  * @param[out] issue
  *     The returned issue is a copy, and can thus be used without threading conflict.
  */
int Project::get(const std::string &issueId, IssueCopy &issue) const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);

    Issue *i = getIssue(issueId);
    if (!i) {
        // issue not found
        LOG_DEBUG("Issue not found: %s", issueId.c_str());
        return -1;
    }

    issue = copyIssue(*i); // make a copy

    return 0;
}

IssueCopy Project::copyIssue(const Issue &issue) const
{
    IssueCopy copy = issue; // make a copy
    consolidateAssociations(copy, true);
    consolidateAssociations(copy, false);
    return copy;
}

// @return 0 if OK, -1 on error
int Project::loadConfig()
{
    LOG_FUNC();
    std::string pathToProjectConfig = path + "/" PATH_PROJECT_CONFIG;
    int err = ProjectConfig::load(pathToProjectConfig, config);
    return err;
}

int Project::modifyConfig(std::list<std::list<std::string> > &tokens, const std::string &author)
{
    LOG_FUNC();
    // verify the syntax of the tokens
    ProjectConfig c = ProjectConfig::parseProjectConfig(tokens);

    return modifyConfig(c, author);
}

int Project::modifyConfig(ProjectConfig newConfig, const std::string &author)
{
    LOG_FUNC();
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);

    // write to file
    std::string data = newConfig.serialize();

    int err = gitdbCommitMaster(path, PATH_PROJECT_CONFIG, data, author);
    if (err) return -1;

    config = newConfig;

    return 0;
}

/** get a predefined view
  * @return
  * if not found an object PredefinedView with empty name is returned
  */
PredefinedView Project::getPredefinedView(const std::string &name)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);

    std::map<std::string, PredefinedView>::const_iterator pv;
    pv = predefinedViews.find(name);
    if (pv == predefinedViews.end()) return PredefinedView();
    else return pv->second;
}

/** create a new or modify an existing predefined view
  * @param name
  *     empty or '_' if creating a new view
  *     non-empty if modifying or renaming an existing view
  * @return
  *     <0 error
  * When creating a new view, setting the name of an existing view is forbidden.
  */
int Project::setPredefinedView(const std::string &name, const PredefinedView &pv, const std::string &author)
{
    LOG_DEBUG("setPredefinedView: %s -> %s", name.c_str(), pv.name.c_str());
    if (pv.name.empty()) return -3;
    if (pv.name == "_") return -2; // '_' is reserved

    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);

    if (name == "_" && predefinedViews.count(pv.name)) {
        // reject creating a new view with the name of an existing view
        return -1;
    }

    // update default view
    if (pv.isDefault) {
        // set all others to non-default
        std::map<std::string, PredefinedView>::iterator i;
        FOREACH(i, predefinedViews) {
            i->second.isDefault = false;
        }
    }

    predefinedViews[pv.name] = pv;

    if (!name.empty() && name != pv.name && name != "_") {
        // it was a rename. remove old name
        std::map<std::string, PredefinedView>::iterator i = predefinedViews.find(name);
        if (i != predefinedViews.end()) predefinedViews.erase(i);
        else LOG_ERROR("Cannot remove old name of renamed view: %s -> %s", name.c_str(), pv.name.c_str());
    }

    // store to file
    return storeViewsToFile(author);
}

int Project::setPredefinedView(std::map<std::string, PredefinedView> views, const std::string &author)
{
    predefinedViews = views;
    return storeViewsToFile(author);
}

/**
  * No mutex handled in here.
  * Must be called from a mutexed scope (lockerForViews)
  */
int Project::storeViewsToFile(const std::string &author)
{
    std::string fileContents;
    fileContents = K_SMIT_VERSION " " VERSION "\n";

    fileContents += PredefinedView::serializeViews(predefinedViews);

    int err = gitdbCommitMaster(path, PATH_VIEWS, fileContents, author);
    return err;
}

int Project::deletePredefinedView(const std::string &name, const std::string &author)
{
    if (name.empty()) return -1;
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);

    std::map<std::string, PredefinedView>::iterator i = predefinedViews.find(name);
    if (i != predefinedViews.end()) {
        predefinedViews.erase(i);
        return storeViewsToFile(author);

    } else {
        LOG_ERROR("Cannot delete view: %s", name.c_str());
        return -1;
    }
}


PredefinedView Project::getDefaultView() const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);
    std::map<std::string, PredefinedView>::const_iterator i;
    FOREACH(i, predefinedViews) {
        if (i->second.isDefault) return i->second;
    }
    return PredefinedView(); // empty name indicates no default view found
}

/** Create the directory and files for a new project
  *
  * @param[out] newProjectPath
  */
int Project::createProjectFiles(const std::string &repositoryPath, const std::string &projectName,
                                std::string &newProjectPath, const std::string &author)
{
    if (projectName.empty()) {
        LOG_ERROR("Cannot create project with empty name");
        return -1;
    }

    if (containsReservedName(projectName)) {
        LOG_ERROR("Cannot create project with name '%s': reserved", projectName.c_str());
        return -1;
    }

    newProjectPath = std::string(repositoryPath) + "/" + projectName;
    if (fileExists(newProjectPath)) {
        // File already exists
        LOG_ERROR("Cannot create project over existing path: %s", newProjectPath.c_str());
        return -1;
    }

    int r = mkdirs(newProjectPath);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", newProjectPath.c_str(), strerror(errno));
        return -1;
    }

    // create directory '.smip'
    std::string subpath = newProjectPath + "/" PATH_SMIP;
    r = mkdir(subpath);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
    }

    // create directory '.templates'
    subpath = newProjectPath + "/" PATH_TEMPLATES;
    r = mkdir(subpath);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
    }

    // init git repo
    r = gitInit(newProjectPath);
    if (r != 0) {
        LOG_ERROR("Could not init git '%s'", newProjectPath.c_str());
        return -1;
    }

    // create file 'project'
    Project p(newProjectPath);
    ProjectConfig pconfig = ProjectConfig::getDefaultConfig();
    r = p.modifyConfig(pconfig, author);
    if (r != 0) {
        LOG_ERROR("Could not create project config '%s'", newProjectPath.c_str());
        return -1;
    }

    // create file 'views'
    std::map<std::string, PredefinedView> defaultViews = PredefinedView::getDefaultViews();
    r = p.setPredefinedView(defaultViews, author);
    if (r != 0) {
        LOG_ERROR("Could not create project views '%s'", newProjectPath.c_str());
        return -1;
    }

    return 0;
}

/** Tag or untag an entry
  *
  */
int Project::toggleTag(const std::string &entryId, const std::string &tagname)
{
    LOCK_SCOPE(locker, LOCK_READ_WRITE);

    Entry *e = getEntry(entryId);
    if (!e) {
        // entry not found
        LOG_DEBUG("Entry not found: %s", entryId.c_str());
        return -1;
    }

    std::set<std::string> tags = e->issue->getTags(e->id);
    std::set<std::string>::iterator itt = tags.find(tagname);

    if (itt != tags.end()) {
        // remove the tag
        tags.erase(itt);

    } else {
        // add the tag
        tags.insert(tagname);
    }

    std::string tagData = Entry::serializeTags(tags);
    int err = gitdbSetNotes(getPathEntries(), entryId, tagData);
    if (err) {
        return -1;
    }

    e->issue->setTags(e->id, tags);

    return 0;
}


std::string Project::allocateNewIssueId()
{
    if (config.numberIssueAcrossProjects) return Database::allocateNewIssueId("global");

    maxIssueId++;
    if (maxIssueId == 0) LOG_ERROR("Project: max issue id zero: wrapped");

    const int SIZ = 25;
    char buffer[SIZ];
    snprintf(buffer, SIZ, "%u", maxIssueId);

    return std::string(buffer);
}

void Project::updateMaxIssueId(uint32_t i)
{
    if (config.numberIssueAcrossProjects) return Database::updateMaxIssueId("global", i);

    if (i > maxIssueId) maxIssueId = i;
}

/** Reload a whole project
  */
int Project::reload()
{
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);

    LOG_INFO("Reloading project '%s'...", getName().c_str());

    // delete all issues
    std::map<std::string, Issue*>::iterator issue;
    FOREACH(issue, issues) {
        delete issue->second;
    }
    issues.clear();

    // delete all associations
    associations.clear();
    reverseAssociations.clear();

    // load the project again
    int r = load();
    return r;
}

std::string Project::storeFile(const char *data, size_t len) const
{
    return gitdbStoreFile(path, data, len);
}


size_t Project::getNumIssues() const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);
    return issues.size();
}

long Project::getLastModified() const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);
    return lastModified;
}


void Project::loadPredefinedViews()
{
    LOG_FUNC();

    std::string viewsPath = path + '/' + PATH_VIEWS;

    PredefinedView::loadViews(viewsPath, predefinedViews);
    LOG_DEBUG("predefined views loaded: %ld", L(predefinedViews.size()));
}

/** Parse the sorting specification
  * Input syntax is:
  *    aa+bb-cc
  * (aa, bb, cc are property names)
  *
  * Ouput will be like:
  *    [ (true, 'aa'), (true, 'bb'), (false, 'cc') ]
  *
  */
std::list<std::pair<bool, std::string> > parseSortingSpec(const char *sortingSpec)
{
    bool currentOrder = true; // ascending
    size_t len = strlen(sortingSpec);
    std::string currentPropertyName;
    size_t currentOffset = 0;
    std::list<std::pair<bool, std::string> > result;
    while (currentOffset < len) {
        char c = sortingSpec[currentOffset];
        if (c == '+' || c == ' ' || c == '-' ) {
            if (currentPropertyName.size()>0) {
                // store previous property name
                result.push_back(std::make_pair(currentOrder, currentPropertyName));
                currentPropertyName = "";
            }
            if (c == '+' || c == ' ') currentOrder = true;
            else currentOrder = false;
        } else {
            currentPropertyName += c;
        }
        currentOffset++;
    }
    // store possible remaining currentPropertyName
    if (currentPropertyName.size()>0) result.push_back(std::make_pair(currentOrder, currentPropertyName));

    return result;
}

void Project::searchEntries(const char *sortingSpec, std::vector<Entry> &entries, int limit) const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);

    // concatenate all the entries of all the issues
    std::map<std::string, Issue*>::const_iterator i;
    FOREACH(i, issues) {
        entries.insert(entries.end(), i->second->entries.begin(), i->second->entries.end());
    }

    if (sortingSpec) {
        std::list<std::pair<bool, std::string> > sSpec = parseSortingSpec(sortingSpec);
        Entry::sort(entries, sSpec);
    }

    // limit the number of items
    if (limit >= 0 && (size_t)limit < entries.size()) entries.erase(entries.begin()+limit, entries.end());
}

/** search
  *   fulltext: text that is searched (optional: 0 for no fulltext search)
  *             The case is ignored.
  *   filterIn: list of propName:value
  *   filterOut: list of propName:value
  *   sortingSpec: aa+bb-cc (+ for ascending, - for descending order)
  *                sort issues by aa ascending, then by bb ascending, then by cc descending
  *
  * @return
  *    The list of matching issues.
  *
  * @remarks
  * If filterIn and filterOut specify each the same property, then the
  * filterOut will be either ignored or take precedence. Examples:
  * Example 1:
  * filterIn=propA:valueA1, filterOut=propA:valueA2
  *     => filterIn will keep only valueA1 (and exclude implicitely valueA2
  *        therefore filterOut is not necessary)
  *
  * Example 2:
  * filterIn=propA:valueA1, filterOut=propA:valueA1
  *     => filterOut takes precedence, valueA1 is excluded from the result
  */
void Project::search(const char *fulltextSearch,
                     const std::map<std::string, std::list<std::string> > &filterIn,
                     const std::map<std::string, std::list<std::string> > &filterOut,
                     const char *sortingSpec,
                     std::vector<IssueCopy> &returnedIssues) const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);

    // General algorithm:
    // For each issue:
    //     1. keep only those specified by filterIn and filterOut
    //     2. then, if fulltext is not null, walk through these issues and their
    //        related messages and keep those that contain <fulltext>
    //     3. then, do the sorting according to <sortingSpec>

    std::map<std::string, Issue*>::const_iterator i;
    for (i=issues.begin(); i!=issues.end(); i++) {

        const Issue* issue = i->second;
        // 1. filters
        if (!filterIn.empty() && !issue->isInFilter(filterIn, FILTER_IN)) continue;
        if (!filterOut.empty() && issue->isInFilter(filterOut, FILTER_OUT)) continue;

        // 2. search full text
        if (! issue->searchFullText(fulltextSearch)) {
            // do not keep this issue
            continue;
        }

        // keep this issue in the result
        IssueCopy icopy(*issue);
        consolidateAssociations(icopy, true);
        consolidateAssociations(icopy, false);
        returnedIssues.push_back(icopy);
    }

    // 4. do the sorting
    if (sortingSpec) {
        std::list<std::pair<bool, std::string> > sSpec = parseSortingSpec(sortingSpec);
        IssueCopy::sort(returnedIssues, sSpec);
    }
}

int Project::insertIssueInTable(Issue *i)
{
    LOG_FUNC();
    if (!i) {
        LOG_ERROR("Cannot insert null issue in project");
        return -1;
    }

    if (i->id.empty()) {
        LOG_ERROR("Cannot insert issue with empty id in project");
        return -2;
    }

    LOG_DEBUG("insertIssueInTable %s", i->id.c_str());

    std::map<std::string, Issue*>::const_iterator existingIssue;
    existingIssue = issues.find(i->id);
    if (existingIssue != issues.end()) {
        LOG_ERROR("Cannot insert issue %s: already in database", i->id.c_str());
        return -3;
    }

    // add the issue in the table
    issues[i->id] = i;
    return 0;
}


void Project::updateLastModified(int issueMtime)
{
    if (lastModified < issueMtime) lastModified = issueMtime;
}

/**
  * issues may be a list of 1 empty string, meaning that associations have been removed
  */
void Project::updateAssociations(const Issue *i, const std::string &associationName, const std::list<std::string> &issues)
{
    // lock not acquired as called from protected scopes (addEntry) and load

    if (!i) return;

    if (issues.empty() || issues.front() == "") {
        if (associations.find(i->id) != associations.end()) {
            associations[i->id].erase(associationName);
            if (associations[i->id].empty()) associations.erase(i->id);
        }

    } else {
        associations[i->id][associationName].clear();
        std::list<std::string>::const_iterator otherIssueX;
        FOREACH(otherIssueX, issues) {
            associations[i->id][associationName].insert(*otherIssueX);
        }
    }

    // clean up reverse associations, to cover the case where an association has been removed
    std::map<IssueId, std::map<AssociationId, std::set<IssueId> > >::iterator raIssue;
    FOREACH(raIssue, reverseAssociations) {
        std::map<AssociationId, std::set<IssueId> >::iterator raAssoName;
        raAssoName = raIssue->second.find(associationName);
        if (raAssoName != raIssue->second.end()) raAssoName->second.erase(i->id);
    }

    // add new reverse associations
    std::list<std::string>::const_iterator otherIssue;
    FOREACH(otherIssue, issues) {
        if (otherIssue->empty()) continue;
        reverseAssociations[*otherIssue][associationName].insert(i->id);
    }

#if 0
    // debug
    // dump associations
    FOREACH(raIssue, reverseAssociations) {
        std::map<std::string, std::set<std::string> >::iterator raAssoName;
        FOREACH(raAssoName, raIssue->second) {
            std::set<std::string>::iterator x;
            FOREACH(x, raAssoName->second) {
                LOG_DEBUG("reverseAssociations[%s][%s]=%s", raIssue->first.c_str(),
                          raAssoName->first.c_str(), x->c_str());
            }
        }
    }
#endif
}

void parseAssociation(std::list<std::string> &values)
{
    // parse the associated issues
    // the input is a string like "1, 2, 3".
    // we want to convert this to a list : ["1", "2", "3"]
    // and store it as a multi-valued property
    if (values.size() != 1) {
        LOG_ERROR("Unexpected association with %ld values", L(values.size()));
        return;
    }

    std::string v = values.front();
    values.clear(); // prepare for new value
    std::list<std::string> issueIds = split(v, " ,;");
    std::list<std::string>::const_iterator associatedIssue;
    FOREACH(associatedIssue, issueIds) {
        if (associatedIssue->empty()) continue; // because split may return empty tokens
        values.push_back(*associatedIssue);
    }

    values.sort();
}

/** Create a new issue
  *
  * - Allocate a new issue id
  */
Issue *Project::createNewIssue()
{
    LOG_FUNC();
    std::string issueId = allocateNewIssueId();

    Issue *i = new Issue();
    i->id = issueId;
    i->project = getName();

    return i;
}

/** If issueId is empty:
  *     - a new issue is created
  *     - its id is returned within parameter 'issueId'
  *
  * @param         properties
  * @param[in/out] issueId
  * @param[out]    entry
  * @param         username
  * @param[out]    oldIssue
  *
  * @return
  *     0 if no error. The entryId is fullfilled.
  *    >0 no entry was created due to no change.
  *    -1 error
  */
int Project::addEntry(PropertiesMap properties, const std::list<AttachedFileRef> &files,
                      std::string &issueId, Entry *&entry, std::string username, IssueCopy &oldIssue)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);

    entry = 0;

    // Check that all properties are in the project config, else remove them.
    // Also parse the associations, if any.
    //
    // Note that the values of properties that have a type select, multiselect and selectUser
    // are not verified (this is a known issue) TODO.
    std::map<std::string, std::list<std::string> >::iterator p;
    p = properties.begin();
    while (p != properties.end()) {
        bool doErase = false;
        std::string propertyName = p->first;

        if ( (propertyName == K_MESSAGE) || (propertyName == K_AMEND) )  {
            if (p->second.size() && p->second.front().empty()) {
                // erase if message or file is emtpy
                doErase = true;
            }
        } else {
            const PropertySpec *pspec = config.getPropertySpec(propertyName);
            if (!pspec && (propertyName != K_SUMMARY)) {
                // erase property because it is not part of the user properties of the project
                doErase = true;
            } // else do not erase and parse the association
            else if (pspec && pspec->type == F_ASSOCIATION) parseAssociation(p->second);
        }

        if (doErase) {
            // here we remove an item from the list that we are walking through
            // be careful...
            std::map<std::string, std::list<std::string> >::iterator itemToErase = p;
            p++;
            properties.erase(itemToErase);
        } else p++;

    }

    Issue *i = NULL;
    if (issueId.size() > 0) {
        // adding an entry to an existing issue

        i = getIssue(issueId);
        if (!i) {
            LOG_INFO("Cannot add new entry to unknown issue: %s", issueId.c_str());
            return -1;
        }

        oldIssue = copyIssue(*i);

        // Simplify the entry by removing properties that have the same value
        // in the issue (only the modified fields are stored)

        // Note that keep-old values are pruned here : values that are no longer
        // in the official values (select, multiselect, selectUser), but
        // that might still be used in some old issues.
        std::map<std::string, std::list<std::string> >::iterator entryProperty;
        entryProperty = properties.begin();
        while (entryProperty != properties.end()) {
            bool doErase = false;

            std::map<std::string, std::list<std::string> >::iterator issueProperty;
            issueProperty = i->properties.find(entryProperty->first);
            if (issueProperty != i->properties.end()) {
                if (issueProperty->second == entryProperty->second) {
                    // the value of this property has not changed
                    doErase = true;
                }
            }

            if (doErase) {
                // here we remove an item from the list that we are walking through
                // be careful...
                std::map<std::string, std::list<std::string> >::iterator itemToErase = entryProperty;
                entryProperty++;
                properties.erase(itemToErase);
            } else entryProperty++;
        }
    }

    if (properties.size() == 0 && files.empty()) {
        LOG_INFO("addEntry: no change. return without adding entry.");
        return 1; // no change
    }

    // at this point properties have been cleaned up

    FOREACH(p, properties) {
        LOG_DEBUG("properties: %s => %s", p->first.c_str(), join(p->second, ", ").c_str());
    }

    // if issueId is empty, create a new issue
    bool newIssueCreated = false;
    if (!i) {
        newIssueCreated = true;
        i = createNewIssue();
        if (!i) return -1;
        issueId = i->id; // update @param[out]
    }

    // create the new entry object
    Entry *e = Entry::createNewEntry(properties, files, username);

    // add the entry to the project and store to disk
    int r = storeNewEntry(i->id, e);
    if (r < 0) {
        delete e;
        return r; // already exists
    }

    // add the entry to the issue
    i->addEntry(e);

    if (newIssueCreated) {
        r = insertIssueInTable(i);
        if (r != 0) return r; // already exists
    }

    updateLastModified(e->ctime);

    // if some association has been updated, then update the associations tables
    FOREACH(p, properties) {
        const PropertySpec *pspec = config.getPropertySpec(p->first);
        if (pspec && pspec->type == F_ASSOCIATION) {
            updateAssociations(i, p->first, p->second);
        }
    }

    entry = e;

    return 0; // success
}

/** Add a new entry to the project
  *
  * Create a commit in the disk database
  */
int Project::storeNewEntry(const std::string &issueId, Entry *e)
{
    const std::string data = e->serialize();

    std::string entryId = GitIssue::addCommit(getPathEntries(), issueId, e->author, e->ctime, data, e->files);

    e->id = entryId;

    if (entryId.empty()) {
        LOG_ERROR("Failed to commit a new entry");
        return -1;
    }

    return 0; // success
}


Entry *Project::getEntry(const std::string &id) const
{
    //TODO
    //std::map<std::string, Entry*>::const_iterator e = entries.find(id);
    //if (e == entries.end()) return 0;
    //return e->second;
    return 0;
}


ProjectConfig Project::getConfig() const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);
    return config;
}

ProjectParameters Project::getProjectParameters() const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);
    ProjectParameters pParams;
    pParams.projectName = name;
    pParams.projectPath = path;
    pParams.pconfig = config;
    pParams.views = predefinedViews;
    return pParams;
}

/** Amend the message of an existing entry
  *
  * This creates a new entry that contains the amendment.
  *
  * @param      entryId
  * @param      msg
  * @param[out] entryOut
  * @param      username
  * @param[out] oldIssue
  *
  * @return
  *     0 success, an entry has been created
  *    >0 no entry was created due to no change
  *    <0 error
  */
int Project::amendEntry(const EntryId &entryId, const std::string &msg,
                        Entry *&entryOut, const std::string &username, IssueCopy &oldIssue)
{
    LOCK_SCOPE(locker, LOCK_READ_WRITE);

    Entry *e = getEntry(entryId);
    if (!e) return -1;

    if (time(0) - e->ctime > Database::getEditDelay()) return -1; // too late!

    if (e->author != username) return -1; // one cannot amend the message of somebody else

    if (e->isAmending()) return -1; // one cannot amend an amending message

    if (msg == e->getMessage()) return 0; // no change (the message is the same)

    // ok, we can proceed

    oldIssue = copyIssue(*(e->issue));

    PropertiesMap properties;
    properties[K_MESSAGE].push_back(msg);
    properties[K_AMEND].push_back(entryId);

    std::list<AttachedFileRef> files; // no file, empty list
    Entry *amendingEntry = Entry::createNewEntry(properties, files, username);

    int r = storeNewEntry(e->issue->id, amendingEntry);
    if (r != 0) {
        delete amendingEntry;
        return -2;
    }

    e->issue->amendEntry(amendingEntry);

    updateLastModified(amendingEntry->ctime);

    entryOut = amendingEntry;
    return 0;
}

/** Get the external program referenced by the trigger
 *
 * @return
 *     The command line associated with the trigger.
 *     Empty if no trigger configured
 */
std::string Project::getTriggerCmdline() const
{
    std::string trigger = getPath() + "/" + PATH_TRIGGER;
    std::string cmdline;
    int ret = loadFile(trigger, cmdline);
    if (ret != 0) return "";

    trim(cmdline);

    return cmdline;
}


#define FILE_PUSHED_BRANCHES "smit_pushed"
#define NEW_ISSUES "new_issues"


/** Update a project after a git push
 *
 *  The file $GIT_DIR/smit_pushed contains the branches
 * that have been pushed.
 *
 * update project entries if pushed entries
 *
 * 3 cases :
 * - pushed entries to existing issue (branches issues/<id>)
 * - pushed entries to new issue (branches new_issues/...)
 * - pushed onto branch master (project config, etc.)
 *
 * if some branches new_issues/... exist, then for each of them:
 *    - allocate an issue <id>
 *    - rename the branch to issues/<id>
 *
 * if some branches issues/... have been updated, then reload these issues
 *
 * if branch master has been updated, then reload config
 */
static void handleUpdateAfterGitPush(Project *pro, const std::string &pushedBranches)
{
    LOG_ERROR("handleUpdateAfterGitPush NOT IMPLEMENTED");

    // load file
    std::string lines;
    int err = loadFile(pushedBranches, lines);
    if (err) {
        // no such file ?
    }

    // parse the lines

    while (!lines.empty()) {
        std::string line = popToken(lines, '\n');
        std::string oldSha1 = popToken(line, ' ');
        std::string newSha1 = popToken(line, ' ');
        std::string &ref = line;

        if (ref.empty()) continue;

        // extract the part 'refs/heads/'
        popToken(ref, '/'); // remove 'refs'
        popToken(ref, '/'); // remove 'heads'
        std::string &branch = ref;
        if (branch == "master") {
            int err;// = pro->loadConfig();
            if (err == -1) {
                // question: how to deal with a broken config that has just been pushed ?
               // ???
                //LOG_DEBUG("Project '%s' not loaded because of errors while reading the config.", path.c_str());
                return;
            }

            //pro->loadPredefinedViews();

        } else {
            std::string group = popToken(branch, '/');
            if (group == NEW_ISSUES) {
                // allocate new id
                // rename branch
                // load this new issue in memory
                //xxx;
            } else if (group == "issues") {
                // reload this issue in memory
                //xxx;
            } else {
                LOG_ERROR("Invalid branch %s", branch.c_str());
            }

        }

    }
}
/** Run the git HTTP backend CGI for serving git pull or push
 *
 *
 */
int Project::runGitHttpBackend(const RequestContext *req, const std::string username, const std::string role)
{
    bool isPushRequest = gitIsPushRequest(req);

    LOCK_SCOPE(locker, LOCK_READ_WRITE); // lock read-only when possible

    // remove file FILE_PUSHED_BRANCHES of the project
    std::string pushedBranches = getPath() + "/.git/" FILE_PUSHED_BRANCHES;
    if (fileExists(pushedBranches)) {
        int err = unlink(pushedBranches.c_str());
        if (err) {
            LOG_ERROR("httpGitServeRequest: cannot unlink '%s': %s", pushedBranches.c_str(), STRERROR(errno));
            req->sendHttpHeader(500, "cannot unlink '%s'", pushedBranches.c_str());
            req->printf("\r\n"); // end header
            return -1;
        }
    }

    gitCgiBackend(req, req->getUri(), Database::getRootDirAbsolute(), username, role);

    if (fileExists(pushedBranches)) {

        handleUpdateAfterGitPush(this, pushedBranches);

    }
    return 0; // success
}






