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

#include "db.h"
#include "parseConfig.h"
#include "logging.h"
#include "identifiers.h"
#include "global.h"
#include "stringTools.h"
#include "mg_win32.h"

#define PROJECT_FILE "project"
#define ISSUES "issues" // sub-directory of a project where the entries are stored
#define VIEWS_FILE "views"
#define TAGS_DIR "tags"
#define K_DELETED "_del"


#define K_PARENT "+parent"
#define K_AUTHOR "+author"
#define K_CTIME "+ctime"
#define K_ISSUE_ID "id"
#define K_PARENT_NULL "null"


/**
  * Directory layout within a project directory
  *
  * issues/<issue>/<entry>
  * issues/<issue>/_del/<entry>
  * files/<file>
  * tags/<issue>/<entry>
  *
  */

Database Database::Db;


int dbLoad(const char * pathToRepository)
{
    // look for all files "pathToRepository/p/project"
    // and parse them
    // then, load all pathToRepository/p/issues/*/*

    Database::Db.pathToRepository = pathToRepository;
    DIR *dirp;

    if ((dirp = opendir(pathToRepository)) == NULL) {
        return -1;

    } else {
        struct dirent *dp;

        while ((dp = readdir(dirp)) != NULL) {
            // Do not show current dir and hidden files
            LOG_DEBUG("d_name=%s", dp->d_name);
            if (0 == strcmp(dp->d_name, ".")) continue;
            if (0 == strcmp(dp->d_name, "..")) continue;
            std::string pathToProject = pathToRepository;
            pathToProject += '/';
            pathToProject += dp->d_name;
            Database::loadProject(pathToProject.c_str());
        }
        closedir(dirp);
    }
    return Database::Db.projects.size();
}


std::string Issue::getSummary() const
{
    return getProperty(properties, "summary");
}

/** Get the specification of a given property
  *
  * @return 0 if not found.
  */
const PropertySpec *ProjectConfig::getPropertySpec(const std::string name) const
{
    std::list<PropertySpec>::const_iterator p;
    FOREACH(p, properties) {
        if (p->name == name) return &(*p);
    }
    return 0;
}

/** Get the list of all properties
  */
std::list<std::string> ProjectConfig::getPropertiesNames() const
{
    std::list<std::string> colspec;

    // get user defined properties
    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, properties) {
        colspec.push_back(pspec->name);
    }

    // add mandatory properties that are not included in orderedProperties
    std::list<std::string> reserved = getReservedProperties();
    colspec.insert(colspec.begin(), reserved.begin(), reserved.end());
    return colspec;
}

std::list<std::string> ProjectConfig::getReservedProperties() const
{
    std::list<std::string> reserved;
    reserved.push_back("id");
    reserved.push_back("ctime");
    reserved.push_back("mtime");
    reserved.push_back("summary");
    return reserved;
}

/** load in memory the given project
  * re-load if it was previously loaded
  * @param path
  *    Full path where the project is stored
  * @param name
  *    Name of the project (generally the same as the basename of the path)
  *
  * @return 0 if success, -1 if failure
  *
  * Project names are encoded on the filesystem because we we want to
  * allow / and any other characters in project names.
  * We use a modified url-encoding, because:
  *   - url-encoding principle is simple
  *   - but with standard url-encoding, some browsers (eg: firefox)
  *     do a url-decoding when clicking on a href, and servers do another
  *     url-decoding, so that a double url-encoding would not be enough.
  */

Project *Project::load(const char *path)
{
    Project *p = new Project;
    LOG_DEBUG("Loading project %s (%p)...", path, p);

    std::string bname = getBasename(path);
    p->name = urlNameDecode(bname);
    LOG_DEBUG("Project name: '%s'", p->name.c_str());

    p->path = path;
    p->maxIssueId = 0;

    int r = p->loadConfig(path);
    if (r == -1) {
        delete p;
        LOG_DEBUG("Project '%s' not loaded because of errors while reading the config.", path);
        return 0;
    }

    p->loadPredefinedViews(path);

    r = p->loadEntries(path);
    if (r == -1) {
        LOG_ERROR("Project '%s' not loaded because of errors while reading the entries.", path);
        delete p;
        return 0;
    }

    p->loadTags(path);

    LOG_INFO("Project %s loaded.", path);

    p->consolidateIssues();

    return p;
}

Entry *loadEntry(std::string dir, const char* basename)
{
    // load a given entry
    std::string path = dir + '/' + basename;
    const char *buf = 0;
    int n = loadFile(path.c_str(), &buf);

    if (n <= 0) return 0; // error or empty file

    Entry *e = new Entry;
    e->id = basename;

    std::list<std::list<std::string> > lines = parseConfigTokens(buf, n);
    free((void*)buf);

    std::list<std::list<std::string> >::iterator line;
    int lineNum = 0;
    std::string smitVersion = "1.0"; // default value if version is not present
    for (line=lines.begin(); line != lines.end(); line++) {
        lineNum++;
        // each line should be a key / value pair
        if (line->size() < 2) {
            LOG_ERROR("Invalid line size %d (%s:%d)", line->size(), path.c_str(), lineNum);
            continue; // ignore this line
        }
        std::string key = line->front();
        line->pop_front(); // remove key from tokens
        std::string firstValue = line->front();

        if (0 == key.compare(K_CTIME)) {
            e->ctime = atoi((char*)firstValue.c_str());
        } else if (0 == key.compare(K_PARENT)) e->parent = firstValue;
        else if (0 == key.compare(K_AUTHOR)) e->author = firstValue;
        else if (key == K_SMIT_VERSION) smitVersion = firstValue;
        else {
            e->properties[key] = *line;
        }
    }
    // if value is not 1.x, then raised a warning
    if (smitVersion.empty() || 0 != strncmp("1.", smitVersion.c_str(), 2)) {
        LOG_INFO("Version of entry %s higher than current Smit version (%s). Check compatibility.",
                 smitVersion.c_str(), VERSION);
    }

    return e;
}

/** Copy properties of an entry to an issue.
  * If overwrite == false, then an already existing property in the
  * issue will not be overwritten.
  */
void Issue::consolidateIssueWithSingleEntry(Entry *e, bool overwrite) {
    std::map<std::string, std::list<std::string> >::iterator p;
    FOREACH(p, e->properties) {
        if (p->first.size() && p->first[0] == '+') continue; // do not consolidate these (+file, +message, etc.)
        if (overwrite || (properties.count(p->first) == 0) ) {
            properties[p->first] = p->second;
        }
    }
    // update also mtime of the issue
    if (mtime == 0 || overwrite) mtime = e->ctime;
}

