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

#include "db.h"
#include "parseConfig.h"
#include "logging.h"
#include "identifiers.h"
#include "global.h"
#include "stringTools.h"

#define PROJECT_FILE "project"
#define ENTRIES "entries" // sub-directory of a project where the entries are stored
#define HEAD "_HEAD"
#define VIEWS_FILE "views"


#define K_PARENT "_parent"
#define K_AUTHOR "author"
#define K_CTIME "ctime"
#define K_ID "id"
#define K_PARENT_NULL "null"

Database Database::Db;


int dbLoad(const char * pathToRepository)
{
    // look for all files "pathToRepository/p/project"
    // and parse them
    // then, load all pathToRepository/p/entries/*/*

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
            Project::load(pathToProject.c_str(), dp->d_name);
        }
        closedir(dirp);
    }
    return Database::Db.projects.size();
}
void Issue::loadHead(const std::string &issuePath)
{
    std::string headPath = issuePath + '/' + HEAD;
    char *buf = 0;
    int n = loadFile(headPath.c_str(), &buf);

    if (n <= 0) return; // error or empty file

    head.assign(buf, n);
    free(buf);
}

std::string getProperty(const std::map<std::string, std::list<std::string> > &properties, const std::string &propertyName)
{
    std::map<std::string, std::list<std::string> >::const_iterator t = properties.find(propertyName);
    std::string propertyValue = "";
    if (t != properties.end() && (t->second.size()>0) ) propertyValue = toString(t->second);

    return propertyValue;
}


std::string Issue::getSummary() const
{
    return getProperty(properties, "summary");
}

/** load in memory the given project
  * re-load if it was previously loaded
  * @param path
  *    Full path where the project is stored
  * @param name
  *    Name of the project (generally the same as the basename of the path)
  *
  * @return 0 if success, -1 if failure
  */

int Project::load(const char *path, char *name)
{
    Project *p = new Project;
    LOG_INFO("Loading project %s (%p)...", path, p);

    p->name = name;
    p->path = path;

    int r = p->loadConfig(path);
    if (r == -1) {
        delete p;
        LOG_DEBUG("Project '%s' not loaded because of errors while reading the config.", path);
        return -1;
    }

    p->loadPredefinedViews(path);

    r = p->loadEntries(path);
    if (r == -1) {
        LOG_ERROR("Project '%s' not loaded because of errors while reading the entries.", path);
        delete p;
        return -1;
    }
    LOG_INFO("Project %s loaded.", path);

    p->consolidateIssues();

    // store the project in memory
    Database::Db.projects[name] = p;
    return 0;
}



Entry *loadEntry(std::string dir, const char* basename)
{
    // load a given entry
    std::string path = dir + '/' + basename;
    char *buf = 0;
    int n = loadFile(path.c_str(), &buf);

    if (n <= 0) return 0; // error or empty file

    Entry *e = new Entry;
    e->id = basename;

    std::list<std::list<std::string> > lines = parseConfigTokens(buf, n);
    free(buf);

    std::list<std::list<std::string> >::iterator line;
    int lineNum = 0;
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
        else {
            e->properties[key] = *line;
        }
    }
    return e;
}

/** Copy properties of an entry to an issue.
  * If overwrite == false, then an already existing property in the
  * issue will not be overwritten.
  */
void consolidateIssueWithSingleEntry(Issue *i, Entry *e, bool overwrite) {
    std::map<std::string, std::list<std::string> >::iterator p;
    for (p = e->properties.begin(); p != e->properties.end(); p++) {
        if (overwrite || (i->properties.count(p->first) == 0) ) {
            i->properties[p->first] = p->second;
        }
    }
    // update also mtime of the issue
    if (i->mtime == 0 || overwrite) i->mtime = e->ctime;
}

/** Consolidate an issue by accumulating all its entries
  *
  * This method must be called from a mutex-protected scope (no mutex is managed in here).
  */
