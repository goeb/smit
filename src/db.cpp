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

#define PROJECT_FILE "project"
#define ENTRIES "entries" // sub-directory of a project where the entries are stored
#define HEAD "_HEAD"

#define K_PARENT "_parent"
#define K_AUTHOR "author"
#define K_CTIME "ctime"

Database Database::Db;


int dbInit(const char * pathToRepository)
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
            int r = Project::load(pathToProject.c_str(), dp->d_name);
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


std::string Issue::getTitle() const
{
    return getProperty(properties, "title");
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
        LOG_ERROR("Project '%s' not loaded because of errors while reading the config.", path);
        return -1;
    }

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

    std::list<std::list<std::string> > lines = parseConfig(buf, n);
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
        //LOG_DEBUG("consolidateIssues() issue %s...", (char*)currentIssue->id.c_str());

        if (currentIssue->head.size() == 0) {
            // repair the head
            // TODO
            LOG_INFO("Missing head for issue '%s'. Repairing TODO...", currentIssue->id.c_str());
        }
        // now that the head is found, walk through all entries
        // following the _parent properties.

        std::string parentId = currentIssue->head;

        // the entries are walked through backwards (from most recent to oldest)
        while (0 != parentId.compare("null")) {
            std::map<std::string, Entry*>::iterator e = entries.find(parentId);
            if (e == entries.end()) {
                LOG_ERROR("Broken chain of entries: missing reference '%s'",
                          parentId.c_str());
                break; // abort the loop
            }
            Entry *currentEntry = e->second;
            // for each property of the parent,
            // create the same property in the issue, if not already existing
            // (in order to have only most recent properties)

            consolidateIssueWithSingleEntry(currentIssue, currentEntry, false); // do not overwrite as we move from most recent to oldest


            parentId = currentEntry->parent;
        }
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

        while (0 != currentEntryId.compare("null")) {
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
    // name type params ...
    // type = text | select | multiselect
    FieldSpec field;
    if (tokens.size() >= 2) {
        field.name = tokens.front();
        tokens.pop_front();
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
    } else { // not enough tokens
        LOG_ERROR("Not enough tokens");
        return field; // error, indicated to caller by empty name of field
    }
}


