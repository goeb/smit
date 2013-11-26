#ifndef _renderingCsv_h
#define _renderingCsv_h

#include <list>
#include <string>

#include "mongoose.h"
#include "db.h"

class RCsv {
public:
    static void printProjectList(struct mg_connection *conn, const std::list<std::pair<std::string, std::string> > &pList);
    static void printIssueList(struct mg_connection *conn, std::vector<struct Issue*> issueList, std::list<std::string> colspec);
};

#endif
