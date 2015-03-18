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
#define K_MERGE_PENDING ".merge-pending";

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

std::list<std::string> ProjectConfig::getReservedProperties()
{
    std::list<std::string> reserved;
    reserved.push_back("id");
    reserved.push_back("ctime");
    reserved.push_back("mtime");
    reserved.push_back("summary");
    return reserved;
}

bool ProjectConfig::isReservedProperty(const std::string &name)
{
    std::list<std::string> reserved = getReservedProperties();
    std::list<std::string>::iterator i = std::find(reserved.begin(), reserved.end(), name);
    if (i == reserved.end()) return false;
    else return true;
}

std::string ProjectConfig::getLabelOfProperty(const std::string &propertyName) const
{
    std::string label;
    std::map<std::string, std::string>::const_iterator l;
    l = propertyLabels.find(propertyName);
    if (l != propertyLabels.end()) label = l->second;

    if (label.size()==0) label = propertyName;
    return label;
}

std::string ProjectConfig::getReverseLabelOfProperty(const std::string &propertyName) const
{
    std::string reverseLabel;
    std::map<std::string, std::string>::const_iterator rlabel;
    rlabel = propertyReverseLabels.find(propertyName);
    if (rlabel != propertyReverseLabels.end()) reverseLabel = rlabel->second;

    if (reverseLabel.size()==0) reverseLabel = propertyName;
    return reverseLabel;
}

bool ProjectConfig::isValidPropertyName(const std::string &name) const
{
    // get user defined properties
    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, properties) {
        if (pspec->name == name) return true;
    }

    // look in reserved properties
    std::list<std::string> reserved = getReservedProperties();
    std::list<std::string>::const_iterator p;
    FOREACH(p, reserved) {
        if ((*p) == name) return true;
    }
    return false;

}
/** Check if a name is valid for a project
  *
  * Characters \r and \n are forbidden
  */
bool ProjectConfig::isValidProjectName(const std::string &name)
{
    std::size_t found = name.find_first_of("\r\n");
    if (found != std::string::npos) return false;
    return true;
}

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

Project *Project::init(const char *path)
{
    Project *p = new Project;
    LOG_DEBUG("Loading project %s (%p)...", path, p);

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


        Entry *e = issue->latest;
        while (e) {
            std::map<std::string, Entry*>::const_iterator otherEntry = entries.find(e->id);
            if (otherEntry != entries.end()) {
                LOG_ERROR("duplicate entry (merge not complete ?) TODO");
                // TODO
            }
            entries[e->id] = e; // store in the global table
            e = e->prev;
        }

        // update the maximum id
        int intId = atoi(issueId.c_str());
        if (intId > 0 && (uint32_t)intId > localMaxId) localMaxId = intId;

        // store the issue in memory
        issue->id = issueId;
        issues[issue->id] = issue;
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
    else if (s == "association") out = F_ASSOCIATION;

    else return -1; // error


    return 0; // ok
}