/** Consolidate an issue by accumulating all its entries
  *
  * This method must be called from a mutex-protected scope (no mutex is managed in here).
  */
void Issue::consolidate()
{
    if (!latest) {
        // missign latest
        LOG_ERROR("Cannot consolidate issue '%s': missing latest", id.c_str());
        return;
    }
    // starting from the head, walk through all entries
    // following the _parent properties.

    Entry *e = latest;
    // the entries are walked through backwards (from most recent to oldest)
    while (e) {
        // for each property of the parent,
        // create the same property in the issue, if not already existing
        // (in order to have only most recent properties)

        consolidateIssueWithSingleEntry(e, false); // do not overwrite as we move from most recent to oldest
        ctime = e->ctime; // the oldest entry will take precedence for ctime
        e = e->prev;
    }
}

// 1. Walk through all loaded issues and compute the head
//    if it was not found earlier during the loadEntries.
// 2. Once the head is found, walk through all the entries
//    and fulfill the properties of the issue.
void Project::consolidateIssues()
{
    //LOG_DEBUG("consolidateIssues()...");
    std::map<std::string, Issue*>::iterator i;
    for (i = issues.begin(); i != issues.end(); i++) {
        Issue *currentIssue = i->second;
        currentIssue->consolidate();
    }
    LOG_DEBUG("consolidateIssues() done.");
}
int Project::loadEntries(const char *path)
{
    // load files path/issues/*/*
    std::string pathToEntries = path;
    pathToEntries = pathToEntries + '/' + ISSUES;
    DIR *entriesDirHandle;
    if ((entriesDirHandle = opendir(pathToEntries.c_str())) == NULL) {
        LOG_ERROR("Cannot open directory '%s'", pathToEntries.c_str());
        return -1;
    }

    struct dirent *issueDir;
    uint32_t localMaxId = 0;

    // walk through all issues
    while ((issueDir = readdir(entriesDirHandle)) != NULL) {
        if (0 == strcmp(issueDir->d_name, ".")) continue;
        if (0 == strcmp(issueDir->d_name, "..")) continue;

        std::string issuePath = pathToEntries;
        issuePath = issuePath + '/' + issueDir->d_name;
        // open this subdir and look for all files of this subdir
        DIR *issueDirHandle;
        if ((issueDirHandle = opendir(issuePath.c_str())) == NULL) continue; // not a directory
        else {
            // we are in a issue directory
            Issue *issue = new Issue();
            issue->id.assign(issueDir->d_name);

            // check the maximum id
            int intId = atoi(issueDir->d_name);
            if (intId > 0) updateMaxIssueId(intId);
            if (intId > 0 && intId > localMaxId) localMaxId = intId;

            issues[issue->id] = issue;

            struct dirent *entryFile;
            while ((entryFile = readdir(issueDirHandle)) != NULL) {
                if (0 == strcmp(entryFile->d_name, ".")) continue;
                if (0 == strcmp(entryFile->d_name, "..")) continue;
                if (0 == strcmp(entryFile->d_name, K_DELETED)) continue;
                if (0 == strcmp(entryFile->d_name, "_HEAD")) {
                    // obsolete.
                    LOG_INFO("Found obsolete _HEAD");
                    continue;
                }
                // regular entry
                std::string filePath = issuePath + '/' + entryFile->d_name;
                Entry *e = loadEntry(issuePath, entryFile->d_name);
                if (e) issue->entries[e->id] = e;
                else LOG_ERROR("Cannot load entry '%s'", filePath.c_str());
            }

            closedir(issueDirHandle);
            issue->computeLatestEntry();
        }
    }
    closedir(entriesDirHandle);

    LOG_INFO("Issues and entries loaded. localMaxId=%d, numbering=%s, globalMaxId=%d", localMaxId,
             config.numberIssueAcrossProjects?"global":"local", Database::getMaxIssueId());
    LOG_DEBUG("Max issue id: %d", maxIssueId);
    return 0;
}
Issue *Project::getIssue(const std::string &id) const
{
    std::map<std::string, Issue*>::const_iterator i;
    i = issues.find(id);
    if (i == issues.end()) return 0;
    else return i->second;
}

int Project::get(const char *issueId, Issue &issue)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);

    Issue *i = getIssue(issueId);
    if (!i) {
        // issue not found
        LOG_DEBUG("Issue not found: %s", issueId);
        return -1;
    } else {
        issue = *i;
    }

    return 0;
}

/** Guess the head from the previously loaded entries
  *
  * And resolve missing nodes.
  */
int Issue::computeLatestEntry()
{
    std::map<std::string, Entry*>::iterator eit;
    FOREACH(eit, entries) {
        Entry *e = eit->second;
        if (e->parent == K_PARENT_NULL) e->prev = 0;
        else {
            std::map<std::string, Entry*>::iterator parentIt = entries.find(e->parent);
            if (parentIt != entries.end()) {
                Entry *parent = parentIt->second;
                if (parent->next) {
                    // error: the next of the parent was already assigned
                    LOG_ERROR("Entry '%s' has already a child: '%s'", parent->id.c_str(),
                              parent->next->id.c_str());
                    // try to resolve this
                    while (parent->next) parent = parent->next;
                    LOG_INFO("Repair: new parent for '%s': '%s'", e->id.c_str(), parent->id.c_str());
                }
                parent->next = e;
                e->prev = parent;
            } else {
                // error: parent is missing
                LOG_ERROR("Parent missing: '%s'", e->parent.c_str());
            }
        }
    }
    // now find the entry that has no 'next'
    FOREACH(eit, entries) {
        Entry *e = eit->second;
        if (e->next == 0) {
            if (!latest) latest = e;
            else {
                // error: another entry was already claimed as latest
                LOG_ERROR("Entries conflict for 'latest': %s, %s", latest->id.c_str(), e->id.c_str());
            }
        }
    }
    return 0;
}


/** Convert a string to a PropertyType
  *
  * @return 0 on success, -1 on error
  */
