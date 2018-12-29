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
#include "utils/parseConfig.h"
#include "utils/filesystem.h"
#include "utils/logging.h"
#include "utils/identifiers.h"
#include "utils/stringTools.h"
#include "global.h"
#include "mg_win32.h"
#include "fnmatch.h"

#define K_MERGE_PENDING ".merge-pending";
#define K_ISSUE_ID "id"


std::string Issue::getSummary() const
{
    return ::getProperty(properties, "summary");
}

/** Add an entry at the end
  *
  * This updates the latest entry.
  */
void Issue::addEntryInTable(Entry *e)
{
    // modification time of the issue it the creation time of the latest entry
    mtime = e->ctime;

    // if the issue had no entries then set the creation time
    if (entries.empty()) ctime = e->ctime;

    entries.push_back(*e);
    entries.back().issue = this;
}

/** Add an entry
  *
  * This does:
  * - update the issue in memory
  */
void Issue::addEntry(Entry *e)
{
    LOG_FUNC();
    addEntryInTable(e);

    // consolidate the issue
    consolidateWithSingleEntry(*e);
}

/** Insert entry before the latest entry
  *
  * This is typically used when loading an issue from persistent storage
  * (starting from latest entry).
  */
void Issue::insertEntry(Entry* e)
{
    if (!e) {
        LOG_ERROR("Issue::insertEntry: e=null");
        return;
    }

    entries.insert(entries.begin(), *e);
    entries.front().issue = this;
}


/** Amend the given Entry with a new message
  *
  * The former message will be replaced by the new one
  * in the consolidation of the issue.
  */
void Issue::amendEntry(Entry *amendingEntry)
{
    addEntry(amendingEntry);

    consolidateAmendment(*amendingEntry);
}



/** Copy properties of an entry to an issue.
  */
void Issue::consolidateWithSingleEntry(const Entry &e) {
    PropertiesIt p;
    FOREACH(p, e.properties) {
        if (p->first.size() && p->first[0] == '+') continue; // do not consolidate these (+file, +message, etc.)
        properties[p->first] = p->second;
    }
    // update also mtime of the issue
    mtime = e.ctime;
}

/** Consolidate a possible amended entry
  *
  * @param e
  *    The amending entry
  */
void Issue::consolidateAmendment(const Entry &amending)
{
    PropertiesIt p = amending.properties.find(K_AMEND);
    if (p == amending.properties.end()) return; // no amendment
    if (p->second.empty()) {
        LOG_ERROR("cannot consolidateAmendment with no entry to consolidate");
        return;
    }
    std::string amendedEntryId = p->second.front();

    p = amending.properties.find(K_MESSAGE);
    const std::string *newMsg = 0;
    if (p == amending.properties.end()) return; // no amending message
    if (p->second.empty()) {
        LOG_ERROR("cannot consolidateAmendment with no message");
        // consider the message as empty

    } else newMsg = &(p->second.front());

    // find this entry and modify its message
    std::vector<Entry>::iterator e;
    FOREACH(e, entries) {
        if (e->id == amendedEntryId) {
            // this is the entry that is being amended
            // overwrite previous message
            e->setMessage(newMsg);
            amendments[amendedEntryId].push_back(amending.id);
            break;
        }
    }
}

/** Consolidate an issue by accumulating all its entries
  *
  */
void Issue::consolidate()
{
    properties.clear();

    // ctime of the issue is ctime of its first entry
    if (!entries.empty()) ctime = entries.front().ctime;

    std::vector<Entry>::const_iterator e;
    FOREACH(e, entries) {
        consolidateWithSingleEntry(*e);
        consolidateAmendment(*e);
    }
}

std::string Issue::getProperty(const std::string &propertyName) const
{
    if (propertyName == "p") return project;

    // TODO handle the other reserved properties (id, mtime, ctime, ...)
    return ::getProperty(properties, propertyName);
}

/** Make a snapshot of the issue
  *
  * The issue is modified, so that only entries before the given
  * datetime are taken into account.
  *
  */
int Issue::makeSnapshot(time_t datetime)
{
    properties.clear();

    int n = 0; // number of remaining entries after snapshot

    // ctime of the issue is ctime of its first entry
    if (!entries.empty()) ctime = entries.front().ctime;

    std::vector<Entry>::const_iterator e;
    FOREACH(e, entries) {
        if (e->ctime > datetime) {
            // This entry newer than the given datetime, stop here.
            break;
        } else {
            n++;
            consolidateWithSingleEntry(*e);
            consolidateAmendment(*e);
        }
    }
    return n;
}


/** Look if any value of the given multi-valued property is present in the given list
  *
  * Ignore case.
  */
bool isPropertyInFilter(const std::list<std::string> &propertyValue,
                        const std::list<std::string> &filteredValues)
{
    std::list<std::string>::const_iterator fv;
    std::list<std::string>::const_iterator v;

    FOREACH (fv, filteredValues) {
        FOREACH (v, propertyValue) {
            if (FNM_NOMATCH != fnmatch(fv->c_str(), v->c_str(), FNM_CASEFOLD)) return true;
            //if ((*v) == (*fv)) return true;
        }
    }
    return false; // not found
}

/** Look if the given value is present in the given list
  *
  * Ignore case.
  */