void Project::consolidateIssue(Issue *i)
{
    if (!i) {
        LOG_ERROR("Cannot consolidate null issue!");
        return;
    }

    if (i->head.empty()) {
        // repair the head
        // TODO
        LOG_ERROR("Missing head for issue '%s'. Repairing TODO...", i->id.c_str());
        return;
    }
    // now that the head is found, walk through all entries
    // following the _parent properties.

    Entry *e = 0;
    std::string parent = i->head;
    // the entries are walked through backwards (from most recent to oldest)
    do {
        e = getEntry(parent);

        if (!e) {
            LOG_ERROR("Broken chain of entries: missing reference %s for issue %s", parent.c_str(), i->id.c_str());
            break;
        }
        // for each property of the parent,
        // create the same property in the issue, if not already existing
        // (in order to have only most recent properties)

        consolidateIssueWithSingleEntry(i, e, false); // do not overwrite as we move from most recent to oldest

        parent = e->parent;
    } while (parent != K_PARENT_NULL);
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

        consolidateIssue(currentIssue);

    }
    LOG_DEBUG("consolidateIssues() done.");
}
int Project::loadEntries(const char *path)
{
    // load files path/entries/*/*
    // The tree of entries is as follows:
    // myproject/entries/XXX/XXX (XXX is the issue id)
    // myproject/entries/XXX/ABCDEF012345
    // myproject/entries/XXX/123356AF
    // myproject/entries/XXX/_HEAD
    // myproject/entries/YYYYYY/YYYYYY (YYYYYY is another issue id)
    // myproject/entries/YYYYYY/_HEAD
    // etc.
    std::string pathToEntries = path;
    pathToEntries = pathToEntries + '/' + ENTRIES;
    DIR *entriesDirHandle;
    if ((entriesDirHandle = opendir(pathToEntries.c_str())) == NULL) {
        LOG_ERROR("Cannot open directory '%s'", pathToEntries.c_str());
        return -1;

    } else {
        struct dirent *issueDir;

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
                issues[issue->id] = issue;

                struct dirent *entryFile;
                while ((entryFile = readdir(issueDirHandle)) != NULL) {
                    if (0 == strcmp(entryFile->d_name, ".")) continue;
                    if (0 == strcmp(entryFile->d_name, "..")) continue;
                    if (0 == strcmp(entryFile->d_name, HEAD)) {
                        // this is the HEAD, ie: the lastest entry.
                        issue->loadHead(issuePath);
                        continue;
                    }
                    std::string filePath = issuePath + '/' + entryFile->d_name;
                    Entry *e = loadEntry(issuePath, entryFile->d_name);
                    if (e) entries[e->id] = e;
                    else LOG_ERROR("Cannot load entry '%s'", filePath.c_str());
                }

                closedir(issueDirHandle);
            }

        }
        closedir(entriesDirHandle);
    }
    return 0;
}
Issue *Project::getIssue(const std::string &id) const
{
    std::map<std::string, Issue*>::const_iterator i;
    i = issues.find(id);
    if (i == issues.end()) return 0;
    else return i->second;
}

Entry *Project::getEntry(const std::string &id) const
{
    if (id == K_PARENT_NULL) return 0;

    std::map<std::string, Entry*>::const_iterator e;
    e = entries.find(id);
    if (e == entries.end()) return 0;
    else return e->second;
}


int Project::get(const char *issueId, Issue &issue, std::list<Entry*> &Entries)
{
    Issue *i = getIssue(issueId);
    if (!i) {
        // issue not found
        LOG_DEBUG("Issue not found: %s", issueId);
        return -1;
    } else {
        issue = *i;
        // build list of entries
        std::string currentEntryId = issue.head; // latest entry

        while (0 != currentEntryId.compare(K_PARENT_NULL)) {
            std::map<std::string, Entry*>::iterator e = entries.find(currentEntryId);
            if (e == entries.end()) {
                LOG_ERROR("Broken chain of entries: missing reference '%s'", currentEntryId.c_str());
                break; // abort the loop
            }
            Entry *currentEntry = e->second;
            Entries.insert(Entries.begin(), currentEntry); // chronological order (latest last)
            currentEntryId = currentEntry->parent;
        }
    }

    return 0;
}

