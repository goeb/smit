#ifndef _renderingCsv_h
#define _renderingCsv_h

#include <list>
#include <string>

#include "mongoose.h"
#include "db.h"
#include "HttpContext.h"

class RCsv {
public:
    static void printProjectList(MongooseRequestContext *req, const std::list<std::pair<std::string, std::string> > &pList);
    static void printIssueList(MongooseRequestContext *req, std::vector<struct Issue*> issueList, std::list<std::string> colspec);
};

#endif