bool isPropertyInFilter(const std::string &propertyValue,
                        const std::list<std::string> &filteredValues)
{
    std::list<std::string>::const_iterator fv;

    FOREACH (fv, filteredValues) {
        if (FNM_NOMATCH != fnmatch(fv->c_str(), propertyValue.c_str(), FNM_CASEFOLD)) return true;
        //if (propertyValue == *fv) return true;
    }

    return false; // not found
}


/** Applies a filter to the issue
  *
  * @return
  *    true, the issue does match the filter
  *    false, the issue does not match
  *
  * In mode FILTER_IN, a logical OR is done for the filters on the same property
  * and a logical AND is done between different properties.
  * Example:
  *    status:open, status:closed, author:john
  * is interpreted as:
  *    ( status == open OR status == closed ) AND author == john
  *
  * However for mode FILTER_OUT, a logical OR is made for all items of the filter.
  * Example:
  *    status:open, status:closed, author:john
  * is interpreted as:
  *    status == open OR status == closed OR author == john
  *
  * The reason for this difference is that we want to get as few
  * results as possible.
  *
  */
bool Issue::isInFilter(const std::map<std::string, std::list<std::string> > &filter, FilterMode mode) const
{
    if (filter.empty()) return false;

    // look for each property of the issue (except ctime and mtime)

    std::map<std::string, std::list<std::string> >::const_iterator f;
    FOREACH(f, filter) {
        std::string examinedProperty = f->first;
        bool doesMatch = false;

        if (examinedProperty == K_ISSUE_ID) {
            // id
            // look if id matches one of the filter values
            doesMatch = isPropertyInFilter(id, f->second);

        } else {
            std::map<std::string, std::list<std::string> >::const_iterator p;
            p = properties.find(examinedProperty);
            // If the issue has no such property, or if the property of this issue has no value,
            // then consider that the property has an empty value.
            if (p == properties.end() || p->second.empty()) {
                doesMatch = isPropertyInFilter("", f->second);
            } else {
                doesMatch = isPropertyInFilter(p->second, f->second);
            }
        }

        if (FILTER_IN == mode && !doesMatch) return false; // this makes a AND between properties
        else if (FILTER_OUT == mode && doesMatch) return true; // this makes a OR
        // else continue looking at the other properties of the filter
    }
    if (FILTER_IN == mode) return true;
    else return false; // mode FILTER_OUT
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
    std::vector<Entry>::const_iterator e;
    FOREACH(e, entries) {
        // do not search through amending entries
        if (!e->isAmending()) {
            // look through the message
            if (mg_strcasestr(e->getMessage().c_str(), text)) return true; // found

            // look through uploaded files
            PropertiesIt files = e->properties.find(K_FILE);
            if (files != e->properties.end()) {
                std::list<std::string>::const_iterator f;
                FOREACH(f, files->second) {
                    if (mg_strcasestr(f->c_str(), text)) return true; // found
                }
            }

            // look at the author of the entry
            if (mg_strcasestr(e->author.c_str(), text)) return true; // found
        }
    }

    return false; // text not found

}

int Issue::getNumberOfTaggedIEntries(const std::string &tagname) const
{
    int n = 0;
    std::map<std::string, std::set<std::string> >::const_iterator tit;
    FOREACH(tit, tags) {
        n += tit->second.count(tagname);
    }
    return n;
}

void Issue::addTag(const std::string &entryId, const std::string &tagname)
{
    tags[entryId].insert(tagname);
}

std::set<std::string> Issue::getTags(const std::string &entryId)
{
    std::set<std::string> result;
    std::map<std::string, std::set<std::string> >::iterator tit = tags.find(entryId);
    if (tit != tags.end()) {
        result = tit->second;
    }
    return result;
}

void Issue::setTags(const std::string &entryId, std::set<std::string> &tagsOfEntry)
{
    if (tagsOfEntry.empty()) tags.erase(entryId);
    else tags[entryId] = tagsOfEntry;
}

bool Issue::hasTag(const std::string &entryId, const std::string &tagname) const
{
    std::map<std::string, std::set<std::string> >::const_iterator tit = tags.find(entryId);
    if (tit == tags.end()) return false;

    std::set<std::string>::const_iterator tagit = tit->second.find(tagname);
    if (tagit == tit->second.end()) return false;

    return true;
}

IssueCopy::IssueCopy(const Issue &i) : Issue(i)
{
}

bool IssueCopy::lessThan(const IssueCopy &other, const std::list<std::pair<bool, std::string> > &sortingSpec) const
{
    return lessThan(&other, sortingSpec);
}

/** Compare 2 issues after sortingSpec.
  *
  * sortingSpec: a list of pairs (ascending-order, property-name)
  *
  * @return
  *     true or false
  *     If they are equal, false is returned.
  */
bool IssueCopy::lessThan(const IssueCopy* other, const std::list<std::pair<bool, std::string> > &sortingSpec) const
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

        } else if (s->second == "p") {
            if (project < other->project) result = -1;
            else if (project == other->project) result = 0;
            else result = +1;

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

/**
  * sortingSpec: a list of pairs (ascending-order, property-name)
  *
  */
void IssueCopy::sort(std::vector<IssueCopy> &inout, const std::list<std::pair<bool, std::string> > &sortingSpec)
{
    if (sortingSpec.size()==0) return;

    IssueComparator ic(sortingSpec);
    std::sort(inout.begin(), inout.end(), ic);
}
