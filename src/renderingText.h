#ifndef _renderingText_h
#define _renderingText_h

#include <list>
#include <string>

#include "mongoose.h"
#include "db.h"

class RText {
public:
    static void printProjectList(struct mg_connection *conn, const std::list<std::pair<std::string, std::string> > &pList);
    static void printIssueList(struct mg_connection *conn, std::vector<struct Issue*> issueList, std::list<std::string> colspec);
    static void printIssue(struct mg_connection *conn, const Issue &issue, const std::list<Entry*> &Entries);
    static void printView(struct mg_connection *conn, const PredefinedView &pv);
};

#endif
