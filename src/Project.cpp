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
#include "db.h"
#include "parseConfig.h"
#include "filesystem.h"
#include "logging.h"
#include "identifiers.h"
#include "global.h"
#include "stringTools.h"
#include "mg_win32.h"

#define K_PARENT "+parent"
#define K_AUTHOR "+author"
#define K_CTIME "+ctime"
#define K_PARENT_NULL "null"


/** init and load in memory the given project
  *
  * @param path
  *    Full path where the project is stored
  *
  * @return
  *    A pointer to the newly created project instance
  *    Null pointer if error
  *
  * Project names are encoded on the filesystem because we we want to
  * allow / and any other characters in project names.
  * We use a modified url-encoding, because:
  *   - url-encoding principle is simple
  *   - but with standard url-encoding, some browsers (eg: firefox)
  *     do a url-decoding when clicking on a href, and servers do another
  *     url-decoding, so that a double url-encoding would not be enough.
  */

Project *Project::init(const std::string &path)
{
    Project *p = new Project;
    LOG_DEBUG("Loading project %s (%p)...", path.c_str(), p);

    std::string bname = getBasename(path);
    p->name = urlNameDecode(bname);
    LOG_DEBUG("Project name: '%s'", p->name.c_str());

    p->path = path;
    p->maxIssueId = 0;

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

    loadTags();

    LOG_INFO("Project %s loaded: %d issues", path.c_str(), issues.size());

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

int Project::loadIssues()
{
    std::string pathToIssues = getIssuesDir();
    LOG_DEBUG("Loading issues: %s", pathToIssues.c_str());

    DIR *issuesDirHandle;
    if ((issuesDirHandle = openDir(pathToIssues.c_str())) == NULL) {
        LOG_ERROR("Cannot open directory '%s'", pathToIssues.c_str());
        return -1;
    }

    // walk through all issues
    std::string pathToObjects = path + '/' + PATH_OBJECTS;
    std::string issueId;
    int localMaxId = 0;
    while ((issueId = getNextFile(issuesDirHandle)) != "") {
        std::string latestEntryOfIssue;
        std::string path = pathToIssues + '/' + issueId;
        int r = loadFile(path, latestEntryOfIssue);
        if (r != 0) {
            LOG_ERROR("Cannot read file '%s': %s", path.c_str(), strerror(errno));
            continue; // go to next issue
        }
        trim(latestEntryOfIssue);

        Issue *issue = Issue::load(pathToObjects, latestEntryOfIssue);
        if (!issue) {
            LOG_ERROR("Cannot load issue %s", issueId.c_str());
            continue;
        }

        // store the entries in the 'entries' table
        Entry *e = issue->latest;
        while (e) {
            int r = insertEntryInTable(e);
            if (r != 0) {
                // this should not happen
                // maybe 2 issues pointing to the same first entry?
                LOG_ERROR("Cannot load issue %s", issueId.c_str());
            }
            e = e->prev;
        }

        // update the maximum id
        int intId = atoi(issueId.c_str());
        if (intId > 0 && (uint32_t)intId > localMaxId) localMaxId = intId;

        // store the issue in memory
        issue->id = issueId;
        insertIssueInTable(issue);
    }

    closeDir(issuesDirHandle);

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

int Project::get(const std::string &issueId, Issue &issue) const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);

    Issue *i = getIssue(issueId);
    if (!i) {
        // issue not found
        LOG_DEBUG("Issue not found: %s", issueId.c_str());
        return -1;
    }

    issue = *i;
    return 0;
}

void Project::getAllIssues(std::vector<Issue*> &issuesList)
{
    std::map<std::string, Issue*>::iterator i;
    FOREACH(i, issues) {
        issuesList.push_back(i->second);
    }
}

