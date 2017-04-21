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

#include "Entry.h"
#include "utils/parseConfig.h"
#include "utils/filesystem.h"
#include "utils/logging.h"
#include "utils/identifiers.h"
#include "utils/stringTools.h"
#include "global.h"
#include "mg_win32.h"

const std::string Entry::EMPTY_MESSAGE("");


/** Load an entry from a file
  *
  * @param id
  *     id of the new Entry instance to be created
  */
Entry *Entry::loadEntry(const std::string &path, const std::string &id)
{
    // load a given entry
    std::string buf;
    int n = loadFile(path.c_str(), buf);

    if (n < 0) {
        // error loading the file
        LOG_ERROR("Cannot load entry '%s': %s", path.c_str(), strerror(errno));
        return 0;
    }

    // log if sha1 does not match
    std::string hash = getSha1(buf);
    if (0 != hash.compare(id)) {
        LOG_ERROR("Hash does not match: path=%s, id=%s, sha1=%s", path.c_str(), id.c_str(), hash.c_str());
    }

    Entry *e = new Entry;
    e->id = id;

    std::list<std::list<std::string> > lines = parseConfigTokens(buf.c_str(), buf.size());

    std::list<std::list<std::string> >::iterator line;
    int lineNum = 0;
    std::string smitVersion = "1.0"; // default value if version is not present
    for (line=lines.begin(); line != lines.end(); line++) {
        lineNum++;
        // each line should be a key / value pair
        if (line->empty()) continue; // ignore this line

        std::string key = line->front();
        line->pop_front(); // remove key from tokens
        std::string firstValue;
        // it is allowed for multiselects and associations to have no value
        if (line->size() >= 1) firstValue = line->front();

        if (0 == key.compare(K_CTIME)) {
            e->ctime = atoi((char*)firstValue.c_str());
        } else if (0 == key.compare(K_PARENT)) e->parent = firstValue;
        else if (0 == key.compare(K_AUTHOR)) e->author = firstValue;
        else if (key == K_SMIT_VERSION) smitVersion = firstValue;
        else {
            e->properties[key] = *line;
        }
    }

    e->updateMessage();
    return e;
}

bool Entry::isAmending() const
{
    std::string a = getProperty(properties, K_AMEND);
    return (!a.empty());
}

int Entry::getCtime() const
{
    return ctime;
}

/** Compute self sha1 and store it
  */
void Entry::setId()
{
    std::string data = serialize();
    id = getSha1(data);
}

void Entry::updateMessage()
{
    // update pointer to message
    std::map<std::string, std::list<std::string> >::const_iterator t = properties.find(K_MESSAGE);
    const std::string *m;
    if (t != properties.end() && (t->second.size()>0) ) m = &(t->second.front());
    else m = &EMPTY_MESSAGE;

    setMessage(m);
}

Entry *Entry::createNewEntry(const PropertiesMap &props, const std::string &author, const Entry *eParent)
{
    Entry *e = new Entry();
    e->properties = props;
    e->author = author;
    e->ctime = time(0);

    e->updateMessage();

    if (eParent) e->parent = eParent->id;
    else e->parent = K_PARENT_NULL;

    e->setId();

    return e;
}

/** Append an entry after this
  *
  */
void Entry::append(Entry *e)
{
    e->setPrev(this);
    setNext(e);
}

std::string Entry::serialize() const
{
    std::ostringstream s;

    s << K_SMIT_VERSION << " " << VERSION << "\n";
    s << K_PARENT << " " << parent << "\n";
    s << serializeProperty(K_AUTHOR, author);
    s << K_CTIME << " " << ctime << "\n";

    std::map<std::string, std::list<std::string> >::const_iterator p;
    for (p = properties.begin(); p != properties.end(); p++) {
        std::string key = p->first;
        std::list<std::string> value = p->second;
        s << serializeProperty(key, value);
    }
    return s.str();
}

/**
  * sortingSpec: a list of pairs (ascending-order, property-name)
  *
  */
void Entry::sort(std::vector<Entry> &inout, const std::list<std::pair<bool, std::string> > &sortingSpec)
{
    if (sortingSpec.size()==0) return;

    EntryComparator ec(sortingSpec);
    std::sort(inout.begin(), inout.end(), ec);
}


bool Entry::lessThan(const Entry &other, const std::list<std::pair<bool, std::string> > &sortingSpec) const
{
    return lessThan(&other, sortingSpec);
}

/** Compare 2 entries after sortingSpec.
  *
  * sortingSpec: a list of pairs (ascending-order, property-name)
  *
  * @return
  *     true or false
  *     If they are equal, false is returned.
  */
bool Entry::lessThan(const Entry* other, const std::list<std::pair<bool, std::string> > &sortingSpec) const
{
    if (!other) return false;

    int result = 0; // 0 means equal, <0 means less-than, >0 means greater-than
    std::list<std::pair<bool, std::string> >::const_iterator s = sortingSpec.begin();

    while ( (result == 0) && (s != sortingSpec.end()) ) {

        // case of id, ctime
        if (s->second == "id") {
            if (id == other->id) result = 0;
            else if (atoi(id.c_str()) < atoi(other->id.c_str())) result = -1;
            else result = +1;

        } else if (s->second == "author") {
            if (author < other->author) result = -1;
            else if (author > other->author) result = +1;
            else result = 0;

        } else if (s->second == "ctime") {
            if (ctime < other->ctime) result = -1;
            else if (ctime > other->ctime) result = +1;
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
