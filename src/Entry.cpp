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


/** Load an entry from a file
  *
  * By default the entry id is the basename of the file, but
  * If id is given, then :
  * - it specifies the id of the new entry (and the basename is not taken as id)
  * - the sha1 of the file is checked
  */
Entry *Entry::loadEntry(const std::string &path, const std::string &id, bool checkId)
{
    // load a given entry
    std::string buf;
    int n = loadFile(path.c_str(), buf);

    if (n < 0) {
        // error loading the file
        LOG_ERROR("Cannot load entry '%s': %s", path.c_str(), strerror(errno));
        return 0;
    }

    // check the sha1, if id is given
    if (checkId) {
        std::string hash = getSha1(buf);
        if (0 != hash.compare(id)) {
            LOG_ERROR("Hash does not match: %s / %s", path.c_str(), id.c_str());
            return 0;
        }
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

    return e;
}


std::string Entry::getMessage() const
{
    return getProperty(properties, K_MESSAGE);
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


Entry *Entry::createNewEntry(const PropertiesMap &props, const std::string &author, const Entry *eParent)
{
    Entry *e = new Entry();
    e->properties = props;
    e->author = author;
    e->ctime = time(0);

    if (eParent) e->parent = eParent->id;
    else e->parent = K_PARENT_NULL;

    e->setId();

    return e;
}

std::string Entry::serialize() const
{
    std::ostringstream s;

    s << K_SMIT_VERSION << " " << VERSION << "\n";
    s << K_PARENT << " " << parent << "\n";
    s << K_AUTHOR << " " << author << "\n";
    s << K_CTIME << " " << ctime << "\n";

    std::map<std::string, std::list<std::string> >::const_iterator p;
    for (p = properties.begin(); p != properties.end(); p++) {
        std::string key = p->first;
        std::list<std::string> value = p->second;
        s << serializeProperty(key, value);
    }
    return s.str();
}