// @return 0 if OK, -1 on error
int Project::loadConfig()
{
    LOG_FUNC();
    std::string pathToProjectFile = path + "/" PATH_PROJECT_CONFIG;
    std::string objectid;
    int r = loadFile(pathToProjectFile, objectid);
    if (r != 0) {
        LOG_ERROR("Cannot load project config '%s': %s", pathToProjectFile.c_str(), strerror(errno));
        return -1;
    }

    trim(objectid);

    std::string pathToProjectConfig = getObjectsDir() + "/" + Object::getSubpath(objectid);

    r = ProjectConfig::load(pathToProjectConfig, config);
    return r;
}

int Project::modifyConfig(std::list<std::list<std::string> > &tokens, const std::string &author)
{
    LOG_FUNC();
    // verify the syntax of the tokens
    ProjectConfig c = ProjectConfig::parseProjectConfig(tokens);

    // keep unchanged the configuration items not managed via this modifyConfig
    c.numberIssueAcrossProjects = config.numberIssueAcrossProjects;

    return modifyConfig(c, author);
}

int Project::modifyConfig(ProjectConfig newConfig, const std::string &author)
{
    LOG_FUNC();
    ScopeLocker scopeLocker(lockerForConfig, LOCK_READ_WRITE);

    newConfig.ctime = time(0);
    newConfig.parent = config.id;
    newConfig.author = author;

    // write to file
    std::string data = newConfig.serialize();

    // write into objects database
    std::string newid;
    int r = Object::write(getObjectsDir(), data, newid);
    if (r < 0) {
        LOG_ERROR("Cannot write new config of project: id=%s", newid.c_str());
        return -1;
    }
    // write ref
    std::string newProjectRef = path + "/" PATH_PROJECT_CONFIG;
    r = writeToFile(newProjectRef, newid);
    if (r != 0) {
        LOG_ERROR("Cannot write new config of project: %s", newProjectRef.c_str());
        return -1;
    }

    config = newConfig;
    config.id = newid;

    return 0;
}

/** get a predefined view
  * @return
  * if not found an object PredefinedView with empty name is returned
  */
