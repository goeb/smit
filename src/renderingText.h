#ifndef _renderingText_h
#define _renderingText_h

#include <list>
#include <string>

#include "mongoose.h"
#include "ustring.h"
#include "db.h"

class RText {
public:
    static void printProjectList(struct mg_connection *conn, const std::list<std::string> &pList);
    static void printIssueList(struct mg_connection *conn, std::list<struct Issue*> issueList, std::list<ustring> colspec);
    static void printIssue(struct mg_connection *conn, const Issue &issue, const std::list<Entry*> &Entries);

};

#endif
