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
    }
    issuesJson += "]";
    req->printf("%s", issuesJson.c_str());
}