PredefinedView Project::getPredefinedView(const std::string &name)
{
    ScopeLocker scopeLocker(lockerForViews, LOCK_READ_ONLY);

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
int Project::setPredefinedView(const std::string &name, const PredefinedView &pv)
{
    LOG_DEBUG("setPredefinedView: %s -> %s", name.c_str(), pv.name.c_str());
    if (pv.name.empty()) return -3;
    if (pv.name == "_") return -2; // '_' is reserved

    ScopeLocker scopeLocker(lockerForViews, LOCK_READ_WRITE);

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
    return storeViewsToFile();
}


/**
  * No mutex handled in here.
  * Must be called from a mutexed scope (lockerForViews)
  */
int Project::storeViewsToFile()
{
    std::string fileContents;
    fileContents = K_SMIT_VERSION " " VERSION "\n";

    fileContents += PredefinedView::serializeViews(predefinedViews);

    std::string id;
    int r = Object::write(getObjectsDir(), fileContents, id);
    if (r < 0) return r;

    std::string subpath = path  + "/" + VIEWS_FILE;
    r = writeToFile(subpath, id);

    return r;
}

int Project::deletePredefinedView(const std::string &name)
{
    if (name.empty()) return -1;
    ScopeLocker scopeLocker(lockerForViews, LOCK_READ_WRITE);

    std::map<std::string, PredefinedView>::iterator i = predefinedViews.find(name);
    if (i != predefinedViews.end()) {
        predefinedViews.erase(i);
        return storeViewsToFile();

    } else {
        LOG_ERROR("Cannot delete view: %s", name.c_str());
        return -1;
    }
}


PredefinedView Project::getDefaultView() const
{
    ScopeLocker scopeLocker(lockerForViews, LOCK_READ_ONLY);
    std::map<std::string, PredefinedView>::const_iterator i;
    FOREACH(i, predefinedViews) {
        if (i->second.isDefault) return i->second;
    }
    return PredefinedView(); // empty name indicates no default view found
}

/** Create the directory and files for a new project
  */
int Project::createProjectFiles(const std::string &repositoryPath, const std::string &projectName,
                                std::string &resultingPath)
{
    if (projectName.empty()) {
        LOG_ERROR("Cannot create project with empty name");
        return -1;
    }
    if (projectName[0] == '.') {
        LOG_ERROR("Cannot create project with name starting with '.'");
        return -1;
    }


    resultingPath = std::string(repositoryPath) + "/" + Project::urlNameEncode(projectName);
    std::string path = resultingPath;
    int r = mkdir(path);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", path.c_str(), strerror(errno));
        return -1;
    }

    // create directory 'refs'
    std::string subpath = path + "/refs";
    r = mkdir(subpath);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
    }

    // create directory 'issues'
    subpath = path + '/' + PATH_ISSUES;
    r = mkdir(subpath);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
    }

    // create directory 'tags'
    subpath = path + '/' + PATH_TAGS;
    r = mkdir(subpath);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
    }

    // create file 'project'
    const std::string config = ProjectConfig::getDefaultConfig().serialize();
    std::string id;
    // Create object in database
    // This also creates the directory 'objects'
    std::string objectsDir = path + "/" + PATH_OBJECTS;
    r = Object::write(objectsDir, config, id);
    if (r < 0) {
        LOG_ERROR("Could not create project config");
        return r;
    }
    // Store the reference 'id'
    subpath = path  + "/" + PATH_PROJECT_CONFIG;
    r = writeToFile(subpath, id);
    if (r != 0) {
        LOG_ERROR("Could not create file '%s': %s", subpath.c_str(), strerror(errno));
        return r;
    }

    // create file 'views'
    std::map<std::string, PredefinedView> defaultViews = PredefinedView::getDefaultViews();
    std::string viewsStr = PredefinedView::serializeViews(defaultViews);
    r = Object::write(objectsDir, viewsStr, id);

    subpath = path  + "/" + VIEWS_FILE;
    r = writeToFile(subpath, id);
    if (r != 0) {
        LOG_ERROR("Could not create file '%s': %s", subpath.c_str(), strerror(errno));
        return r;
    }

    // create directory 'html'
    subpath = path + "/html";
    r = mkdir(subpath);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
    }

    // create directory 'tmp'
    subpath = path + "/" K_PROJECT_TMP;
    r = mkdir(subpath);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
    }

    return 0;
}

/** Tag or untag an entry
  *
  */
int Project::toggleTag(const std::string &entryId, const std::string &tagid)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);

    Entry *e = getEntry(entryId);
    if (!e) {
        // entry not found
        LOG_DEBUG("Entry not found: %s", entryId.c_str());
        return -1;
    } else {

        // invert the tag
        std::set<std::string>::iterator tag = e->tags.find(tagid);
        if (tag == e->tags.end()) e->tags.insert(tagid);
        else e->tags.erase(tag);

        tag = e->tags.find(tagid); // update tag status after inversion

        // store to disk
        std::string tagDir = getPath() + "/" PATH_TAGS;
        std::string path = tagDir + "/" + Object::getSubpath(entryId) + "." + tagid;
        if (tag != e->tags.end()) {
            std::string dir = getDirname(path);
            int r = mkdirs(getDirname(path));
            if (r != 0) {
                LOG_ERROR("Cannot create dir '%s': %s", dir.c_str(), strerror(errno));
                return -1;
            }

            // create the empty file
            r = writeToFile(path, "");
            if (r != 0) {
                LOG_ERROR("Cannot create tag '%s': %s", path.c_str(), strerror(errno));
                return -1;
            }

        } else {
            // remove the file
            int r = unlink(path.c_str());
            if (r != 0) {
                LOG_ERROR("Cannot remove tag '%s': %s", path.c_str(), strerror(errno));
                return -1;
            }
        }

        return 0;
    }
}

