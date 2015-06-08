#ifndef _renderingText_h
#define _renderingText_h

#include <list>
#include <string>

#include "db.h"
#include "HttpContext.h"

class RText {
public:
    static void printProjectList(const RequestContext *req, const std::list<std::pair<std::string, std::string> > &pList);
    static void printIssueList(const RequestContext *req, std::vector<Issue> &issueList, std::list<std::string> colspec);
    static void printIssue(const RequestContext *req, const Issue &issue);
};

#endif
