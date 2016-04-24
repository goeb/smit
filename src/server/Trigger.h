
#ifndef _trigger_h
#define _trigger_h

#include "user/session.h"
#include "project/Project.h"

class Trigger {
public:
    static void notifyEntry(const Project &project, const Entry *entry, bool isNewIssue);
    static std::string formatEntry(const Project &project, const Issue &issue, const Entry &entry,
                                   const std::map<std::string, Role> &users, bool isNewIssue);
    static void run(const std::string &program, const std::string &toStdin);

};


#endif
