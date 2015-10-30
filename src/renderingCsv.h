#ifndef _renderingCsv_h
#define _renderingCsv_h

#include <list>
#include <string>

#include "db.h"
#include "HttpContext.h"

class RCsv {
public:
    static void printProjectList(const RequestContext *req, const std::list<std::pair<std::string, std::string> > &pList);
    static void printIssueList(const RequestContext *req, const std::vector<IssueCopy> &issueList,
                               std::list<std::string> colspec, const char *separator);
};

#endif
