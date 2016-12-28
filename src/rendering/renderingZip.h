#ifndef _renderingZip_h
#define _renderingZip_h

#include "project/Issue.h"
#include "ContextParameters.h"

class RZip {
public:
    static int printIssue(const ContextParameters &ctx, const IssueCopy &issue);
};



#endif
