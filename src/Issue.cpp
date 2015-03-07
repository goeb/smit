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

#include "Issue.h"
#include "parseConfig.h"
#include "filesystem.h"
#include "logging.h"
#include "identifiers.h"
#include "global.h"
#include "stringTools.h"
#include "mg_win32.h"


#define K_MERGE_PENDING ".merge-pending";
#define K_ISSUE_ID "id"

enum FilterSearch {
    PROPERTY_NOT_FILTERED,
    PROPERTY_FILTERED_FOUND,
    PROPERTY_FILTERED_NOT_FOUND
};



std::string Issue::getSummary() const
{
    return getProperty(properties, "summary");
}


void Issue::addEntryInTable(Entry *e)
{
    // modification time of the issue it the creation time of the latest entry
    mtime = e->ctime;

    // if the issue had no entries then set the creation time
    if (!latest) ctime = e->ctime;

    // update the chain list of entries
    e->prev = latest;
    if (latest) {
        latest->next = e;
    }
    latest = e;
    e->issue = this;

}

/** Add an entry
  *
  * This does:
  * - update the issue in memory
  * - store the on persistent storage
  */
void Issue::addEntry(Entry *e)
{
    addEntryInTable(e);

    // consolidate the issue
    consolidateWithSingleEntry(e, true);
}

/** Insert entry before the latest entry
  *
  * This is typically used when loading an issue from persistent storage
  * (starting from latest entry).
  */
void Issue::insertEntry(Entry* e)
{
    if (first) {
        e->next = first;
        first->prev = e;
    }
    if (!latest) latest = e;
    first = e;
    e->issue = this;
}

Issue *Issue::load(const std::string &objectsDir, const std::string &latestEntryOfIssue)
{
    Issue *issue = new Issue();

    std::string entryid = latestEntryOfIssue;
    int error = 0;
    while (entryid.size() && entryid != K_PARENT_NULL) {
        std::string entryPath = objectsDir + '/' + Entry::getSubpath(entryid);
        Entry *e = Entry::loadEntry(entryPath, entryid, false);
        if (!e) {
            LOG_ERROR("Cannot load entry '%s'", entryPath.c_str());
            error = 1;
            break; // abort the loading of this issue
        }

        issue->insertEntry(e); // store the entry in the chain list

        entryid = e->parent; // go to parent entry
    }

    if (error) {
        // delete all entries and the issue
        Entry *e = issue->first;
        while (e) {
            Entry *tobeDeleted = e;
            e = e->next;
            delete tobeDeleted;
        }
        delete issue;
        return 0;

    }

    issue->consolidate();
    return issue;
}

/** Amend the given Entry with a new message
  *
  * The former message will be replaced by the new one
  * in the consolidation of the issue.
  */
void Issue::amendEntry(const std::string &entryId, const std::string &newMsg, const std::string &username)
{
    PropertiesMap properties;
    properties[K_MESSAGE].push_back(newMsg);
    properties[K_AMEND].push_back(entryId);
    Entry *amendingEntry = Entry::createNewEntry(properties, username, latest);

    addEntry(amendingEntry);

    consolidateAmendment(amendingEntry);
}



/** Copy properties of an entry to an issue.
  * If overwrite == false, then an already existing property in the
  * issue will not be overwritten.
  */
void Issue::consolidateWithSingleEntry(Entry *e, bool overwrite) {
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

/** Consolidate a possible amended entry
  */
void Issue::consolidateAmendment(Entry *e)
{
    PropertiesIt p = e->properties.find(K_AMEND);
    if (p == e->properties.end()) return; // no amendment
    if (p->second.empty()) {
        LOG_ERROR("cannot consolidateAmendment with no entry to consolidate");
        return;
    }
    std::string amendedEntryId = p->second.front();

    p = e->properties.find(K_MESSAGE);
    std::string msg;
    if (p == e->properties.end()) return; // no amending message
    if (p->second.empty()) {
        LOG_ERROR("cannot consolidateAmendment with no message");
        // consider the message as empty

    } else msg = p->second.front();

    // find this entry and modify its message
    Entry *amendedEntry = e;
    while ((amendedEntry = amendedEntry->prev)) {
        if (amendedEntry->id == amendedEntryId) break;
    }
    if (!amendedEntry) {
        LOG_ERROR("cannot consolidateAmendment for unfound entry '%s'", amendedEntryId.c_str());
        return;
    }
    amendedEntry->properties[K_MESSAGE].clear();
    amendedEntry->properties[K_MESSAGE].push_back(msg);
    amendedEntry->amendments.push_back(e->id);

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

        consolidateWithSingleEntry(e, false); // do not overwrite as we move from most recent to oldest
        consolidateAmendment(e);
        ctime = e->ctime; // the oldest entry will take precedence for ctime
        e = e->prev;
    }
}


/**
  * sortingSpec: a list of pairs (ascending-order, property-name)
  *
  */
void sort(std::vector<const Issue*> &inout, const std::list<std::pair<bool, std::string> > &sortingSpec)
{
    if (sortingSpec.size()==0) return;

    IssueComparator ic(sortingSpec);
    std::sort(inout.begin(), inout.end(), ic);
}

/** Look if the given multi-valued property is present in the given list
  */
bool isPropertyInFilter(const std::list<std::string> &propertyValue,
                        const std::list<std::string> &filteredValues)
{
    std::list<std::string>::const_iterator fv;
    std::list<std::string>::const_iterator v;

    FOREACH (fv, filteredValues) {
        FOREACH (v, propertyValue) {
            if (mg_strcasestr(v->c_str(), fv->c_str())) return PROPERTY_FILTERED_FOUND;
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



/**
  * @return
  *    true, if the issue should be kept
  *    false, if the issue should be excluded
  */
bool Issue::isInFilter(const std::map<std::string, std::list<std::string> > &filter) const
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
        PropertiesIt files = e->properties.find(K_FILE);
        if (files != e->properties.end()) {
            std::list<std::string>::const_iterator f;
            FOREACH(f, files->second) {
                if (mg_strcasestr(f->c_str(), text)) return true; // found
            }
        }

        // look at the author
        if (mg_strcasestr(e->author.c_str(), text)) return true; // found

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