std::string Project::allocateNewIssueId()
{
    if (config.numberIssueAcrossProjects) return Database::allocateNewIssueId();

    maxIssueId++;
    if (maxIssueId == 0) LOG_ERROR("Project: max issue id zero: wrapped");

    const int SIZ = 25;
    char buffer[SIZ];
    snprintf(buffer, SIZ, "%u", maxIssueId);

    return std::string(buffer);
}

void Project::updateMaxIssueId(uint32_t i)
{
    if (config.numberIssueAcrossProjects) return Database::updateMaxIssueId(i);

    if (i > maxIssueId) maxIssueId = i;
}

/** Reload a whole project
  */
int Project::reload()
{
    ScopeLocker L1(locker, LOCK_READ_WRITE);
    ScopeLocker L2(lockerForConfig, LOCK_READ_WRITE);

    LOG_INFO("Reloading project '%s'...", getName().c_str());

    // delete all issues
    std::map<std::string, Issue*>::iterator issue;
    FOREACH(issue, issues) {
        delete issue->second;
    }
    issues.clear();

    // delete all entries
    std::map<std::string, Entry*>::iterator entry;
    FOREACH(entry, entries) {
        delete entry->second;
    }
    entries.clear();

    // delete all associations
    associations.clear();
    reverseAssociations.clear();

    // load the project again
    int r = load();
    return r;
}

/** Insert a file in the directory of attached files
  *
  * @param basename
  *     The file must be alredy present in the tmp directory of the project.
  *
  * @return
  *     0 : ok
  *     -1: file already exists
  *     -2: file name does not match hash of the file contents
  *     -3: internal error: cannot rename
  */
int Project::addFile(const std::string &objectId)
{
    ScopeLocker L1(locker, LOCK_READ_WRITE);

    std::string srcPath = getTmpDir() + "/" + objectId;
    std::string destPath = getObjectsDir() + "/" + Object::getSubpath(objectId);

    // TODO use Object::insertFile(getObjectsDir(), srcPath, id)

    // check that the hash of the file contents matches the file name (the 40 first characters)
    std::string sha1 = getSha1OfFile(srcPath);
    // check the SHA1
    if (sha1 != objectId) {
        LOG_ERROR("SHA1 does not match: %s (%s)", objectId.c_str(), sha1.c_str());
        // remove tmp file
        return -1;
    }

    if (fileExists(destPath)) {
        int r = cmpFiles(srcPath, destPath);
        if (r != 0) {
            LOG_ERROR("ID collision, files differ: %s", objectId.c_str());
            return -2;
        } else {
            // ok the are the same
            return 0;
        }
    }

    // rename to final location
    mkdirs(getDirname(destPath));
    int r = rename(srcPath.c_str(), destPath.c_str());
    if (r != 0) {
        LOG_ERROR("cannot rename %s -> %s: %s", srcPath.c_str(), destPath.c_str(), strerror(errno));
        // remove tmp file
        return -3;
    }

    return 0;
}


int Project::getNumIssues() const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);
    return issues.size();
}

void Project::loadPredefinedViews()
{
    LOG_FUNC();

    std::string viewsPath = path + '/' + VIEWS_FILE;

    std::string id;
    int n = loadFile(viewsPath.c_str(), id);
    if (n == 0) {
        trim(id); // remove possible \n
        std::string path = getObjectsDir() + "/" + Object::getSubpath(id);
        PredefinedView::loadViews(path, predefinedViews);
    } // else error of empty file

    LOG_DEBUG("predefined views loaded: %ld", L(predefinedViews.size()));
}

/** Look for tags: files <project>/refs/tags/<prefix-sha1>/<suffix-sha1>
  *
  * Tags are formed like this:
  *   <entry-id> '.' <tag-id>
  * Where: <entry-id> := <prefix-sha1> <suffix-sha1>
  */