int strToPropertyType(const std::string &s, PropertyType &out)
{
    if (s == "text") out = F_TEXT;
    else if (s == "selectUser") out = F_SELECT_USER;
    else if (s == "select") out = F_SELECT;
    else if (s == "multiselect") out = F_MULTISELECT;
    else if (s == "textarea") out = F_TEXTAREA;
    else if (s == "textarea2") out = F_TEXTAREA2;

    else return -1; // error


    return 0; // ok
}

PropertySpec parsePropertySpec(std::list<std::string> & tokens)
{
    // Supported syntax:
    // name [label <label>] type params ...
    // type = text | select | multiselect | selectUser | ...
    PropertySpec property;
    if (tokens.size() < 2) {
        LOG_ERROR("Not enough tokens");
        return property; // error, indicated to caller by empty name of property
    }

    property.name = tokens.front();
    // check that property name contains only [a-zA-Z0-9-_]
    const char* allowedInPropertyName = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
    if (property.name.find_first_not_of(allowedInPropertyName) != std::string::npos) {
        // invalid character
        LOG_ERROR("Invalid property name: %s", property.name.c_str());
        property.name = "";
        return property;
    }
    tokens.pop_front();

    if (tokens.empty()) {
        LOG_ERROR("Not enough tokens");
        property.name = "";
        return property; // error, indicated to caller by empty name of property
    }

    // look for optional -label parameter
    if (tokens.front() == "-label") {
        tokens.pop_front();
        if (tokens.empty()) {
            LOG_ERROR("Not enough tokens (-label)");
            property.name = "";
            return property; // error, indicated to caller by empty name of property
        }
        property.label = tokens.front();
        tokens.pop_front();
    }

    if (tokens.empty()) {
        LOG_ERROR("Not enough tokens");
        property.name = "";
        return property; // error, indicated to caller by empty name of property
    }
    std::string type = tokens.front();
    tokens.pop_front();
    int r = strToPropertyType(type, property.type);
    if (r < 0) { // error, unknown type
        LOG_ERROR("Unkown property type '%s'", type.c_str());
        property.name.clear();
        return property; // error, indicated to caller by empty name of property
    }

    if (F_SELECT == property.type || F_MULTISELECT == property.type) {
        // populate the allowed values
        while (tokens.size() > 0) {
            std::string value = tokens.front();
            tokens.pop_front();
            property.selectOptions.push_back(value);
        }
    }
    return property;
}

/** Return a configuration object from a list of lines of tokens
  * The 'lines' parameter is modified and cleaned up of incorrect lines
  */
ProjectConfig parseProjectConfig(std::list<std::list<std::string> > &lines)
{
    ProjectConfig config;

    std::list<std::list<std::string> >::iterator line;
    std::list<std::list<std::string> > wellFormatedLines;

    FOREACH (line, lines) {
        wellFormatedLines.push_back(*line);

        std::string token = pop(*line);
        if (token == K_SMIT_VERSION) {
            // not used in this version
            std::string v = pop(*line);
            LOG_DEBUG("Smit version of project: %s", v.c_str());

        } else if (0 == token.compare("addProperty")) {
            PropertySpec property = parsePropertySpec(*line);
            if (property.name.size() > 0) {
                config.properties.push_back(property);
                LOG_DEBUG("properties: added %s", property.name.c_str());

                if (! property.label.empty()) config.propertyLabels[property.name] = property.label;

            } else {
                // parse error, ignore
                wellFormatedLines.pop_back(); // remove incorrect line
            }

        } else if (0 == token.compare("setPropertyLabel")) {
            if (line->size() != 2) {
                LOG_ERROR("Invalid setPropertyLabel");
                wellFormatedLines.pop_back(); // remove incorrect line
            } else {
                std::string propName = line->front();
                std::string propLabel = line->back();
                config.propertyLabels[propName] = propLabel;
            }

        } else if (token == "numberIssues") {
            std::string value = pop(*line);
            if (value == "global") config.numberIssueAcrossProjects = true;
            else LOG_ERROR("Invalid value '%s' for numberIssues.", value.c_str());

        } else if (token == "tag") {
            TagSpec tagspec;
            tagspec.id = pop(*line);
            tagspec.label = tagspec.id; // by default label = id

            if (tagspec.id.empty()) {
                LOG_ERROR("Invalid tag id");
                continue; // ignore current line and go to next one
            }
            while (line->size()) {
                token = pop(*line);
                if (token == "-label") {
                    std::string label = pop(*line);
                    if (label.empty()) LOG_ERROR("Invalid empty tag label");
                    else tagspec.label = label;
                } else if (token == "-display") {
                    tagspec.display = true;
                } else {
                    LOG_ERROR("Invalid token '%s' in tag specification", token.c_str());
                }
            }
            LOG_DEBUG("tag '%s' -label '%s' -display=%d", tagspec.id.c_str(), tagspec.label.c_str(),
                      tagspec.display);
            config.tags[tagspec.id] = tagspec;

        } else {
            LOG_DEBUG("Unknown function '%s'", token.c_str());
            wellFormatedLines.pop_back(); // remove incorrect line
        }
    }
    lines = wellFormatedLines;
    return config;
}

std::map<std::string, PredefinedView> PredefinedView::parsePredefinedViews(std::list<std::list<std::string> > lines)
{
    std::map<std::string, PredefinedView> pvs;
    std::list<std::list<std::string> >::iterator line;
    std::string token;
    FOREACH(line, lines) {
        token = pop(*line);
        if (token.empty()) continue;

        if (token == K_SMIT_VERSION) {
            std::string v = pop(*line);
            LOG_DEBUG("Smit version of view: %s", v.c_str());

        } else if (token == "addView") {
            PredefinedView pv;
            pv.name = pop(*line);
            if (pv.name.empty()) {
                LOG_ERROR("parsePredefinedViews: Empty view name. Skip.");
                continue;
            }

            while (! line->empty()) {
                token = pop(*line);
                if (token == "filterin" || token == "filterout") {
                    if (line->size() < 2) {
                        LOG_ERROR("parsePredefinedViews: Empty property or value for filterin/out");
                        pv.name.clear(); // invalidate this line
                        break;
                    }
                    std::string property = pop(*line);
                    std::string value = pop(*line);
                    if (token == "filterin") pv.filterin[property].push_back(value);
                    else pv.filterout[property].push_back(value);

                } else if (token == "default") {
                    pv.isDefault = true;

                } else if (line->size() == 0) {
                    LOG_ERROR("parsePredefinedViews: missing parameter after %s", token.c_str());
                    pv.name.clear(); // invalidate this line
                    break;

                } else if (token == "colspec") {
                    pv.colspec = pop(*line);

                } else if (token == "sort") {
                    pv.sort = pop(*line);

                } else if (token == "search") {
                    pv.search = pop(*line);

                } else {
                    LOG_ERROR("parsePredefinedViews: Unexpected token %s", token.c_str());
                    pv.name.clear();
                }
            }
            if (! pv.name.empty()) pvs[pv.name] = pv;

        } else {
            LOG_ERROR("parsePredefinedViews: Unexpected token %s", token.c_str());
        }
    }

    return pvs;
}


