#ifndef _renderingCsv_h
#define _renderingCsv_h

#include <list>
#include <string>

#include "server/HttpContext.h"
#include "repository/db.h"

class RCsv {
public:
    static void printProjectList(const RequestContext *req, const std::list<ProjectSummary> &pList);
    static void printIssueList(const RequestContext *req, const std::vector<IssueCopy> &issueList,
                               std::list<std::string> colspec);
    static void printIssue(const RequestContext *req, const IssueCopy &issue, const ProjectConfig &config);

};

#endif
