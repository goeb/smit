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
#define K_NOTIFY_POLICY_ALL           "all"
#define K_NOTIFY_POLICY_NONE          "none"
#define K_NOTIFY_POLICY_CUSTOM        "custom"


class NotificationRule {
public:
    std::string propertyName;
    RuleVerb verb;
    std::string value; // used with some verbs
};

class NotificationPolicyCustom {
public:


    std::list<NotificationRule> rules;
    bool notifyOnewMessageOrFile; // notify

    // Methods
    int match(IssueCopy oldi, IssueCopy newi);
    NotificationPolicyCustom(): notifyOnewMessageOrFile(false) {}

};

class Notification {
public:
    std::string email;
    std::string gpgPublicKey; // armored, format of gpg --armor --export ...
    std::string notificationPolicy;
    NotificationPolicyCustom customPolicy;

    // Methods
    Notification *load(const std::string &path);
    int store(const std::string &path) const;
};


#endif
