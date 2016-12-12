#ifndef _notification_h
#define _notification_h

#include <string>
#include <map>
#include <list>
#include <set>

#include "utils/mutexTools.h"
#include "project/Issue.h"
#include "repository/db.h"

enum RuleVerb {
    RV_ANY_CHANGE,
    RV_BECOME_LEAVES_EQUAL
};

// Types of notification policies
#define NOTIFY_POLICY_ALL           "all"
#define NOTIFY_POLICY_NONE          "none"    // default value
#define NOTIFY_POLICY_ME            "me"      // things that concerne the user
#define NOTIFY_POLICY_CUSTOM        "custom"


class NotificationRule {
public:
    std::string propertyName;
    RuleVerb verb;
    std::string value; // used with some verbs
    NotificationRule(): verb(RV_ANY_CHANGE) {}
};

class NotificationPolicyCustom {
public:


    std::list<NotificationRule> rules;
    bool notifyOnNewMessageOrFile; // notify

    // Methods
    int match(IssueCopy oldi, IssueCopy newi);
    NotificationPolicyCustom(): notifyOnNewMessageOrFile(false) {}

};

class Notification {
public:
    std::string email;
    std::string gpgPublicKey; // armored, format of gpg --armor --export ...
    std::string notificationPolicy; // empty value (default) means no notification at all
    NotificationPolicyCustom customPolicy;

    // Methods
    static void load(const std::string &path, Notification &notif);
    static int deleteStorageFile(const std::string &path);
    int store(const std::string &path) const;
    std::string toString() const;
};


#endif
