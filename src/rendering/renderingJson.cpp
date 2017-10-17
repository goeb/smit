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

#include <sstream>

#include "global.h"
#include "renderingJson.h"
#include "utils/logging.h"
#include "utils/jTools.h"

#define CONTENT_TYPE_JSON "application/json"

void RJson::printIssueList(const RequestContext *req, const std::vector<IssueCopy> &issueList,
                           std::list<std::string> colspec)
{
    req->printf("Content-Type: " CONTENT_TYPE_JSON "\r\n\r\n");

    // list of issues
    std::string issuesJson = "[";

    std::vector<IssueCopy>::const_iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        // render an issue as an array
        // Eg: [ "1234", "open" ]
        std::string singleIssueJson = "[";

        std::list<std::string>::iterator c;
        for (c = colspec.begin(); c != colspec.end(); c++) {

            std::string text;
            std::string column = *c;

            if (column == "id") text = i->id;
            else if (column == "p") text = i->project;
            else if (column == "ctime") text = toString(i->ctime);
            else if (column == "mtime") text = toString(i->mtime);
            else {
                std::map<std::string, std::list<std::string> >::const_iterator p;
                const std::map<std::string, std::list<std::string> > & properties = i->properties;

                p = properties.find(column);
                if (p != properties.end()) text = toString(p->second);
            }
            if (c != colspec.begin()) singleIssueJson += ", ";
            singleIssueJson += toJsonString(text);

        }
        singleIssueJson += "]";
        if (i != issueList.begin()) issuesJson += ",\n";
        issuesJson += singleIssueJson;

        // TODO do printf here, in order to prevent accumulating
        // a huge list that is printed only at the end.
    }
    issuesJson += "]";

    req->printf("%s", issuesJson.c_str());
}

static std::string propertyToJson(PropertiesIt pit)
{
    std::string result = toJsonString(pit->first) + ":";
    if (pit->second.size() == 1) {
        // string value
        result += toJsonString(pit->second.front());
    } else {
        // make an array
        result += toJsonArray(pit->second);
    }
    return result;
}

/** Print a list of entries
 *
 *  [ { "entry_header": { ... },
 *      "properties": { ... },
 *      "message": "...",
 *      ...
 *    },
 *    ...
 *  ]
 */
static void printEntries(const RequestContext *req, const std::vector<Entry> &entries)
{
    req->printf("[");

    std::vector<Entry>::const_iterator e;

    for (e=entries.begin(); e!=entries.end(); e++) {

        if (e!=entries.begin()) req->printf(",");

        std::string entryJson = "{";

        // entry_header
        entryJson += toJsonString("entry_header");
        entryJson += ":{" + toJsonString("id") + ":" + toJsonString(e->id);
        entryJson += "," + toJsonString("author") + ":" + toJsonString(e->author);
        entryJson += "," + toJsonString("ctime") + ":" + toString(e->ctime);
        entryJson += "," + toJsonString("parent") + ":";
        if (e->parent == K_PARENT_NULL) entryJson += J_NULL;
        else entryJson += toJsonString(e->parent);
        entryJson += "}";

        entryJson += ",";

        std::string message;
        std::string amend;
        std::list<std::string> files;

        // properties
        entryJson += toJsonString("properties") + ":{";
        PropertiesIt p;
        const PropertiesMap & properties = e->properties;
        bool needsComma = false;
        for (p=properties.begin(); p!=properties.end(); p++) {
            std::string pname = p->first;
            if (pname == K_MESSAGE && p->second.size()) message = p->second.front();
            else if (pname == K_AMEND && p->second.size()) amend = p->second.front();
            else if (pname == K_FILE) files = p->second;
            else {
                // regular properties
                if (needsComma) entryJson += ",";
                entryJson += propertyToJson(p);
                needsComma = true;
            }
        }
        entryJson += "}";

        // message
        if (message.size()) {
            entryJson += "," + toJsonString("message") + ":";
            entryJson += toJsonString(message);
        }

        // amend
        if (amend.size()) {
            entryJson += "," + toJsonString("amend") + ":";
            entryJson += toJsonString(amend);
        }

        // files
        if (files.size()) {
            entryJson += "," + toJsonString("files") + ":";
            entryJson += toJsonArray(files);
        }

        entryJson += "}";
        req->printf("%s", entryJson.c_str());
    }

    req->printf("]");
}

/** Print an issue
 *
 *  { "properties": { ... }
 *    "entries": [ ... ]
 *  }
 */
void RJson::printIssue(const RequestContext *req, const IssueCopy &issue)
{
    req->printf("Content-Type: " CONTENT_TYPE_JSON "\r\n\r\n");

    req->printf("{\"properties\":");
    std::string issueProperties = "{";
    PropertiesIt pit;
    FOREACH(pit, issue.properties) {
        if (pit != issue.properties.begin()) issueProperties += ",";
        issueProperties += propertyToJson(pit);

    }
    issueProperties += "}";
    req->printf("%s", issueProperties.c_str());

    req->printf(",\"entries\":");

    std::vector<Entry> entries;
    Entry *e = issue.first;
    while (e) {
        entries.push_back(*e);
        e = e->getNext();
    }
    printEntries(req, entries);
    req->printf("}");
}

void RJson::printEntryList(const RequestContext *req, const std::vector<Entry> &entries)
{
    req->printf("Content-Type: " CONTENT_TYPE_JSON "\r\n\r\n");
    // list of entries
    printEntries(req, entries);
}