FieldSpec parseFieldSpec(std::list<std::string> & tokens)
{
    // Supported syntax:
    // name [label <label>] type params ...
    // type = text | select | multiselect | selectUser
    FieldSpec field;
    if (tokens.size() < 2) {
        LOG_ERROR("Not enough tokens");
        return field; // error, indicated to caller by empty name of field
    }

    field.name = tokens.front();
    tokens.pop_front();

    if (tokens.size() < 1) {
        LOG_ERROR("Not enough tokens");
        field.name = "";
        return field; // error, indicated to caller by empty name of field
    }

    std::string type = tokens.front();
    tokens.pop_front();
    if (0 == type.compare("text")) field.type = F_TEXT;
    else if (0 == type.compare("selectUser")) field.type = F_SELECT_USER;
    else if (0 == type.compare("select")) field.type = F_SELECT;
    else if (0 == type.compare("multiselect")) field.type = F_MULTISELECT;
    else { // error, unknown type
        LOG_ERROR("Unkown field type '%s'", type.c_str());
        field.name.clear();
        return field; // error, indicated to caller by empty name of field
    }

    if (F_SELECT == field.type || F_MULTISELECT == field.type) {
        // populate the allowed values
        while (tokens.size() > 0) {
            std::string value = tokens.front();
            tokens.pop_front();
            field.selectOptions.push_back(value);
        }
    }
    return field;
}

/** Return a configuration object from a list of lines of tokens
  * The 'lines' parameter is modified and cleaned up of incorrect lines
  * (suitable for subsequent searialisation to project file).
  */
