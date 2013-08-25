#ifndef _renderingHtml_h
#define _renderingHtml_h

#include <list>
#include <string>

#include "mongoose.h"
#include "db.h"

class RHtml {
public:
    static void printHeader(struct mg_connection *conn, const char *project);
    static void printFooter(struct mg_connection *conn, const char *project);

    static void printProjectList(struct mg_connection *conn, const std::list<std::string> &pList);
    static void printIssueList(struct mg_connection *conn, const char *project, std::list<Issue*> issueList, std::list<ustring> colspec);
    static void printIssue(struct mg_connection *conn, const char *project, const Issue &issue, const std::list<Entry*> &Entries);

};

#endif