// @return 0 if OK, -1 on error
int Project::loadConfig(const char *path)
{
    LOG_FUNC();
    std::string pathToProjectFile = path;
    pathToProjectFile = pathToProjectFile + '/' + PROJECT_FILE;


    const char *buf = 0;
    int n = loadFile(pathToProjectFile.c_str(), &buf);

    if (n <= 0) return -1; // error or empty file

    std::list<std::list<std::string> > lines = parseConfigTokens(buf, n);

    free((void*)buf); // not needed any longer

    config = parseProjectConfig(lines);

    return 0;
}



int Project::modifyConfig(std::list<std::list<std::string> > &tokens)
{
    LOG_FUNC();
    ScopeLocker scopeLocker(lockerForConfig, LOCK_READ_WRITE);

    // verify the syntax of the tokens
    ProjectConfig c = parseProjectConfig(tokens);

    // keep unchanged the configuration items not managed via this modifyConfig
    c.predefinedViews = config.predefinedViews;
    c.tags = config.tags;
    c.numberIssueAcrossProjects = config.numberIssueAcrossProjects;

    // add version
    std::list<std::string> versionLine;
    versionLine.push_back(K_SMIT_VERSION);
    versionLine.push_back(VERSION);
    tokens.insert(tokens.begin(), versionLine);

    // at this point tagspecs are not managed by the web interface so they
    // are not in 'tokens'
    // add them now.
    std::map<std::string, TagSpec>::iterator t;
    std::list<std::string> line;
    FOREACH(t, config.tags) {
        line.push_back("tag");
        line.push_back(t->second.id);
        if (!t->second.label.empty()) {
            line.push_back("-label");
            line.push_back(t->second.label);
        }
        if (t->second.display) line.push_back("-display");
        tokens.push_back(line);
    }

    // serialize numbering policy (not managed by the web interface)
    if (config.numberIssueAcrossProjects) {
        line.clear();
        line.push_back("numberIssues");
        line.push_back("global");
        tokens.push_back(line);
    }

    // write to file
    std::string data = serializeTokens(tokens);

    std::string pathToProjectFile = path + '/';
    pathToProjectFile += PROJECT_FILE;
    int r = writeToFile(pathToProjectFile.c_str(), data);
    if (r != 0) {
        LOG_ERROR("Cannot write new config of project: %s", pathToProjectFile.c_str());
        return -1;
    }

    config = c;

    return 0;
}

/** get a predefined view
  * @return
  * if not found an object PredefinedView with empty name is returned
  */
PredefinedView Project::getPredefinedView(const std::string &name)
{
    ScopeLocker scopeLocker(lockerForConfig, LOCK_READ_ONLY);

    std::map<std::string, PredefinedView>::const_iterator pv;
    pv = config.predefinedViews.find(name);
    if (pv == config.predefinedViews.end()) return PredefinedView();
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

    ScopeLocker scopeLocker(lockerForConfig, LOCK_READ_WRITE);

    if (name == "_" && config.predefinedViews.count(pv.name)) {
        // reject creating a new view with the name of an existing view
        return -1;
    }

    // update default view
    if (pv.isDefault) {
        // set all others to non-default
        std::map<std::string, PredefinedView>::iterator i;
        FOREACH(i, config.predefinedViews) {
            i->second.isDefault = false;
        }
    }

    config.predefinedViews[pv.name] = pv;

    if (!name.empty() && name != pv.name && name != "_") {
        // it was a rename. remove old name
        std::map<std::string, PredefinedView>::iterator i = config.predefinedViews.find(name);
        if (i != config.predefinedViews.end()) config.predefinedViews.erase(i);
        else LOG_ERROR("Cannot remove old name of renamed view: %s -> %s", name.c_str(), pv.name.c_str());
    }

    // store to file
    return storeViewsToFile();
}


/**
  * No mutex handled in here.
  * Must be called from a mutexed scope (lockerForConfig)
  */
int Project::storeViewsToFile()
{
    std::string fileContents;
    fileContents = K_SMIT_VERSION " " VERSION "\n";

    std::map<std::string, PredefinedView>::const_iterator i;
    FOREACH(i, config.predefinedViews) {
        fileContents += i->second.serialize() + "\n";
    }
    std::string path = getPath() + '/' + VIEWS_FILE;
    return writeToFile(path.c_str(), fileContents);
}

int Project::deletePredefinedView(const std::string &name)
{
    if (name.empty()) return -1;
    ScopeLocker scopeLocker(lockerForConfig, LOCK_READ_WRITE);

    std::map<std::string, PredefinedView>::iterator i = config.predefinedViews.find(name);
    if (i != config.predefinedViews.end()) {
        config.predefinedViews.erase(i);
        return storeViewsToFile();

    } else {
        LOG_ERROR("Cannot delete view: %s", name.c_str());
        return -1;
    }
}


PredefinedView Project::getDefaultView()
{
    ScopeLocker scopeLocker(lockerForConfig, LOCK_READ_ONLY);
    std::map<std::string, PredefinedView>::const_iterator i;
    FOREACH(i, config.predefinedViews) {
        if (i->second.isDefault) return i->second;
    }
    return PredefinedView(); // empty name indicates no default view found
}

/** Create the directory and files for a new project
  */
