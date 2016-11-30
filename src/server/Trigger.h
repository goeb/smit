
#ifndef _trigger_h
#define _trigger_h

#include "user/session.h"
#include "project/Project.h"

struct Recipient {
    std::string email;
    std::string gpgPubKey;
};

class Trigger {
public:
    static void notifyEntry(const Project &project, const Entry *entry,
                            const IssueCopy &oldIssue, const std::list<Recipient> &recipients);

private:
    static std::string formatEntry(const Project &project, const IssueCopy &oldIssue, const Entry &entry,
                                   const std::list<Recipient> &recipients);
    static void run(const std::string &program, const std::string &toStdin);

};

#endif