void Project::loadTags()
{
    std::string tagsPath = path + "/" PATH_TAGS "/";
    DIR *tagsDirHandle = openDir(tagsPath.c_str());

    if (!tagsDirHandle) {
        LOG_DEBUG("No tags directory '%s'", tagsPath.c_str());
        return;
    }

    std::string filename;
    while ((filename = getNextFile(tagsDirHandle)) != "") {

        std::string prefix = filename;
        // get the issue object

        std::string suffixPath = tagsPath + "/" + prefix;
        // open this subdir and look for all files of this subdir
        DIR *suffixDirHandle = openDir(suffixPath.c_str());
        if (!suffixDirHandle) continue; // not a directory ?

        std::string tag;
        while ((tag = getNextFile(suffixDirHandle)) != "") {

            std::string entryId = popToken(tag, '.');
            // add the prefix
            entryId = prefix + entryId;
            std::map<std::string, Entry*>::iterator eit;
            eit = entries.find(entryId);
            if (eit == entries.end()) {
                LOG_ERROR("Tags for unknown entry: %s", entryId.c_str());
                continue;
            }
            Entry *e = eit->second;
            if (tag.empty()) tag = "tag"; // keep compatibility with version <= 1.2.x

            e->tags.insert(tag);
        }
        closeDir(suffixDirHandle);
    }
    closeDir(tagsDirHandle);
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
std::vector<const Issue*> Project::search(const char *fulltextSearch,
                                    const std::map<std::string, std::list<std::string> > &filterIn,
                                    const std::map<std::string, std::list<std::string> > &filterOut,
                                    const char *sortingSpec) const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);

    // General algorithm:
    // For each issue:
    //     1. keep only those specified by filterIn and filterOut
    //     2. then, if fulltext is not null, walk through these issues and their
    //        related messages and keep those that contain <fulltext>
    //     3. then, do the sorting according to <sortingSpec>
    std::vector<const Issue*> result;

    std::map<std::string, Issue*>::const_iterator i;
    for (i=issues.begin(); i!=issues.end(); i++) {

        const Issue* issue = i->second;
        // 1. filters
        if (!filterIn.empty() && !issue->isInFilter(filterIn)) continue;
        if (!filterOut.empty() && issue->isInFilter(filterOut)) continue;

        // 2. search full text
        if (! issue->searchFullText(fulltextSearch)) {
            // do not keep this issue
            continue;
        }

        // keep this issue in the result
        result.push_back(issue);
    }

    // 4. do the sorting
    if (sortingSpec) {
        std::list<std::pair<bool, std::string> > sSpec = parseSortingSpec(sortingSpec);
        sort(result, sSpec);
    }
    return result;
}


std::map<std::string, std::set<std::string> > Project::getReverseAssociations(const std::string &issue) const
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);

    std::map<std::string, std::map<std::string, std::set<std::string> > >::const_iterator raIssue;
    raIssue = reverseAssociations.find(issue);
    if (raIssue == reverseAssociations.end()) return std::map<std::string, std::set<std::string> >();
    else return raIssue->second;
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

int Project::insertEntryInTable(Entry *e)
{
    LOG_FUNC();
    if (!e) {
        LOG_ERROR("Cannot insert null entry in project");
        return -1;
    }
    if (e->id.empty()) {
        LOG_ERROR("Cannot insert entry with empty id in project");
        return -2;
    }

    LOG_DEBUG("insertEntryInTable %s", e->id.c_str());

    std::map<std::string, Entry*>::const_iterator existingEntry;
    existingEntry = entries.find(e->id);
    if (existingEntry != entries.end()) {
        LOG_ERROR("Cannot insert entry %s: already in database", e->id.c_str());
        return -3;
    }

    // add the issue in the table
    entries[e->id] = e;
    return 0;
}


