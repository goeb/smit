#ifndef _renderingText_h
#define _renderingText_h

#include <list>
#include <string>

#include "mongoose.h"
#include "db.h"
#include "HttpContext.h"

class RText {
public:
    static void printProjectList(MongooseRequestContext *req, const std::list<std::pair<std::string, std::string> > &pList);
    static void printIssueList(MongooseRequestContext *req, std::vector<struct Issue*> issueList, std::list<std::string> colspec);
    static void printIssue(MongooseRequestContext *req, const Issue &issue);
};

#endif
