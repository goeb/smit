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

#include "renderingCsv.h"
#include "db.h"
#include "logging.h"
#include "stringTools.h"

/** Double quoting is needed when the string contains:
  *    a " character
  *    \n or \r
  *    a comma ,
  */
std::string doubleQuoteCsv(const std::string &input, const char *separator)
{
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


void RCsv::printProjectList(const RequestContext *req, const std::list<std::pair<std::string, std::string> > &pList)
{
    req->printf("Content-Type: text/plain\r\n\r\n");

    std::list<std::pair<std::string, std::string> >::const_iterator p;
    for (p=pList.begin(); p!=pList.end(); p++) {
        req->printf("%s,%s\r\n", doubleQuoteCsv(p->first, ",").c_str(), doubleQuoteCsv(p->second, ",").c_str());
    }
}

void RCsv::printIssueList(const RequestContext *req, const std::vector<IssueCopy> &issueList,
                          std::list<std::string> colspec, const char *separator)
{
    req->printf("Content-Type: text/plain\r\n\r\n");

    if (!separator || separator[0] == 0) separator = ","; // comma, for Comma-Separated Values

    // print names of columns
    std::list<std::string>::iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {
        if (colname != colspec.begin()) req->printf(",");
        req->printf("%s", doubleQuoteCsv(*colname, separator).c_str());
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

            req->printf("%s", doubleQuoteCsv(text, separator).c_str());
        }
        req->printf("\r\n");
    }
}

