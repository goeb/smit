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


/** Load an entry from a string
  *
  * @param data
  *
  * The format of data is the one returned by:
  *      git log --format=raw --notes
  * Example of data:
  * commit cb64638ed0b606095fd78f50c03b0e28c7827a11
  * tree 6f3b1c4bdbf1a2eef2d752dda971ef51bdd2f631
  * author homer <> 1386451794 +0100
  * committer homer <> 1511301253 +0100
  *
  *     convert_v3_to_v4_issues
  *
  *     msg <
  *     msg uerbaque, **nec placidam membris** dat cura quietem.
  *     msg Postera Phoebea lustrabat lampade terras,
  *     msg umentemque Aurora polo dimouerat umbram,
  *     msg cum sic unanimam adloquitur male sana sororem:
  *     msg "Anna soror, quae me suspensam insomnia terrent!
  *     property due_date "next week"
  *     property in_charge homer
  *     property status open
  *     property summary "At regina graui"
  *
  *     smit-v3-id: zQp6nXdMe4EbIlG9wFuUqwtSZG4
  */
Entry *Entry::loadEntry(std::string data)
{
    Entry *e = new Entry;

    // extract the entry id (ie: commit id)
    std::string commitKey = popToken(data, ' '); // should be "commit", not verified...
    e->id = popToken(data, '\n');

    if (commitKey != "commit" || e->id.size() != 40) {
        LOG_ERROR("Invalid entry: %s", data.c_str());
        delete e;
        return 0;
    }

    std::string key;
    std::string tree;
    bool inNotesPart = false;
    std::string msg; // at most one message is supported in an entry
    std::string multilineValue; // at most one at a time (not interleaved)
    std::string multilineProperty;

    while (!data.empty()) {

        std::string line = popToken(data, '\n');
        if (line.empty()) continue;

        if (line[0] != ' ') {
            key = popToken(line, ' ');
            if (key == "tree") tree = line;
            else if (key == "parent") e->parent = line;
            else if (key == "Notes:") inNotesPart = true;
            else if (key == "author") {
                // take the author name until the first '<'
                e->author = popToken(line, '<');
                trim(e->author);
                popToken(line, ' '); // remove the email
                std::string ctimeStr = popToken(line, ' ');
                e->ctime = atoi(ctimeStr.c_str());
            }

        } else if (inNotesPart) {
            // in notes part
            key = popToken(line, ' ');
            if (key == "tag") {
                // TODO xxxxxxxxxxxxxxxxxxxxxxxxxx
            }
        } else {
            // in the body part

            // do not trim now the rest of the line, as multilines
            // need their spaces (indentation, etc.)
            key = popToken(line, ' ', false);
            if (key == "amend") {
                trim(line);
                e->properties[K_AMEND].push_back(line);

            } else if (key == "msg") {
                // remove one space -- the space that separates the "msg" keyword
                // from the rest iof the line.
                // concatenate with previous msg lines
                if (!msg.empty()) msg += '\n';
                if (!line.empty()) msg += line.substr(1);

            } else if (key == "property") {
                // do not trim now the rest of the line, as multilines
                // need their spaces (indentation, etc.)
                std::string propertyId = popToken(line, ' ', false);

                if (propertyId.empty()) continue;

                if (!multilineProperty.empty()) {
                    // manage a pending multiline property

                    if (propertyId == multilineProperty) {
                        // remove one space -- the space that separates the "msg" keyword
                        // from the rest iof the line.

                        // concatenate with previous
                        if (!multilineValue.empty()) multilineValue += '\n';
                        if (!line.empty()) multilineValue += line.substr(1);
                        continue;
                    }

                    // reached the end of the multilineProperty.
                    // record it.
                    e->properties[multilineProperty].push_back(multilineValue);
                    multilineProperty = "";
                    multilineValue = "";
                }

                if (line == " <") {
                    // start a new multiline property
                    multilineProperty = propertyId;
                    continue;
                }

                // regular property (on a single line)
                std::list<std::list<std::string> > subLines = parseConfigTokens(line.c_str(), line.size());
                std::list<std::string> subLine;
                if (subLines.empty()) {
                    // ok, no value here
                } else if (subLines.size() == 1) {
                    subLine = subLines.front();
                } else {
                    LOG_ERROR("loadEntry: invalid line for entry %s: %s",
                              e->id.c_str(), line.c_str());
                    continue;
                }
                e->properties[propertyId] = subLine;
                LOG_DEBUG("e->properties[%s]=%s", propertyId.c_str(), toString(subLine).c_str());
            }
        }
    }

    if (!msg.empty()) {
        LOG_DEBUG("e->properties[K_MESSAGE]=%s", msg.c_str());
        e->properties[K_MESSAGE].push_back(msg);
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

    LOG_ERROR("TODO remove this e->setId()");
    e->setId(); // TODO remove

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
