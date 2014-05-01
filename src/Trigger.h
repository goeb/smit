
#ifndef _trigger_h
#define _trigger_h

#include "db.h"
#include "session.h"

class Trigger {
public:
    static void notifyEntry(const Project &project, const std::string issueId, const std::string &entryId);
    static std::string formatEntry(const Project &project, const Issue &issue, const Entry &entry,
                                   const std::map<std::string, Role> &users);
    static void run(const std::string &program, const std::string &toStdin);

};


#endif