int Project::storeRefIssue(const std::string &issueId, const std::string &entryId)
{
    LOG_DIAG("storeRefIssue: %s -> %s", issueId.c_str(), entryId.c_str());
    std::string issuePath = path + "/" PATH_ISSUES "/" + issueId;
    int r = writeToFile(issuePath, entryId);
    if (r!=0) {
        LOG_ERROR("Cannot store issue %s", issueId.c_str());
    }
    return r;
}

/** Rename an issue (take the next available id)
  *
  * No mutex protection here.
  *
  * @return
  *     the newly assigned
  *     or and empty string in case of failure
  */
std::string Project::renameIssue(const std::string &oldId)
{
    std::map<std::string, Issue*>::iterator i;
    i = issues.find(oldId);
    if (i == issues.end()) {
        LOG_ERROR("Cannot rename issue %s: not in database", oldId.c_str());
        return "";
    }
    if (!i->second) {
        LOG_ERROR("Cannot rename issue %s: null issue", oldId.c_str());
        return "";
    }

    // get a new id
    std::string newId = allocateNewIssueId();

    int r = renameIssue(*(i->second), newId);
    if (r!=0) return "";

    return newId;
}

/** Rename an issue
  *
  * No mutex protection here.
  */
int Project::renameIssue(Issue &i, const std::string &newId)
{
    std::string oldId = i.id;

    // add the issue in the table
    issues[newId] = &i;

    // delete the old slot
    issues.erase(oldId);

    // set the new id
    i.id = newId;

    // store the new id on disk
    int r = storeRefIssue(newId, i.latest->id);
    if (r!=0) {
        return -1;
    }

    // unlink the old issue
    std::string oldIssuePath = path + "/" PATH_ISSUES "/" + oldId;
    r = unlink(oldIssuePath.c_str());
    if (r!=0) {
        LOG_ERROR("Cannot unlink %s: %s", oldIssuePath.c_str(), strerror(errno));
        return -1;
    }

    return 0;
}


/** Add an issue to the project
  */
int Project::addNewIssue(Issue &i)
{
    // insert the issue in the table of issues
    int r = insertIssueInTable(&i);
    if (r!=0) return r;

    // Store issue ref on disk
    r = storeRefIssue(i.id, i.latest->id);

    return r;
}


/**
  * issues may be a list of 1 empty string, meaning that associations have been removed
  */
void Project::updateAssociations(const Issue *i, const std::string &associationName, const std::list<std::string> &issues)
{
    // lock not acquired as called from protected scopes (addEntry) and load

    if (!i) return;

    if (issues.empty() || issues.front() == "") {
        associations[i->id].erase(associationName);
        if (associations[i->id].empty()) associations.erase(i->id);

    } else associations[i->id][associationName] = issues;


    // convert list to set
    std::set<std::string> otherIssues;
    std::list<std::string>::const_iterator otherIssue;
    FOREACH(otherIssue, issues) {
        if (! otherIssue->empty()) otherIssues.insert(*otherIssue);
    }

    // clean up reverse associations, to cover the case where an association has been removed
    std::map<std::string, std::map<std::string, std::set<std::string> > >::iterator raIssue;
    FOREACH(raIssue, reverseAssociations) {
        std::map<std::string, std::set<std::string> >::iterator raAssoName;
        raAssoName = raIssue->second.find(associationName);
        if (raAssoName != raIssue->second.end()) raAssoName->second.erase(i->id);
    }

    // add new reverse associations
    FOREACH(otherIssue, issues) {
        if (otherIssue->empty()) continue;
        reverseAssociations[*otherIssue][associationName].insert(i->id);
    }

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
    // create new directory for this issue
    std::string issueId = allocateNewIssueId();

    Issue *i = new Issue();
    i->id = issueId;

    return i;
}
/** store entry on disk
  */