PropertySpec parsePropertySpec(std::list<std::string> & tokens)
{
    // Supported syntax:
    // name [label <label>] type params ...
    // type = text | select | multiselect | selectUser | ...
    PropertySpec pspec;
    if (tokens.size() < 2) {
        LOG_ERROR("Not enough tokens");
        return pspec; // error, indicated to caller by empty name of pspec
    }

    pspec.name = tokens.front();
    // check that property name contains only [a-zA-Z0-9-_]
    const char* allowedInPropertyName = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    if (pspec.name.find_first_not_of(allowedInPropertyName) != std::string::npos) {
        // invalid character
        LOG_ERROR("Invalid property name: %s", pspec.name.c_str());
        pspec.name = "";
        return pspec;
    }
    tokens.pop_front();

    if (tokens.empty()) {
        LOG_ERROR("Not enough tokens");
        pspec.name = "";
        return pspec; // error, indicated to caller by empty name of property
    }

    // look for optional -label parameter
    if (tokens.front() == "-label") {
        tokens.pop_front();
        if (tokens.empty()) {
            LOG_ERROR("Not enough tokens (-label)");
            pspec.name = "";
            return pspec; // error, indicated to caller by empty name of property
        }
        pspec.label = tokens.front();
        tokens.pop_front();
    }

    if (tokens.empty()) {
        LOG_ERROR("Not enough tokens");
        pspec.name = "";
        return pspec; // error, indicated to caller by empty name of property
    }
    std::string type = tokens.front();
    tokens.pop_front();
    int r = strToPropertyType(type, pspec.type);
    if (r < 0) { // error, unknown type
        LOG_ERROR("Unkown property type '%s'", type.c_str());
        pspec.name.clear();
        return pspec; // error, indicated to caller by empty name of property
    }

    if (F_SELECT == pspec.type || F_MULTISELECT == pspec.type) {
        // populate the allowed values
        while (tokens.size() > 0) {
            std::string value = tokens.front();
            tokens.pop_front();
            pspec.selectOptions.push_back(value);
        }
    } else if (F_ASSOCIATION == pspec.type) {
        if (tokens.size() > 1) {
            // expect -reverseLabel
            if (tokens.front() == "-reverseLabel") {
                tokens.pop_front();
                pspec.reverseLabel = tokens.front();
                tokens.pop_front();
            }
        }
    }
    return pspec;
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
                if (! property.reverseLabel.empty()) config.propertyReverseLabels[property.name] = property.reverseLabel;

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


// @return 0 if OK, -1 on error
int Project::loadConfig()
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
    c.numberIssueAcrossProjects = config.numberIssueAcrossProjects;

    // add version
    std::list<std::string> versionLine;
    versionLine.push_back(K_SMIT_VERSION);
    versionLine.push_back(VERSION);
    tokens.insert(tokens.begin(), versionLine);

    // at this point numbering policy is not managed by the web interface so they
    // are not in 'tokens'
    // serialize numbering policy (not managed by the web interface)
    if (config.numberIssueAcrossProjects) {
        std::list<std::string> line;
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


PredefinedView Project::getDefaultView() const
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

    // create directory 'objects'
    subpath = path + "/" + PATH_OBJECTS;
    r = mkdir(subpath);
    if (r != 0) {
        LOG_ERROR("Could not create directory '%s': %s", subpath.c_str(), strerror(errno));
        return -1;
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

    // create directory 'refs'
    subpath = path + "/refs";
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
        eit = entries.find(entryId);
        if (eit == entries.end()) {
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
        std::string path = getPath() + "/" PATH_TAGS "/";

        int r;
        if (tag != e->tags.end()) {
            // create sub directories every time (not optimum)
            mkdir(path);
            path += issueId;
            mkdir(path);
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
    LOG_INFO("Reloading project '%s'...", getName().c_str());
    ScopeLocker L1(locker, LOCK_READ_WRITE);
    ScopeLocker L2(lockerForConfig, LOCK_READ_WRITE);

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

    const char *buf = 0;
    int n = loadFile(viewsPath.c_str(), &buf);

    if (n > 0) {

        std::list<std::list<std::string> > lines = parseConfigTokens(buf, n);

        free((void*)buf); // not needed any longer

        config.predefinedViews = PredefinedView::parsePredefinedViews(lines);
    } // else error of empty file

    LOG_DEBUG("predefined views loaded: %ld", L(config.predefinedViews.size()));
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

int Project::insertIssue(Issue *i)
{
    LOG_FUNC();
    if (!i) {
        LOG_ERROR("Cannot insert null issue in project");
        return -1;
    }

    LOG_DEBUG("insertIssue %s", i->id.c_str());

    std::map<std::string, Issue*>::const_iterator existingIssue;
    existingIssue = issues.find(i->id);
    if (existingIssue != issues.end()) {
        LOG_ERROR("Cannot insert issue %s: already in database", i->id.c_str());
        return -2;
    }

    // add the issue in the table
    issues[i->id] = i;
    return 0;
}

int Project::storeRefIssue(const std::string &issueId, const std::string &entryId)
{
    LOG_DEBUG("storeRefIssue: %s -> %s", issueId.c_str(), entryId.c_str());
    std::string issuePath = path + "/" PATH_ISSUES "/" + issueId;
    int r = writeToFile(issuePath.c_str(), entryId);
    if (r!=0) {
        LOG_ERROR("Cannot store issue %s", issueId.c_str());
    }
    return r;
}

/** Rename an issue (take the next available id)
  */
std::string Project::renameIssue(const std::string &oldId)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);
    //
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
    // add the issue in the table
    issues[newId] = &i;

    // delete the old slot
    issues.erase(i.id);

    std::string oldId = i.id;
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

/** get the next issue
  *
  * This method is a helper for iterating over the issues of the project.
  */
Issue *Project::getNextIssue(Issue *i)
{
    std::map<std::string, Issue*>::iterator it;
    if (!i) {
        // get the first issue
        it = issues.begin();
    } else {
        // get the next after the given issue
        it = issues.find(i->id);

        if (it == issues.end()) return 0; // this should not happen

        it++;
    }

    if (it == issues.end()) return 0;
    else return it->second;
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
    std::vector<std::string> issueIds = split(v, " ,;");
    std::vector<std::string>::const_iterator associatedIssue;
    FOREACH(associatedIssue, issueIds) {
        if (associatedIssue->empty()) continue; // because split may return empty tokens
        values.push_back(*associatedIssue);
    }

    values.sort();
}

/** Remove "" values from list of multiselect values if "" is not in the allowed range
  *
  * This is because the HTML form adds a hidden input with empty value.
  */
void Project::cleanupMultiselect(std::list<std::string> &values, const std::list<std::string> &selectOptions)
{
    // convert to a set
    std::set<std::string> allowed(selectOptions.begin(), selectOptions.end());
    std::list<std::string>::iterator v = values.begin();
    bool gotEmpty = false;
    while (v != values.end()) {
        if ( (allowed.find(*v) == allowed.end()) || (gotEmpty && v->empty()) ) {
            // erase currect item
            std::list<std::string>::iterator v0 = v;
            v++;
            values.erase(v0);
        } else {
            if (v->empty()) gotEmpty = true; // empty is allowed. but we don't w<ant a second one
            v++;
        }
    }
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
    std::string pathOfNewEntry = getObjectsDir() + "/" + e->getSubpath();
    if (fileExists(pathOfNewEntry)) {
        LOG_ERROR("Cannot create new entry as object already exists: %s", pathOfNewEntry.c_str());
        return -1;
    }

    std::string subdir = getObjectsDir() + "/" + e->getSubdir();
    mkdirs(subdir); // create dir if needed

    int r = writeToFile(pathOfNewEntry.c_str(), e->serialize());
    if (r != 0) {
        // error.
        LOG_ERROR("Could not write new entry to disk");
        return -2;
    }
    return 0;
}

/** If issueId is empty:
  *     - a new issue is created
  *     - its ID is returned within parameter 'issueId'
  * @return
  *     0 if no error. The entryId is fullfilled.
  *       except if no entry was created due to no change.
  */
int Project::addEntry(PropertiesMap properties, std::string &issueId, std::string &entryId, std::string username)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);
    ScopeLocker scopeLockerConfig(lockerForConfig, LOCK_READ_ONLY);

    entryId.clear();

    // check that all properties are in the project config
    // else, remove them
    // and parse the associations, if any
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
            else if (pspec && pspec->type == F_MULTISELECT) cleanupMultiselect(p->second, pspec->selectOptions);
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

        // simplify the entry by removing properties that have the same value
        // in the issue (only the modified fields are stored)
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

        if (properties.size() == 0) {
            LOG_INFO("addEntry: no change. return without adding entry.");
            return 0; // no change
        }
    }
    // at this point properties have been cleaned up

    FOREACH(p, properties) {
        LOG_DEBUG("properties: %s => %s", p->first.c_str(), join(p->second, ", ").c_str());
    }

    // write the entry to disk

    // if issueId is empty, create a new issue
    if (!i) {
        i = createNewIssue();
        if (!i) return -1;
        issueId = i->id; // in/out parameter
    }

    // create the new entry object
    Entry *e = Entry::createNewEntry(properties, username, i->latest);

    // check that this entry ID does not already exist in memory
    if (entries.find(e->id) != entries.end()) {
        LOG_ERROR("Entry with same id already exists: %s", e->id.c_str());
        return -1;
    }

    int r = storeEntry(e);
    if (r<0) return r;

	// update latest entry of issue on disk
    r = storeRefIssue(issueId, e->id);
    if (r<0) return r;

    // add this entry in internal in-memory tables
    entries[e->id] = e;
    i->addEntry(e);
    issues[issueId] = i;

    // if some association has been updated, then update the associations tables
    FOREACH(p, properties) {
        const PropertySpec *pspec = config.getPropertySpec(p->first);
        if (pspec && pspec->type == F_ASSOCIATION) {
            updateAssociations(i, p->first, p->second);
        }
    }

    entryId = e->id;

    return 0; // success
}

/** Push an uploaded entry in the database
  *
  * An error is raised in any of the following cases:
  * - the author of the entry is not the same as the username
  * - the parent is not null and issueId does not already exist
  * - the parent is not null and does not match the latest entry of the existing issueId
  *
  * A new issue is created if the parent of the pushed entry is 'null'
  * In this case, a new issueId is assigned, then it is returned (IN/OUT parameter).
  */
int Project::pushEntry(std::string issueId, const std::string &entryId,
                       const std::string &username, const std::string &tmpPath)
{
    LOG_FUNC();
    LOG_DEBUG("pushEntry(%s, %s, %s, %s)", issueId.c_str(), entryId.c_str(),
              username.c_str(), tmpPath.c_str());

    // load the file as an entry
    Entry *e = Entry::loadEntry(tmpPath, entryId, true); // check that the sha1 matches
    if (!e) return -1;

    // check that the username is the same as the author of the entry
    if (e->author != username) {
        LOG_ERROR("pushEntry error: usernames do not match (%s / %s)",
                  username.c_str(), e->author.c_str());
        delete e;
        return -2;
    }

    Issue *i = 0;
    Issue *newI = 0;
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);

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
            return -4; // the parent is not null and issueId does not exist
        }
        if (i->latest->id != e->parent) {
            LOG_ERROR("pushEntry error: parent does not match latest entry (%s / %s)",
                      issueId.c_str(), entryId.c_str());
            delete e;
            return -5;
        }
    }

    // move the entry to the official place
    std::string newpath = getObjectsDir() + "/" + Object::getSubpath(entryId);

    // verify that newpath does not exist
    if (fileExists(newpath)) {
        LOG_ERROR("cannot push entry %s: already exists", entryId.c_str());
        delete e;
        if (newI) delete newI;
        return -6;
    }

    // insert the new entry in the database
    i->addEntry(e);

    if (newI) {
        // insert the new issue in the database
        int r = insertIssue(i);
        if (r != 0) {
            delete e;
            delete i;
            return -1;
        }
    }

    mkdir(getDirname(newpath));
    LOG_DEBUG("rename: %s -> %s", tmpPath.c_str(), newpath.c_str());
    int r = rename(tmpPath.c_str(), newpath.c_str());
    if (r != 0) {
        LOG_ERROR("pushEntry error: could not officiliaze pushed entry %s/%s: %s",
                  issueId.c_str(), entryId.c_str(), strerror(errno));
        if (newI) delete newI;
        delete e;
        return -7;
    }

    // store the new ref of the issue
    r = storeRefIssue(i->id, i->latest->id);
    if (r != 0) {
        unlink(newpath.c_str()); // cleanup entry file
        if (newI) delete newI;
        delete e;
        return -8;
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
    ScopeLocker(lockerForConfig, LOCK_READ_ONLY);
    return config;
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
    ScopeLocker(locker, LOCK_READ_WRITE);

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
    e->issue->amendEntry(entryId, "", username);
    return 0;
}