// @return 0 if OK, -1 on error
int Project::loadConfig(const char *path)
{
    std::string pathToProjectFile = path;
    pathToProjectFile = pathToProjectFile + '/' + PROJECT_FILE;
    FILE *f = fopen(pathToProjectFile.c_str(), "r");
    if (NULL == f) {
        LOG_DEBUG("Could not open file %s, %s", pathToProjectFile.c_str(), strerror(errno));
        return -1;
    }
    // else continue and parse the file

    int r = fseek(f, 0, SEEK_END); // go to the end of the file
    if (r != 0) {
        LOG_ERROR("could not fseek(%s): %s", pathToProjectFile.c_str(), strerror(errno));
        fclose(f);
        return -1;
    }
    long filesize = ftell(f);
    if (filesize > 1024*1024) {
        LOG_ERROR("loadConfig: file %s over-sized (%ld bytes)", path, filesize);
        fclose(f);
        return -1;
    }
    rewind(f);
    char *buf = (char *)malloc(filesize);
    size_t n = fread(buf, 1, filesize, f);
    if (n != filesize) {
        LOG_ERROR("fread(%s): short read. feof=%d, ferror=%d", pathToProjectFile.c_str(), feof(f), ferror(f));
        fclose(f);
        return -1;
    }
    std::list<std::list<std::string> > lines = parseConfig(buf, n);

    std::list<std::list<std::string> >::iterator line;
    for (line = lines.begin(); line != lines.end(); line++) {
        std::string token = line->front();
        line->pop_front();
        if (0 == token.compare("addField")) {
            FieldSpec field = parseFieldSpec(*line);
            if (field.name.size() > 0) {
                config.fields[field.name] = field;
                config.orderedFields.push_back(field.name);
                LOG_DEBUG("orderedFields: added %s", field.name.c_str());
            }
            // else: parse error, ignore
        } else if (0 == token.compare("addListDisplay")) {
            // TODO
            LOG_ERROR("addListDisplay not implemented");
        } else if (0 == token.compare("setDefaultColspec")) {
            if (line->empty()) {
                LOG_ERROR("Missing setDefaultColspec in '%s'", path);
                continue; // ignore this line
            }
            std::string colspec = line->front();
            if (colspec.size() > 0) {
                defaultColspec = parseColspec((char*)colspec.c_str());
            } else {
                LOG_ERROR("Empty setDefaultColspec in '%s'", path);
            }

        } else if (0 == token.compare("setDefaultFilter")) {
            // TODO
            LOG_ERROR("setDefaultFilter not implemented");
         } else if (0 == token.compare("setDefaultSorting")) {
            // TODO
            LOG_ERROR("setDefaultSorting not implemented");
        } else {
            LOG_ERROR("Unknown function '%s'", token.c_str());

        }
    }

    fclose(f);
    return 0;
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
    int len = strlen(sortingSpec);
    std::string currentPropertyName;
    int currentOffset = 0;
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


/** search
  *   fulltext: text that is searched (optional: 0 for no fulltext search)
  *             The case is ignored. (TODO)
  *   filterSpec: status:open,release!v1.0,assignee:John Smith
  *   sortingSpec: aa+bb-cc (+ for ascending, - for descending order)
  *                sort issues by aa ascending, then by bb ascending, then by cc descending
  *
  * @return
  *    The list of matching issues.
  */
std::list<Issue*> Project::search(const char *fulltextSearch, const char *filterSpec, const char *sortingSpec)
{
    AutoLocker scopeLocker(locker, LOCK_READ_ONLY);

    // General algorithm:
    //     1. walk through all issues and keep those needed in <filterspec> (marked '+')
    //     2. then, if fulltext is not null, walk through these issues and their
    //        related messages and keep those that contain <fulltext>
    //     3. then, remove the issues that are excluded by <filterSpec> (marked '-')
    //     4. then, do the sorting according to <sortingSpec>
    std::list<struct Issue*> result;
    std::map<std::string, Issue*>::iterator i;
    for (i=issues.begin(); i!=issues.end(); i++) {

        Issue* issue = i->second;
        // 1. TODO

        // 2. search full text
        if (! searchFullText(issue, fulltextSearch)) {
            // do not keep this issue
            continue;
        }

        // 3. remove the issues that are excluded by <filterSpec> (marked '-')
        // TODO

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
    while (e = getEntry(next)) {
        if (strcasestr(e->getMessage().c_str(), text)) return true; // found
        next = e->parent;
    }

    return false; // text not found

}

/** If issueId is empty:
  *     - a new issue is created
  *     - its ID is returned within parameter 'issueId'
  */
int Project::addEntry(std::map<std::string, std::list<std::string> > properties, std::string &issueId)
{
    locker.lockForWriting(); // TODO look for optimization

    Issue *i = 0;
    if (issueId.size()>0) {
        i = getIssue(issueId);
        if (!i) {
            LOG_INFO("Cannot add new entry to unknown issue: %s", issueId.c_str());
            locker.unlockForWriting();
            return -1;
        }

        // simplify the entry by removing properties that have the same value
        // in the issue (only the modified fiels are stored)
        // TODO
        // check if all properties are in the project config
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
                f = config.fields.find(entryProperty->first);
                if ( (f == config.fields.end()) && (entryProperty->first != K_TITLE) ) {
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
            locker.unlockForWriting();
            return 0; // no change
        }
    }

    std::string parent;
    if (i) parent = i->head;
    else parent = "null";

    // create Entry object with properties
    Entry *e = new Entry;
    e->parent = parent;
    e->ctime = time(0);
    //e->id
    e->author = "Fred"; // TODO
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
        int len = 3;
        while (len < id.size() && getIssue(id.substr(0, len))) len++;
        if (len > id.size()) {
            // another issue with same id exists. Cannot proceed.
            LOG_ERROR("Cannot store issue '%s': another exists with same id", id.c_str());
            locker.unlockForWriting();
            return -1;
        }

        id = id.substr(0, len); // shorten the issue here
        pathOfNewEntry = path + '/' + ENTRIES + '/' + id;
        // TODO
        int r = mkdir(pathOfNewEntry.c_str(), S_IRUSR | S_IXUSR | S_IWUSR);
        if (r != 0) {
            LOG_ERROR("Could not create dir '%s': %s", pathOfNewEntry.c_str(), strerror(errno));
            locker.unlockForWriting();

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
    int r = writeToFile(pathOfNewEntry.c_str(), data, false); // do not allow overwrite
    if (r != 0) {
        // error. TODO
        locker.unlockForWriting();

        return r;
    }

    // add this entry in Project::entries
    entries[id] = e;

    // consolidate the issue
    consolidateIssueWithSingleEntry(i, e, true);
    i->mtime = e->ctime;
    i->head = id;

    // update _HEAD
    std::string pathToHead = path + '/' + ENTRIES + '/' + issueId + '/' + HEAD;
    r = writeToFile(pathToHead.c_str(), id, true); // allow overwrite for _HEAD

    locker.unlockForWriting();

    return r;
}




// Deleting an entry is only possible if:
//     - this entry is the HEAD (has no child)
//     - the deleting happens less than 5 minutes after creation of the entry
// @return TODO
int deleteEntry(std::string entry)
{

}

std::string Entry::getMessage()
{
    return getProperty(properties, K_MESSAGE);
}

int Entry::getCtime() const
{
    return ctime;
}

/** Concatenate a list of strings to a string
  */
std::string toString(const std::list<std::string> &values)
{
    std::ostringstream text;
    std::list<std::string>::const_iterator v;
    for (v=values.begin(); v!=values.end(); v++) {
        if (v != values.begin()) text << ", ";
        text << v->c_str();
    }
    return text.str();
}


std::string Entry::getStringifiedProperty(const std::string &propertyName)
{
    std::map<std::string, std::list<std::string> >::iterator p;
    p = properties.find(propertyName);
    if (p == properties.end()) return "";
    else toString(p->second);
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

std::list<std::string> getProjectList()
{
    std::list<std::string> pList;
    std::map<std::string, Project*>::iterator p;
    for (p = Database::Db.projects.begin(); p!=Database::Db.projects.end(); p++) {
        pList.push_back(p->first);
    }
    return pList;
}

Locker::Locker()
{
    nReaders = 0;
    pthread_mutex_init(&readOnlyMutex, 0);
    pthread_mutex_init(&readWriteMutex, 0);

}

Locker::~Locker()
{
    pthread_mutex_destroy(&readOnlyMutex);
    pthread_mutex_destroy(&readWriteMutex);
}

void Locker::lockForWriting()
{
    pthread_mutex_lock(&readWriteMutex);
}
void Locker::unlockForWriting()
{
    pthread_mutex_unlock(&readWriteMutex);
}

void Locker::lockForReading()
{
    pthread_mutex_lock(&readOnlyMutex);
    if (nReaders == 0) {
        // first reader
        lockForWriting();
    }
    nReaders++;
    pthread_mutex_unlock(&readOnlyMutex);
}

void Locker::unlockForReading()
{
    pthread_mutex_lock(&readOnlyMutex);
    if (nReaders <= 0) {
        // error
        LOG_ERROR("unlockForReading error: nReaders == %d", nReaders);
    } else if (nReaders) {
        nReaders--;
    }

    if (nReaders == 0) {
        unlockForWriting();
    }
    pthread_mutex_unlock(&readOnlyMutex);

}