int Project::storeEntry(const Entry *e)
{
    const std::string data = e->serialize();

    // store the entry
    std::string id;
    int r = Object::write(getObjectsDir(), data, id);
    if (r < 0) {
        // error.
        LOG_ERROR("Could not write new entry to disk");
        return -2;
    }

    // check for debug TODO
    if (id != e->id) {
        LOG_ERROR("sha1 do not match: s=%s <> e->id=%s", id.c_str(), e->id.c_str());
    }

    return 0;
}

/** If issueId is empty:
  *     - a new issue is created
  *     - its id is returned within parameter 'issueId'
  *
  * @param entry IN/OUT
  *
  * @return
  *     0 if no error. The entryId is fullfilled.
  *       except if no entry was created due to no change.
  */
int Project::addEntry(PropertiesMap properties, const std::string &issueId,
                      Entry *&entry, std::string username)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);
    ScopeLocker scopeLockerConfig(lockerForConfig, LOCK_READ_ONLY);

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

        if ( (propertyName == K_MESSAGE) ||
             (propertyName == K_FILE)    || (propertyName == K_AMEND) )  {
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

    Issue *i = 0;
    if (issueId.size() > 0) {
        // adding an entry to an existing issue

        i = getIssue(issueId);
        if (!i) {
            LOG_INFO("Cannot add new entry to unknown issue: %s", issueId.c_str());
            return -1;
        }

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

    if (properties.size() == 0) {
        LOG_INFO("addEntry: no change. return without adding entry.");
        return 1; // no change
    }

    // at this point properties have been cleaned up

    FOREACH(p, properties) {
        LOG_DEBUG("properties: %s => %s", p->first.c_str(), join(p->second, ", ").c_str());
    }

    // write the entry to disk

    // if issueId is empty, create a new issue
    bool newIssueCreated = false;
    if (!i) {
        newIssueCreated = true;
        i = createNewIssue();
        if (!i) return -1;
    }

    // create the new entry object
    Entry *e = Entry::createNewEntry(properties, username, i->latest);

    // add the entry to the project
    int r = addNewEntry(e);
    if (r != 0) return r; // already exists

    // add the entry to the issue
    i->addEntry(e);

    if (newIssueCreated) {
        r = insertIssueInTable(i);
        if (r != 0) return r; // already exists
    }

	// update latest entry of issue on disk
    r = storeRefIssue(i->id, e->id);
    if (r < 0) return r;

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
  */
int Project::addNewEntry(Entry *e)
{
    // add this entry in internal in-memory tables
    int r = insertEntryInTable(e);
    if (r != 0) return r; // already exists

    r = storeEntry(e);

    return r;
}


/** Push an uploaded entry in the database
  *
  * An error is raised in any of the following cases:
  * (a) the author of the entry is not the same as the username
  * (b) the parent is not null and issueId does not already exist
  * (c) the parent is not null and does not match the latest entry of the existing issueId
  * (d) the entry already exists
  *
  * Error cases (c) and (d) may be resolved by a smit-pull from the client.
  *
  * A new issue is created if the parent of the pushed entry is 'null'
  * In this case, a new issueId is assigned, then it is returned (IN/OUT parameter).
  *
  * @return
  *     0 success, and the issueId is possibly updated
  *    -1 client error occurred (map to HTTP 400 Bad Request)
  *    -2 server error (map to HTTP 500 Internal Server Error)
  *    -3 conflict that could be resolved by a pull (map to HTTP 409 Conflict)
  */
int Project::pushEntry(std::string &issueId, const std::string &entryId,
                       const std::string &username, const std::string &tmpPath)
{
    LOG_FUNC();
    LOG_DEBUG("pushEntry(%s, %s, %s, %s)", issueId.c_str(), entryId.c_str(),
              username.c_str(), tmpPath.c_str());

    // load the file as an entry
    Entry *e = Entry::loadEntry(tmpPath, entryId, true); // check also that the sha1s match
    if (!e) return -1;

    // check that the username is the same as the author of the entry
    if (e->author != username) {
        LOG_ERROR("pushEntry error: usernames do not match (%s / %s)",
                  username.c_str(), e->author.c_str());
        delete e;
        return -1;
    }

    Issue *i = 0;
    Issue *newI = 0;
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);

    // check if the entry already exists
    Entry *existingEntry = getEntry(entryId);
    if (existingEntry) {
        LOG_ERROR("Pushed entry already exists: %s", entryId.c_str());
        delete e;
        return -3; // conflict
    }

    if (e->parent == K_PARENT_NULL) {
        LOG_DEBUG("pushEntry: parent null");
        // assign a new issue id
        newI = createNewIssue();
        i = newI;
        issueId = newI->id; // update the IN/OUT parameter

    } else {
        i = getIssue(issueId);
        if (!i) {
            LOG_ERROR("pushEntry error: parent is not null and issueId does not exist (%s / %s)",
                      issueId.c_str(), entryId.c_str());
            delete e;
            return -1; // the parent is not null and issueId does not exist
        }
        if (i->latest->id != e->parent) {
            LOG_ERROR("pushEntry error: parent does not match latest entry (%s / %s)",
                      issueId.c_str(), entryId.c_str());
            delete e;
            return -3; // conflict
        }
    }

    if (newI) {
        // insert the new issue in the database
        int r = insertIssueInTable(i);
        if (r != 0) {
            delete e;
            delete i;
            return -2;
        }
    }

    // insert the new entry in the issue
    i->addEntry(e);

    // add the new entry in the project
    int r = addNewEntry(e);
    if (r != 0) return -2;

    // store the new ref of the issue
    r = storeRefIssue(i->id, i->latest->id);
    if (r != 0) {
        if (newI) delete newI;
        delete e;
        return -2;
    }

    return 0;
}

Entry *Project::getEntry(const std::string &id) const
{
    std::map<std::string, Entry*>::const_iterator e = entries.find(id);
    if (e == entries.end()) return 0;
    return e->second;
}


ProjectConfig Project::getConfig() const
{
    ScopeLocker scopeLocker(lockerForConfig, LOCK_READ_ONLY);
    return config;
}

std::map<std::string, PredefinedView> Project::getViews() const
{
    ScopeLocker scopeLocker(lockerForViews, LOCK_READ_ONLY);
    return predefinedViews;
}



/** Deleting an entry is only possible if:
  *     - the deleting happens less than DELETE_DELAY_S seconds after creation of the entry
  *     - this entry is the HEAD (has no child)
  *     - this entry is not the first of the issue
  *     - the author of the entry is the same as the given username
  * @return
  *     0 if success
  *     <0 in case of error
  *
  * Uploaded files are not deleted.
  * Temporarily, deleting an entry is the same as amending it with an empty message.
  * TODO: remove deleteEntry() and replace by amendEntry()
  *
  */

int Project::deleteEntry(const std::string &entryId, const std::string &username)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);

    std::map<std::string, Entry*>::iterator ite;
    ite = entries.find(entryId);
    if (ite == entries.end()) return -1;

    Entry *e = ite->second;

    if (time(0) - e->ctime > DELETE_DELAY_S) return -2;
    else if (e->parent == K_PARENT_NULL) return -3;
    else if (e->author != username) return -4;
    //else if (i->latest != e) return -7;
    else if (e->isAmending()) return -8; // one cannot amend an amending message

    // ok, we can proceed
    Entry *amendingEntry = e->issue->amendEntry(entryId, "", username);
    if (!amendingEntry) {
        // should never happen
        LOG_ERROR("amending entry: null");
        return -1;
    }

    int r = addNewEntry(amendingEntry);
    if (r != 0) return r;

    // update latest entry of issue on disk
    r = storeRefIssue(e->issue->id, amendingEntry->id);
    if (r < 0) return r;


    return 0;
}







