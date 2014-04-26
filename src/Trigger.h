
#ifndef _trigger_h
#define _trigger_h

#include "db.h"

class Trigger {
public:
    static void notifyEntry(const Project &project, const std::string issueId, const std::string &entryId);
    static std::string formatEntry(const Project &project, const Issue &issue, const Entry &entry);
    static void run(const std::string &program, const std::string &toStdin);

};


#endif