int Project::createProjectFiles(const char *repositoryPath, const char *projectName, std::string &resultingPath)
{
    if (! projectName || strlen(projectName) == 0) {
        LOG_ERROR("Cannot create project with empty name");
        return -1;
    }

    resultingPath = std::string(repositoryPath) + "/" + Project::urlNameEncode(projectName);
    std::string path = resultingPath;
    int r = mg_mkdir(path.c_str(), S_IRUSR | S_IXUSR | S_IWUSR);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", path.c_str(), strerror(errno));
        return -1;
    }

    // create file 'project'
    const char* config =
            K_SMIT_VERSION " " VERSION "\n"
            "setPropertyLabel id \"#\"\n"
            "addProperty status select open closed deleted\n"
            "addProperty owner selectUser\n"
            ;
    std::string subpath = path  + "/" + PROJECT_FILE;
    r = writeToFile(subpath.c_str(), config);
    if (r != 0) {
        LOG_ERROR("Could not create file '%s': %s", subpath.c_str(), strerror(errno));
        return r;
    }

    // create file 'views'
    const char* views =
            "addView \"All Issues\" sort status-mtime+id\n"
            "addView \"My Issues\" filterin owner me sort status-mtime+id\n"
            ;
    subpath = path  + "/" + VIEWS_FILE;
    r = writeToFile(subpath.c_str(), views);
    if (r != 0) {
        LOG_ERROR("Could not create file '%s': %s", subpath.c_str(), strerror(errno));
        return r;
    }

    // create directory 'issues'
    subpath = path + "/" + ISSUES;
    r = mg_mkdir(subpath.c_str(), S_IRUSR | S_IXUSR | S_IWUSR);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
    }

    // create directory 'html'
    subpath = path + "/html";
    r = mg_mkdir(subpath.c_str(), S_IRUSR | S_IXUSR | S_IWUSR);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
    }

    // create directory 'tmp'
    subpath = path + "/tmp";
    r = mg_mkdir(subpath.c_str(), S_IRUSR | S_IXUSR | S_IWUSR);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
    }

    // create directory 'files'
    subpath = path + "/files";
    r = mg_mkdir(subpath.c_str(), S_IRUSR | S_IXUSR | S_IWUSR);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
    }


    return 0;
}

/** Tag or untag an entry
  *
  */
int Project::toggleTag(const std::string &issueId, const std::string &entryId, const std::string &tagid)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);

    Issue *i = getIssue(issueId);
    if (!i) {
        // issue not found
        LOG_DEBUG("Issue not found: %s", issueId.c_str());
        return -1;
    } else {
        std::map<std::string, Entry*>::iterator eit;
        eit = i->entries.find(entryId);
        if (eit == i->entries.end()) {
            LOG_DEBUG("Entry not found: %s/%s", issueId.c_str(), entryId.c_str());
            return -1;
        }
        Entry *e = eit->second;

        // invert the tag
        std::set<std::string>::iterator tag = e->tags.find(tagid);
        if (tag == e->tags.end()) e->tags.insert(tagid);
        else e->tags.erase(tag);

        tag = e->tags.find(tagid); // update tag status after inversion

        // store to disk
        std::string path = getPath() + "/" TAGS_DIR "/";

        int r;
        if (tag != e->tags.end()) {
            // create sub directories every time (not optimum)
            mg_mkdir(path.c_str(), S_IRUSR | S_IXUSR | S_IWUSR);
            path += issueId;
            mg_mkdir(path.c_str(), S_IRUSR | S_IXUSR | S_IWUSR);
            path += "/" + entryId;
            path += "." + tagid;

            // create the file
            r = writeToFile(path.c_str(), "");

        } else {
            // remove the file
            path += issueId + "/" + entryId + "." + tagid;
            r = unlink(path.c_str());
        }

        return r;
    }
}

uint32_t Project::allocateNewIssueId()
{
    if (config.numberIssueAcrossProjects) return Database::allocateNewIssueId();

    maxIssueId++;
    if (maxIssueId == 0) LOG_ERROR("Project: max issue id zero: wrapped");
    return maxIssueId;
}

void Project::updateMaxIssueId(uint32_t i)
{
    if (config.numberIssueAcrossProjects) return Database::updateMaxIssueId(i);

    if (i > maxIssueId) maxIssueId = i;
}


void Project::loadPredefinedViews(const char *projectPath)
{
    LOG_FUNC();

    std::string path = projectPath;
    path += '/';
    path += VIEWS_FILE;

    const char *buf = 0;
    int n = loadFile(path.c_str(), &buf);

    if (n > 0) {

        std::list<std::list<std::string> > lines = parseConfigTokens(buf, n);

        free((void*)buf); // not needed any longer

        config.predefinedViews = PredefinedView::parsePredefinedViews(lines);
    } // else error of empty file

    LOG_DEBUG("predefined views loaded: %d", config.predefinedViews.size());
}

/** Look for tags: files <project>/tags/<issue>/<entry>
  *
  */
void Project::loadTags(const char *projectPath)
{
    std::string path = projectPath;
    path += "/" TAGS_DIR "/";
    DIR *tagsDirHandle;
    if ((tagsDirHandle = opendir(path.c_str())) == NULL) {
        LOG_DEBUG("No tags directory '%s'", path.c_str());
        return;
    }

    struct dirent *issueDir;
    while ((issueDir = readdir(tagsDirHandle)) != NULL) {
        if (0 == strcmp(issueDir->d_name, ".")) continue;
        if (0 == strcmp(issueDir->d_name, "..")) continue;

        std::string issueId = issueDir->d_name;
        // get the issue object
        Issue *i = getIssue(issueId);
        if (!i) {
            LOG_ERROR("Tags for unknown issue: '%s'", issueId.c_str());
            continue;
        }

        std::string issuePath = path;
        issuePath += "/" + issueId;
        // open this subdir and look for all files of this subdir
        DIR *issueDirHandle;
        if ((issueDirHandle = opendir(issuePath.c_str())) == NULL) continue; // not a directory
        else {
            struct dirent *entryTag;
            while ((entryTag = readdir(issueDirHandle)) != NULL) {
                if (0 == strcmp(entryTag->d_name, ".")) continue;
                if (0 == strcmp(entryTag->d_name, "..")) continue;

                std::string tag = entryTag->d_name;
                std::string entryId = popToken(tag, '.');
                std::map<std::string, Entry*>::iterator eit;
                eit = i->entries.find(entryId);
                if (eit == i->entries.end()) {
                    LOG_DEBUG("Tags for unknown entry: %s/%s", issueId.c_str(), entryId.c_str());
                    continue;
                }
                Entry *e = eit->second;
                if (tag.empty()) tag = "tag"; // keep compatibility with version <= 1.2.x

                e->tags.insert(tag);
            }
            closedir(issueDirHandle);
        }
    }
    closedir(tagsDirHandle);
}


