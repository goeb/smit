#ifndef _renderingText_h
#define _renderingText_h

#include <list>
#include <string>

#include "HttpContext.h"
#include "repository/db.h"

class RText {
public:
    static void printProjectList(const RequestContext *req, const std::list<ProjectSummary> &pList);
    static void printIssueList(const RequestContext *req, const std::vector<IssueCopy> &issueList, std::list<std::string> colspec);
    static void printIssue(const RequestContext *req, const IssueCopy &issue);
};

#endif