ProjectConfig parseProjectConfig(std::list<std::list<std::string> > &lines)
{
    ProjectConfig config;

    std::list<std::list<std::string> >::iterator line;
    std::list<std::list<std::string> > wellFormatedLines;

    FOREACH (line, lines) {
        wellFormatedLines.push_back(*line);

        std::string token = pop(*line);
        if (0 == token.compare("addProperty")) {
            PropertySpec property = parseFieldSpec(*line);
            if (property.name.size() > 0) {
                config.properties[property.name] = property;
                config.orderedProperties.push_back(property.name);
                LOG_DEBUG("orderedProperties: added %s", property.name.c_str());

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
        } else {
            LOG_ERROR("Unknown function '%s'", token.c_str());
            wellFormatedLines.pop_back(); // remove incorrect line
        }
    }
    lines = wellFormatedLines;
    return config;
}

std::map<std::string, PredefinedView> parsePredefinedViews(std::list<std::list<std::string> > lines)
{
    std::map<std::string, PredefinedView> pvs;
    std::list<std::list<std::string> >::iterator line;
    std::string token;
    FOREACH(line, lines) {
        token = pop(*line);
        if (token.empty()) continue;

        if (token == "addView") {
            PredefinedView pv;
            pv.name = pop(*line);
            if (pv.name.empty()) {
                LOG_ERROR("parsePredefinedViews: Empty view name. Skip.");
                continue;
            }

            while (! line->empty()) {
                token = pop(*line);
                if (token == "filterin" || token == "filterout") {
                    std::string property = pop(*line);
                    std::string value = pop(*line);
                    if (property.empty() || value.empty()) {
                        LOG_ERROR("parsePredefinedViews: Empty property or value for filterin/out");
                        pv.name.clear(); // invalidate this line
                        break;
                    }
                    if (token == "filterin") pv.filterin[property].push_back(value);
                    else pv.filterout[property].push_back(value);

                } else if (token == "colspec") {
                    pv.colspec = pop(*line);
                    if (pv.colspec.empty()) {
                        LOG_ERROR("parsePredefinedViews: Empty property or value for filterin/out");
                        pv.name.clear(); // invalidate this line
                        break;
                    }
                } else if (token == "sort") {
                    pv.sort = pop(*line);
                    if (pv.sort.empty()) {
                        LOG_ERROR("parsePredefinedViews: Empty property or value for filterin/out");
                        pv.name.clear(); // invalidate this line
                        break;
                    }

                } else if (token == "search") {
                    pv.search = pop(*line);
                    if (pv.sort.empty()) {
                        LOG_ERROR("parsePredefinedViews: Empty property or value for filterin/out");
                        pv.name.clear(); // invalidate this line
                        break;
                    }
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


    char *buf = 0;
    int n = loadFile(pathToProjectFile.c_str(), &buf);

    if (n <= 0) return -1; // error or empty file

    std::list<std::list<std::string> > lines = parseConfigTokens(buf, n);

    free(buf); // not needed any longer

    config = parseProjectConfig(lines);

    return 0;
}

int Project::modifyConfig(std::list<std::list<std::string> > &tokens)
{
    LOG_FUNC();
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE);
    ProjectConfig c = parseProjectConfig(tokens);
    if (c.properties.size() == 0) {
        // error do not accept this
        LOG_ERROR("Reject modification of project structure as there is no property at all");
        return -1;
    }
    c.predefinedViews = config.predefinedViews; // keep those unchanged

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

void Project::loadPredefinedViews(const char *projectPath)
{
    LOG_FUNC();

    std::string path = projectPath;
    path += '/';
    path += VIEWS_FILE;

    char *buf = 0;
    int n = loadFile(path.c_str(), &buf);

    if (n > 0) {

        std::list<std::list<std::string> > lines = parseConfigTokens(buf, n);

        free(buf); // not needed any longer

        config.predefinedViews = parsePredefinedViews(lines);
    } // else error of empty file

    LOG_DEBUG("predefined views loaded: %d", config.predefinedViews.size());
}



std::string Project::getLabelOfProperty(const std::string &propertyName) const
{
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

/**
  * sortingSpec: a list of pairs (ascending-order, property-name)
  *
  */
void sort(std::list<Issue*> &inout, const std::list<std::pair<bool, std::string> > &sortingSpec)
{
    if (sortingSpec.size()==0) return;

    std::list<Issue*> workingList = inout; // make a copy
    inout.clear();
    Issue* maxIssue = 0;
    // headsort algorithm is used
    std::list<Issue*>::iterator i;
    std::list<Issue*>::iterator imax; // iterator on position of the max element
    while (workingList.size() > 0) {
        // get the max of workingList
        i = workingList.begin();
        imax = i;
        maxIssue = 0;
        while (i != workingList.end()) {
            if (!maxIssue) maxIssue = *i;
            else {
                if (maxIssue->lessThan(*i, sortingSpec)) {
                    maxIssue = *i;
                    imax = i;
                }
            }
            i++;
        }
        // put maxIssue in the result
        inout.push_back(maxIssue);
        // erase max issue from working list
        workingList.erase(imax);
    }
}

enum FilterSearch {
    PROPERTY_NOT_FILTERED,
    PROPERTY_FILTERED_FOUND,
    PROPERTY_FILTERED_NOT_FOUND
};

/** Look if the given property name/value is present in the given list
  */
FilterSearch filterProperty(const std::string &propertyName, const std::string &propertyValue,
          const std::map<std::string, std::list<std::string> > &filter)
{
    std::map<std::string, std::list<std::string> >::const_iterator p;
    p = filter.find(propertyName);
    if (p == filter.end()) return PROPERTY_NOT_FILTERED;

    std::list<std::string>::const_iterator v;
    for (v = p->second.begin(); v != p->second.end(); v++) {
        if (*v == propertyValue) return PROPERTY_FILTERED_FOUND;
    }
    return PROPERTY_FILTERED_NOT_FOUND; // not found
}

/**
  * @return
  *    true, if the issue should be kept
  *    false, if the issue should be excluded
  */
bool Issue::filter(const std::map<std::string, std::list<std::string> > &filterIn,
                   const std::map<std::string, std::list<std::string> > &filterOut)
{
    if (filterIn.size() == 0 && filterOut.size() == 0) return true;

    // look for each property of the issue (except ctime and mtime)
    // id
    if (PROPERTY_FILTERED_FOUND == filterProperty(K_ID, id, filterOut)) return false;
    if (PROPERTY_FILTERED_NOT_FOUND == filterProperty(K_ID, id, filterIn)) return false;

    // other properties
    std::map<std::string, std::list<std::string> >::const_iterator p;
    for (p = properties.begin(); p != properties.end();  p++) {
        std::list<std::string>::const_iterator v;
        for (v = p->second.begin(); v != p->second.end(); v++) {
            if (PROPERTY_FILTERED_FOUND == filterProperty(p->first, *v, filterOut)) return false;
            if (PROPERTY_FILTERED_NOT_FOUND == filterProperty(p->first, *v, filterIn)) return false;
        }
    }
    return true;
}

/** search
  *   fulltext: text that is searched (optional: 0 for no fulltext search)
  *             The case is ignored. (TODO)
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
std::list<Issue*> Project::search(const char *fulltextSearch,
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
    std::list<struct Issue*> result;

    std::map<std::string, Issue*>::iterator i;
    for (i=issues.begin(); i!=issues.end(); i++) {

        Issue* issue = i->second;
        // 1. TODO
        if (!issue->filter(filterIn, filterOut)) continue;

        // 2. search full text
        if (! searchFullText(issue, fulltextSearch)) {
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

/** Compare 2 issues after sortingSpec.
  *
  * sortingSpec: a list of pairs (ascending-order, property-name)
  *
  * @return
  *     true or false
  */
bool Issue::lessThan(Issue* other, const std::list<std::pair<bool, std::string> > &sortingSpec)
{
    if (!other) return false;

    int result = 0; // 0 means equal, <0 means less-than, >0 means greater-than
    std::list<std::pair<bool, std::string> >::const_iterator s = sortingSpec.begin() ;
    while ( (result == 0) && (s != sortingSpec.end()) ) {
        // case of id, ctime, mtime
        if (s->second == "id") result = id.compare(other->id);
        else if (s->second == "ctime") {
            if (ctime < other->ctime) result = -1;
            else if (ctime > other->ctime) result = +1;
            else result = 0;
        } else if (s->second == "mtime") {
            if (mtime < other->mtime) result = -1;
            else if (mtime > other->mtime) result = +1;
            else result = 0;
        } else {
            // the other properties
            std::string local = getProperty(properties, s->second);
            std::string otherProperty = getProperty(other->properties, s->second);
            result = local.compare(otherProperty);
        }
        if (!s->first) result = -result; // descending order
        s++;
    }
    if (result<0) return false;
    else return true;
}


/** Search for the given text through the issue properties
  * and the messages of the entries.
  *
  * @return
  *     true if found, false otherwise
  *
  */
bool Project::searchFullText(const Issue* issue, const char *text) const
{
    if (!text) return true;

    // look if id contains the fulltextSearch
    if (strcasestr(issue->id.c_str(), text)) return true; // found

    // look through the properties of the issue
    std::map<std::string, std::list<std::string> >::const_iterator p;
    for (p = issue->properties.begin(); p != issue->properties.end(); p++) {
        std::list<std::string>::const_iterator pp;
        std::list<std::string> listOfValues = p->second;
        for (pp = listOfValues.begin(); pp != listOfValues.end(); pp++) {
            if (strcasestr(pp->c_str(), text)) return true;  // found
        }
    }

    // look through the entries
    Entry *e = 0;
    std::string next = issue->head;
    while ( (e = getEntry(next)) ) {
        if (strcasestr(e->getMessage().c_str(), text)) return true; // found
        next = e->parent;
    }

    return false; // text not found

}

/** If issueId is empty:
  *     - a new issue is created
  *     - its ID is returned within parameter 'issueId'
  */
int Project::addEntry(std::map<std::string, std::list<std::string> > properties, std::string &issueId, std::string username)
{
    ScopeLocker scopeLocker(locker, LOCK_READ_WRITE) ; // TODO look for optimization

    Issue *i = 0;
    if (issueId.size()>0) {
        i = getIssue(issueId);
        if (!i) {
            LOG_INFO("Cannot add new entry to unknown issue: %s", issueId.c_str());
            return -1;
        }

        // simplify the entry by removing properties that have the same value
        // in the issue (only the modified fiels are stored)
        //
        // check that all properties are in the project config
        // and that they bring a modification of the issue
        // else, remove them
        std::map<std::string, std::list<std::string> >::iterator entryProperty;
        entryProperty = properties.begin();
        while (entryProperty != properties.end()) {
            bool doErase = false;

            if (entryProperty->first == K_MESSAGE) {
                if (entryProperty->second.size() && entryProperty->second.front().empty()) {
                    // erase if message is emtpy
                    doErase = true;
                }
            } else {
                std::map<std::string, FieldSpec>::iterator f;
                f = config.properties.find(entryProperty->first);
                if ( (f == config.properties.end()) && (entryProperty->first != K_SUMMARY) ) {
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

    std::string parent;
    if (i) parent = i->head;
    else parent = K_PARENT_NULL;

    // create Entry object with properties
    Entry *e = new Entry;
    e->parent = parent;
    e->ctime = time(0);
    //e->id
    e->author = username;
    e->properties = properties;

    // serialize the entry
    std::string data = e->serialize();

    // generate a id for this entry
    std::string id;
    id = computeIdBase34((uint8_t*)data.c_str(), data.size());

    LOG_DEBUG("new entry: %s", id.c_str());

    std::string pathOfNewEntry;

    // write this entry to disk, update _HEAD
    // if issueId is empty, generate a new issueId
    if (issueId.empty()) {
        // create new directory for this issue

        // in order to have a short id, keep only the first characters
        // and if another issue exists with this id, then increase the length.
        size_t len = 3;
        while (len < id.size() && getIssue(id.substr(0, len))) len++;
        if (len > id.size()) {
            // another issue with same id exists. Cannot proceed.
            LOG_ERROR("Cannot store issue '%s': another exists with same id", id.c_str());
            return -1;
        }

        id = id.substr(0, len); // shorten the issue here
        pathOfNewEntry = path + '/' + ENTRIES + '/' + id;

        int r = mkdir(pathOfNewEntry.c_str(), S_IRUSR | S_IXUSR | S_IWUSR);
        if (r != 0) {
            LOG_ERROR("Could not create dir '%s': %s", pathOfNewEntry.c_str(), strerror(errno));

            return -1;
        }
        i = new Issue();
        i->ctime = e->ctime;
        i->id = id;
        issueId = id; // set the new issue ID

        // add it to the internal memory
        issues[id] = i;

    } else {
        pathOfNewEntry = path + '/' + ENTRIES + '/' + issueId;
    }
    pathOfNewEntry += '/';
    pathOfNewEntry += id;
    int r = writeToFile(pathOfNewEntry.c_str(), data);
    if (r != 0) {
        // error.
        LOG_ERROR("Could not write new entry to disk");

        return r;
    }

    // add this entry in Project::entries
    e->id = id;
    entries[id] = e;

    // consolidate the issue
    consolidateIssueWithSingleEntry(i, e, true);
    i->mtime = e->ctime;
    i->head = id;

    // update _HEAD
    r = writeHead(issueId, id);

    return r;
}

int Project::writeHead(const std::string &issueId, const std::string &entryId)
{
    std::string pathToHead = path + '/' + ENTRIES + '/' + issueId + '/' + HEAD;
    int r = writeToFile(pathToHead.c_str(), entryId);
    if (r<0) {
        LOG_ERROR("Cannot write HEAD (%s/%s)", issueId.c_str(), entryId.c_str());
    }
    return r;
}

std::list<std::string> Project::getDefaultColspec()
{
    std::list<std::string> colspec = config.orderedProperties;
    // add mandatory properties that are not included in orderedProperties
    colspec.push_front("summary");
    colspec.push_front("mtime");
    colspec.push_front("ctime");
    colspec.push_front("id");
    return colspec;
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
  */

int Project::deleteEntry(std::string entryId, const std::string &username)
{
    ScopeLocker(locker, LOCK_READ_WRITE);

    Issue *i = 0;
    std::map<std::string, Entry*>::iterator ite;
    ite = entries.find(entryId);
    if (ite == entries.end()) return -1;

    Entry *e = ite->second;

    if (time(0) - e->ctime > DELETE_DELAY_S) return -2;
    else if (e->parent == K_PARENT_NULL) return -3;
    else if (e->author != username) return -4;

    else {

        // find the issue related to this entry
        Entry *e2 = e;
        while (e2 && e2->parent != K_PARENT_NULL) e2 = getEntry(e2->parent);

        if (!e2) return -5; // could not find parent issue

        i = getIssue(e2->id);
        if (!i) return -6;

        if (i->head != e->id) return -7;
    }
    // ok, we can delete this entry

    // modify pointers
    i->head = e->parent;
    consolidateIssue(i);
    // write to disk
    int r = writeHead(i->id, e->parent);

   // removre from internal tables
    entries.erase(ite);
    delete e;

    return r;
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



Project *Database::getProject(const std::string & projectName)
{
    std::map<std::string, Project*>::iterator p = Database::Db.projects.find(projectName);
    if (p == Database::Db.projects.end()) return 0;
    else return p->second;
}