std::string Project::getLabelOfProperty(const std::string &propertyName) const
{
    ScopeLocker scopeLocker(lockerForConfig, LOCK_READ_ONLY);

    std::string label;
    std::map<std::string, std::string> labels = config.propertyLabels;
    std::map<std::string, std::string>::const_iterator l;
    l = labels.find(propertyName);
    if (l != labels.end()) label = l->second;

    if (label.size()==0) label = propertyName;
    return label;
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

class IssueComparator {
public:
    IssueComparator(const std::list<std::pair<bool, std::string> > &sSpec) : sortingSpec(sSpec) { }
    bool operator() (const Issue* i, const Issue* j) { return i->lessThan(j, sortingSpec); }
private:
    const std::list<std::pair<bool, std::string> > &sortingSpec;
};

/**
  * sortingSpec: a list of pairs (ascending-order, property-name)
  *
  */
void sort(std::vector<Issue*> &inout, const std::list<std::pair<bool, std::string> > &sortingSpec)
{
    if (sortingSpec.size()==0) return;

    IssueComparator ic(sortingSpec);
    std::sort(inout.begin(), inout.end(), ic);
}

enum FilterSearch {
    PROPERTY_NOT_FILTERED,
    PROPERTY_FILTERED_FOUND,
    PROPERTY_FILTERED_NOT_FOUND
};

/** Look if the given multi-valued property is present in the given list
  */
bool isPropertyInFilter(const std::list<std::string> &propertyValue,
                        const std::list<std::string> &filteredValues)
{
    std::list<std::string>::const_iterator fv;
    std::list<std::string>::const_iterator v;

    FOREACH (fv, filteredValues) {
        FOREACH (v, propertyValue) {
            if (*v == *fv) return PROPERTY_FILTERED_FOUND;
        }
        if (fv->empty() && propertyValue.empty()) {
            // allow filtering for empty values
            return true;
        }
    }
    return false; // not found
}

/** Look if the given property is present in the given list
  */
bool isPropertyInFilter(const std::string &propertyValue,
                        const std::list<std::string> &filteredValues)
{
    std::list<std::string>::const_iterator fv;

    FOREACH (fv, filteredValues) if (propertyValue == *fv) return true;

    return false; // not found
}


/**
  * @return
  *    true, if the issue should be kept
  *    false, if the issue should be excluded
  */
bool Issue::isInFilter(const std::map<std::string, std::list<std::string> > &filter)
{
    if (filter.empty()) return false;

    // look for each property of the issue (except ctime and mtime)

    std::map<std::string, std::list<std::string> >::const_iterator f;
    FOREACH(f, filter) {
        std::string filteredProperty = f->first;

        if (filteredProperty == K_ISSUE_ID) {
            // id
            if (isPropertyInFilter(id, f->second)) return true;

        } else {
            std::map<std::string, std::list<std::string> >::const_iterator p;
            p = properties.find(filteredProperty);
            bool fs;
            if (p == properties.end()) fs = isPropertyInFilter("", f->second);
            else fs = isPropertyInFilter(p->second, f->second);

            if (fs) return true;
        }
    }
    return false;
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
std::vector<Issue*> Project::search(const char *fulltextSearch,
                                    const std::map<std::string, std::list<std::string> > &filterIn,
                                    const std::map<std::string, std::list<std::string> > &filterOut,
                                    const char *sortingSpec)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_ONLY);

    // General algorithm:
    // For each issue:
    //     1. keep only those specified by filterIn and filterOut
    //     2. then, if fulltext is not null, walk through these issues and their
    //        related messages and keep those that contain <fulltext>
    //     3. then, do the sorting according to <sortingSpec>
    std::vector<struct Issue*> result;

    std::map<std::string, Issue*>::iterator i;
    for (i=issues.begin(); i!=issues.end(); i++) {

        Issue* issue = i->second;
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

/** Compare two values of the given property
  * @return -1, 0, +1
  */
int compareProperties(const std::map<std::string, std::list<std::string> > &plist1,
                      const std::map<std::string, std::list<std::string> > &plist2,
                      const std::string &name)
{
    std::map<std::string, std::list<std::string> >::const_iterator p1 = plist1.find(name);
    std::map<std::string, std::list<std::string> >::const_iterator p2 = plist2.find(name);

    if (p1 == plist1.end() && p2 == plist2.end()) return 0;
    else if (p1 == plist1.end()) return -1; // arbitrary choice
    else if (p2 == plist2.end()) return +1; // arbitrary choice
    else {
        std::list<std::string>::const_iterator v1 = p1->second.begin();
        std::list<std::string>::const_iterator v2 = p2->second.begin();
        while (v1 != p1->second.end() && v2	!= p2->second.end()) {
            int lt = v1->compare(*v2);
            if (lt < 0) return -1;
            else if (lt > 0) return +1;
            // else continue
            v1++;
            v2++;
        }
        if (v1 == p1->second.end() && v2 == p2->second.end()) {
            return 0; // they are equal
        } else if (v1 == p1->second.end()) return -1; // arbitrary choice
        else return +1; // arbitrary choice
    }
    return 0; // not reached normally
}

/** Compare 2 issues after sortingSpec.
  *
  * sortingSpec: a list of pairs (ascending-order, property-name)
  *
  * @return
  *     true or false
  *     If they are equal, false is returned.
  */
bool Issue::lessThan(const Issue* other, const std::list<std::pair<bool, std::string> > &sortingSpec) const
{
    if (!other) return false;

    int result = 0; // 0 means equal, <0 means less-than, >0 means greater-than
    std::list<std::pair<bool, std::string> >::const_iterator s = sortingSpec.begin();

    while ( (result == 0) && (s != sortingSpec.end()) ) {
        // case of id, ctime, mtime
        if (s->second == "id") {
            if (id == other->id) result = 0;
            else if (atoi(id.c_str()) < atoi(other->id.c_str())) result = -1;
            else result = +1;

        } else if (s->second == "ctime") {
            if (ctime < other->ctime) result = -1;
            else if (ctime > other->ctime) result = +1;
            else result = 0;
        } else if (s->second == "mtime") {
            if (mtime < other->mtime) result = -1;
            else if (mtime > other->mtime) result = +1;
            else result = 0;
        } else {
            // the other properties

            result = compareProperties(properties, other->properties, s->second);
        }
        if (!s->first) result = -result; // descending order
        s++;
    }
    if (result<0) return true;
    else return false;
}


/** Search for the given text through the issue properties
  * and the messages of the entries.
  *
  * @return
  *     true if found, false otherwise
  *
  */
bool Issue::searchFullText(const char *text) const
{
    if (!text) return true;

    // look if id contains the fulltextSearch
    if (mg_strcasestr(id.c_str(), text)) return true; // found

    // look through the properties of the issue
    std::map<std::string, std::list<std::string> >::const_iterator p;
    for (p = properties.begin(); p != properties.end(); p++) {
        std::list<std::string>::const_iterator pp;
        std::list<std::string> listOfValues = p->second;
        for (pp = listOfValues.begin(); pp != listOfValues.end(); pp++) {
            if (mg_strcasestr(pp->c_str(), text)) return true;  // found
        }
    }

    // look through the entries
    Entry *e = latest;
    while (e) {
        if (mg_strcasestr(e->getMessage().c_str(), text)) return true; // found

        // look through uploaded files
        std::map<std::string, std::list<std::string> >::const_iterator files = e->properties.find(K_FILE);
        if (files != e->properties.end()) {
            std::list<std::string>::const_iterator f;
            FOREACH(f, files->second) {
                if (mg_strcasestr(f->c_str(), text)) return true; // found
            }
        }

        e = e->prev;
    }

    return false; // text not found

}

int Issue::getNumberOfTaggedIEntries(const std::string &tagId) const
{
    Entry *e = latest;
    int n = 0;
    while (e) {
        if (e->tags.find(tagId) != e->tags.end()) n++;
        e = e->prev;
    }
    return n;
}

/** If issueId is empty:
  *     - a new issue is created
  *     - its ID is returned within parameter 'issueId'
  */
int Project::addEntry(std::map<std::string, std::list<std::string> > properties, std::string &issueId, std::string &entryId, std::string username)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);
    ScopeLocker scopeLockerConfig(lockerForConfig, LOCK_READ_ONLY);

    Issue *i = 0;
    if (issueId.size()>0) {
        // adding an entry to an existing issue

        i = getIssue(issueId);
        if (!i) {
            LOG_INFO("Cannot add new entry to unknown issue: %s", issueId.c_str());
            return -1;
        }

        // simplify the entry by removing properties that have the same value
        // in the issue (only the modified fields are stored)
        //
        // check that all properties are in the project config
        // and that they bring a modification of the issue
        // else, remove them
        std::map<std::string, std::list<std::string> >::iterator entryProperty;
        entryProperty = properties.begin();
        while (entryProperty != properties.end()) {
            bool doErase = false;

            if ( (entryProperty->first == K_MESSAGE) || (entryProperty->first == K_FILE) )  {
                if (entryProperty->second.size() && entryProperty->second.front().empty()) {
                    // erase if message or file is emtpy
                    doErase = true;
                }
            } else {
                const PropertySpec *pspec = config.getPropertySpec(entryProperty->first);
                if ( !pspec && (entryProperty->first != K_SUMMARY) ) {
                    // erase property because it is not part of the official fields of the project
                    doErase = true;
                } else {
                    std::map<std::string, std::list<std::string> >::iterator issueProperty;
                    issueProperty = i->properties.find(entryProperty->first);
                    if (issueProperty != i->properties.end()) {
                        if (issueProperty->second == entryProperty->second) {
                            // the value of this property has not changed
                            doErase = true;
                        }
                    }
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
        if (properties.size() == 0) {
            LOG_INFO("addEntry: no change. return without adding entry.");
            return 0; // no change
        }
    }

    Entry *parent;
    if (i) parent = i->latest;
    else parent = 0;

    // create Entry object with properties
    Entry *e = new Entry;
    if (parent) e->parent = parent->id;
    else e->parent = K_PARENT_NULL;
    e->ctime = time(0);
    //e->id
    e->author = username;
    e->properties = properties;

    if (i) {
        e->prev = parent;
        parent->next = e;
    }


    // serialize the entry
    std::string data = e->serialize();

    // generate a id for this entry
    std::string newEntryId = getSha1(data.c_str(), data.size());

    LOG_DEBUG("new entry: %s", newEntryId.c_str());

    std::string pathOfNewEntry;
    std::string pathOfIssue;

    // write this entry to disk
    // if issueId is empty, generate a new issueId
    if (issueId.empty()) {
        // create new directory for this issue

        const int SIZ = 25;
        char buffer[SIZ];
        int newId = allocateNewIssueId();
        snprintf(buffer, SIZ, "%d", newId);
        issueId = buffer;

        pathOfIssue = path + '/' + ISSUES + '/' + issueId;

        int r = mg_mkdir(pathOfIssue.c_str(), S_IRUSR | S_IXUSR | S_IWUSR);
        if (r != 0) {
            LOG_ERROR("Could not create dir '%s': %s", pathOfIssue.c_str(), strerror(errno));

            return -1;
        }
        i = new Issue();
        i->ctime = e->ctime;
        i->id = issueId;

        // add it to the internal memory
        issues[issueId] = i;

    } else {
        // check that this entry ID does not exist
        if (i->entries.find(newEntryId) != i->entries.end()) {
            LOG_ERROR("Entry with same id already exists: %s", newEntryId.c_str());
            return -1;
        }
        pathOfIssue = path + '/' + ISSUES + '/' + issueId;
    }

    pathOfNewEntry = pathOfIssue + '/' + newEntryId;
    int r = writeToFile(pathOfNewEntry.c_str(), data);
    if (r != 0) {
        // error.
        LOG_ERROR("Could not write new entry to disk");
        return r;
    }

    // add this entry in Project::entries
    e->id = newEntryId;
    i->entries[newEntryId] = e;

    // consolidate the issue
    i->consolidateIssueWithSingleEntry(e, true);
    i->mtime = e->ctime;
    i->latest = e;

    // move the uploaded files (if any)
    std::map<std::string, std::list<std::string> >::iterator files = e->properties.find(K_FILE);
    if (files != e->properties.end()) {
        std::string dir = path + "/" + K_UPLOADED_FILES_DIR;
        mg_mkdir(dir.c_str(), S_IRUSR | S_IWUSR | S_IXUSR); // create dir if needed

        std::list<std::string>::iterator f;
        FOREACH(f, files->second) {
            std::string oldpath = path + "/tmp/" + *f;
            std::string newpath = dir + "/" + *f;

            if (access(newpath.c_str(), F_OK ) != -1 ) {
                // destination file already exists
                // file already uploaded (or SHA1 collision)
                // do nothing, and erase temporary file
                LOG_INFO("File '%s' already uploaded. Ignore new upload of this file.", f->c_str());
                unlink(oldpath.c_str());

            } else {
                // move the file from tmp to persistent directory
                int r = rename(oldpath.c_str(), newpath.c_str());
                if (r != 0) {
                    LOG_ERROR("Cannot move file '%s' -> '%s': %s", oldpath.c_str(), newpath.c_str(), strerror(errno));
                }
            }
        }
    } // end of processing of uploaded files

    entryId = newEntryId;
    return r;
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
  *
  */

int Project::deleteEntry(const std::string &issueId, const std::string &entryId, const std::string &username)
{
    ScopeLocker(locker, LOCK_READ_WRITE);

    Issue *i = getIssue(issueId);
    if (!i) return -6;

    std::map<std::string, Entry*>::iterator ite;
    ite = i->entries.find(entryId);
    if (ite == i->entries.end()) return -1;

    Entry *e = ite->second;

    if (time(0) - e->ctime > DELETE_DELAY_S) return -2;
    else if (e->parent == K_PARENT_NULL) return -3;
    else if (e->author != username) return -4;
    else if (i->latest != e) return -7;

    // ok, we can delete this entry
    // modify pointers
    i->latest = e->prev;
    i->latest->next = 0;
    e->prev = 0;
    e->next = 0;

    i->consolidate();

    // remove from internal tables
    i->entries.erase(ite);
    // do not really delete the entry, because we do not
    // have any mutex for this (a HTML page may be wanting to
    // display an erased entry at the same time)
    // this is a small memory leak, but acceptable I suppose
    //delete e;

    // move the file to _del
    std::string issuePath = getPath() + "/" + ISSUES + "/" + issueId;
    std::string deletePath = issuePath + "/" + K_DELETED;
    mg_mkdir(deletePath.c_str(), S_IRUSR | S_IWUSR | S_IXUSR); // create it if not already done
    deletePath += "/" + entryId;
    std::string entryPath = issuePath + "/" + entryId;
    int r = rename(entryPath.c_str(), deletePath.c_str());
    if (r != 0) {
        LOG_ERROR("Cannot rename '%s' -> '%s': (%d) %s", entryPath.c_str(), deletePath.c_str(),
                  errno, strerror(errno));
        return -1;
    }

    return 0;
}

std::string Entry::getMessage()
{
    return getProperty(properties, K_MESSAGE);
}

int Entry::getCtime() const
{
    return ctime;
}


std::string Entry::serialize()
{
    std::ostringstream s;

    s << K_SMIT_VERSION << " " << VERSION << "\n";
    s << K_PARENT << " " << parent << "\n";
    s << K_AUTHOR << " " << author << "\n";
    s << K_CTIME << " " << ctime << "\n";

    std::map<std::string, std::list<std::string> >::iterator p;
    for (p = properties.begin(); p != properties.end(); p++) {
        std::string key = p->first;
        std::list<std::string> value = p->second;
        s << serializeProperty(key, value);
    }
    return s.str();
}


Project *Database::createProject(const std::string &name)
{
    std::string resultingPath;
    int r = Project::createProjectFiles(Db.getRootDir().c_str(), name.c_str(), resultingPath);
    if (r != 0) return 0;

    Project *p = loadProject(resultingPath.c_str());
    return p;
}

Project *Database::loadProject(const char *path)
{
    Project *p = Project::load(path);
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


uint32_t Database::allocateNewIssueId()
{
    ScopeLocker scopeLocker(Db.locker, LOCK_READ_WRITE);

    Db.maxIssueId++;
    if (Db.maxIssueId == 0) LOG_ERROR("Database: max issue id zero: wrapped");
    LOG_DEBUG("allocateNewIssueId: %d", Db.maxIssueId);
    return Db.maxIssueId;
}

void Database::updateMaxIssueId(uint32_t i)
{
    if (i > Db.maxIssueId) Db.maxIssueId = i;
}



std::string PredefinedView::getDirectionName(bool d)
{
    return d?_("Ascending"):_("Descending");
}

std::string PredefinedView::getDirectionSign(const std::string &text)
{
    if (text == _("Ascending")) return "+";
    else return "-";
}


/** Generate a query string
  *
  * Example: filterin=x:y&filterout=a:b&search=ss&sort=s22&colspec=a+b+c
  */
std::string PredefinedView::generateQueryString() const
{
    std::string qs = "";
    if (!search.empty()) qs += "search=" + urlEncode(search) + '&';
    if (!sort.empty()) qs += "sort=" + sort + '&';
    if (!colspec.empty()) qs += "colspec=" + colspec + '&';
    std::map<std::string, std::list<std::string> > ::const_iterator f;
    FOREACH(f, filterin) {
        std::list<std::string>::const_iterator v;
        FOREACH(v, f->second) {
            qs += "filterin=" + urlEncode(f->first) + ':' + urlEncode(*v) + '&';
        }
    }
    FOREACH(f, filterout) {
        std::list<std::string>::const_iterator v;
        FOREACH(v, f->second) {
            qs += "filterout=" + urlEncode(f->first) + ':' + urlEncode(*v) + '&';
        }
    }

    // remove latest &
    if (!qs.empty() && qs[qs.size()-1] == '&') qs = qs.substr(0, qs.size()-1);
    return qs;
}

std::string PredefinedView::serialize() const
{
    std::string out;

    out += "addView " + serializeSimpleToken(name) + " \\\n";
    if (isDefault) out += "    default \\\n";

    std::map<std::string, std::list<std::string> >::const_iterator f;
    FOREACH(f, filterin) {
        std::list<std::string>::const_iterator i;
        FOREACH(i, f->second) {
            out += "    filterin " + serializeSimpleToken(f->first) + " ";
            out += serializeSimpleToken(*i) + " \\\n";
        }
    }

    FOREACH(f, filterout) {
        std::list<std::string>::const_iterator i;
        FOREACH(i, f->second) {
            out += "    filterout " + serializeSimpleToken(f->first) + " ";
            out += serializeSimpleToken(*i) + " \\\n";
        }
    }

    if (!sort.empty()) out += "    sort " + serializeSimpleToken(sort) + " \\\n";
    if (!colspec.empty()) out += "    colspec " + serializeSimpleToken(colspec) + " \\\n";
    if (!search.empty()) out += "    search " + serializeSimpleToken(search) + "\n";

    return out;
}
