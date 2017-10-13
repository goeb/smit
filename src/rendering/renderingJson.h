#ifndef _renderingJson_h
#define _renderingJson_h

#include <vector>
#include <string>

#include "server/HttpContext.h"
#include "project/Issue.h"

class RJson {
public:
    static void printIssueList(const RequestContext *req, const std::vector<IssueCopy> &issueList,
                               std::list<std::string> colspec);
    static void printEntryList(const RequestContext *req, const std::vector<Entry> &entries);
};

#endif
