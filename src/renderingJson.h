#ifndef _renderingJson_h
#define _renderingJson_h

#include <list>
#include <string>

#include "db.h"
#include "HttpContext.h"

class RJson {
public:
    static void printIssueList(const RequestContext *req, const std::vector<IssueCopy> &issueList,
                               std::list<std::string> colspec);
};

#endif
