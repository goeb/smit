/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include "renderingText.h"
#include "db.h"
#include "logging.h"
#include "stringTools.h"

void RText::printProjectList(struct mg_connection *conn, const std::list<std::pair<std::string, std::string> > &pList)
{
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");

    std::list<std::pair<std::string, std::string> >::const_iterator p;
    for (p=pList.begin(); p!=pList.end(); p++) {
        mg_printf(conn, "%s (%s)\n", p->first.c_str(), p->second.c_str());

    }
}

void RText::printIssueList(struct mg_connection *conn, std::vector<struct Issue*> issueList, std::list<std::string> colspec)
{
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");

    // print names of columns
    std::list<std::string>::iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {
        mg_printf(conn, "%s,\t", colname->c_str());
   }
    mg_printf(conn, "\n");

    // list of issues
    std::vector<struct Issue*>::iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        std::list<std::string>::iterator c;
        for (c = colspec.begin(); c != colspec.end(); c++) {
            std::string text;
            std::string column = *c;

            if (column == "id") text = (*i)->id;
            else if (column == "ctime") text = epochToString((*i)->ctime);
            else if (column == "mtime") text = epochToString((*i)->mtime);
            else {
                std::map<std::string, std::list<std::string> >::iterator p;
                std::map<std::string, std::list<std::string> > & properties = (*i)->properties;

                p = properties.find(column);
                if (p != properties.end()) text = toString(p->second);
            }

            mg_printf(conn, "%s,\t", text.c_str());
        }
        mg_printf(conn, "\n");
    }

}

void RText::printIssue(struct mg_connection *conn, const Issue &issue, const std::list<Entry*> &Entries)
{
    LOG_DEBUG("RText::printIssue...");
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");
    mg_printf(conn, "not implemented\r\n");

}
void RText::printView(struct mg_connection *conn, const PredefinedView &pv)
{
    LOG_FUNC();
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");
    mg_printf(conn, "not implemented\r\n");

}
void RText::printListOfViews(struct mg_connection *conn, const Project &pv)
{
    LOG_FUNC();
    mg_printf(conn, "Content-Type: text/plain\r\n\r\n");
    mg_printf(conn, "not implemented\r\n");

}
