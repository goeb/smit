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

#include <stdlib.h>

#include "global.h"
#include "renderingCsv.h"
#include "repository/db.h"
#include "utils/logging.h"
#include "utils/stringTools.h"

static char *getSep() {
    static char *CsvSeparator = getenv("CSV_SEPARATOR");
    if (!CsvSeparator) return (char*)",";
    else return CsvSeparator;
}

/** Double quoting is needed when the string contains:
  *    a " character
  *    \n or \r
  *    a comma ,
  */
std::string doubleQuoteCsv(const std::string &input)
{
    char *separator = getSep();

    if (input.find_first_of("\n\r\"") == std::string::npos &&
            input.find(separator) == std::string::npos ) return input; // no need for quotes

    size_t n = input.size();
    std::string result = "\"";

    size_t i;
    for (i=0; i<n; i++) {
        if (input[i] == '"') result += "\"\"";
        else result += input[i];
    }
    result += '"';
    return result;
}

static void printHeaders(const RequestContext *req, const char *filename)
{
    req->printf("Content-Type: text/csv\r\n");
    req->printf("Content-Disposition: attachment; filename=\"%s.csv\"\r\n", filename);
    req->printf("\r\n\r\n");
}

void RCsv::printProjectList(const RequestContext *req, const std::list<ProjectSummary> &pList)
{
    printHeaders(req, "projects");

    char *separator = getSep();

    std::list<ProjectSummary>::const_iterator p;
    for (p=pList.begin(); p!=pList.end(); p++) {
        req->printf("%s", doubleQuoteCsv(p->name).c_str());
        req->printf("%s", separator);
        req->printf("%s\r\n", doubleQuoteCsv(p->myRole).c_str());
    }
}

void RCsv::printIssueList(const RequestContext *req, const std::vector<IssueCopy> &issueList,
                          std::list<std::string> colspec)
{
    printHeaders(req, "issues");

    char *separator = getSep();

    // print names of columns
    std::list<std::string>::iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {
        if (colname != colspec.begin()) req->printf("%s", separator);
        req->printf("%s", doubleQuoteCsv(*colname).c_str());
    }
    req->printf("\r\n");


    // list of issues
    std::vector<IssueCopy>::const_iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        std::list<std::string>::iterator c;
        for (c = colspec.begin(); c != colspec.end(); c++) {
            if (c != colspec.begin()) req->printf("%s", separator);

            std::string text;
            std::string column = *c;

            if (column == "id") text = i->id;
            else if (column == "p") text = i->project;
            else if (column == "ctime") text = epochToString(i->ctime);
            else if (column == "mtime") text = epochToString(i->mtime);
            else {
                std::map<std::string, std::list<std::string> >::const_iterator p;
                const std::map<std::string, std::list<std::string> > & properties = i->properties;

                p = properties.find(column);
                if (p != properties.end()) text = toString(p->second);
            }

            req->printf("%s", doubleQuoteCsv(text).c_str());
        }
        req->printf("\r\n");
    }
}

void RCsv::printIssue(const RequestContext *req, const IssueCopy &issue, const ProjectConfig &config)
{
    LOG_DEBUG("RCsv::printIssue...");
    printHeaders(req, issue.id.c_str());

    char *separator = getSep();

    // print columns headers

    // author
    req->printf("author");

    // ctime
    req->printf("%s", separator);
    req->printf("ctime");

    std::list<std::string> props = config.getPropertiesNames();
    std::list<std::string>::const_iterator p;
    FOREACH(p, props) {
        req->printf("%s", separator);
        req->printf("%s", doubleQuoteCsv(*p).c_str());
    }

    // message
    req->printf("%s", separator);
    req->printf("message");

    // files
    req->printf("%s", separator);
    req->printf("files");

    req->printf("\r\n"); // new line

    // print entries
    PropertiesIt pit;
    Entry *e = issue.first;
    while (e) {
        // author
        req->printf("%s", doubleQuoteCsv(e->author).c_str());
        // ctime
        req->printf("%s", separator);
        req->printf("%d", e->ctime);

        FOREACH(p, props) {
            req->printf("%s", separator);
            pit = e->properties.find(*p);
            if (pit != e->properties.end()) {
                req->printf("%s", doubleQuoteCsv(toString(pit->second)).c_str());
            }
        }

        // message
        req->printf("%s", separator);
        pit = e->properties.find(K_MESSAGE);
        if (pit != e->properties.end()) {
            req->printf("%s", doubleQuoteCsv(toString(pit->second)).c_str());
        }

        // files
        req->printf("%s", separator);
        pit = e->properties.find(K_FILE);
        if (pit != e->properties.end()) {
            req->printf("%s", doubleQuoteCsv(toString(pit->second)).c_str());
        }

        req->printf("\r\n"); // new line
        e = e->getNext();
    }
}

