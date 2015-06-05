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

#include "renderingText.h"
#include "db.h"
#include "logging.h"
#include "stringTools.h"
#include "global.h"

void RText::printProjectList(const RequestContext *req, const std::list<std::pair<std::string, std::string> > &pList)
{
    req->printf("Content-Type: text/plain\r\n\r\n");

    std::list<std::pair<std::string, std::string> >::const_iterator p;
    for (p=pList.begin(); p!=pList.end(); p++) {
        req->printf("%s %s %s\n", p->first.c_str(),
                    p->second.c_str(), p->first.c_str());

    }
}

void RText::printIssueList(const RequestContext *req, std::vector<const Issue*> issueList, std::list<std::string> colspec)
{
    req->printf("Content-Type: text/plain\r\n\r\n");

    // print names of columns
    std::list<std::string>::iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {
        if (colname != colspec.begin()) req->printf(",\t");
        req->printf("%s", colname->c_str());
    }
    req->printf("\n");

    // list of issues
    std::vector<const Issue*>::const_iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        std::list<std::string>::iterator c;
        for (c = colspec.begin(); c != colspec.end(); c++) {
            std::string text;
            std::string column = *c;
            if (c != colspec.begin()) req->printf(",\t");

            if (column == "id") text = (*i)->id;
            else if (column == "ctime") text = epochToString((*i)->ctime);
            else if (column == "mtime") text = epochToString((*i)->mtime);
            else {
                std::map<std::string, std::list<std::string> >::const_iterator p;
                const std::map<std::string, std::list<std::string> > & properties = (*i)->properties;

                p = properties.find(column);
                if (p != properties.end()) text = toString(p->second);
            }

            req->printf("%s", text.c_str());
        }
        req->printf("\n");
    }

}

void RText::printIssue(const RequestContext *req, const Issue &issue)
{
    LOG_DEBUG("RText::printIssue...");
    req->printf("Content-Type: text/plain\r\n\r\n");
    Entry *e = issue.first;
    while (e) {
        req->printf("%s\n", e->id.c_str());
        e = e->getNext();
    }
}

