
#include <sstream>

#include "notification.h"
#include "utils/filesystem.h"
#include "utils/parseConfig.h"
#include "utils/logging.h"
#include "global.h"

int NotificationPolicyCustom::match(IssueCopy oldi, IssueCopy newi)
{
    return -1;
}

#define K_EMAIL                       "email"
#define K_GPG_KEY                     "gpgPublicKey"
#define K_NOTIFY_POLICY               "notifyPolicy"
#define K_NOTIFY_CUSTOM               "notifyCustom"
#define K_NOTIFY_CUSTOM_OPT_MSG_FILE  "-messageOrFile"
#define K_NOTIFY_CUSTOM_OPT_PROP_VALUE   "-propertyValue"
#define K_NOTIFY_CUSTOM_OPT_ANY       "-propertyAnyChange"

/** Example of config file:
 * email "foo@example.com"
 * gpgPublicKey < boundary------
 * ...
 * boundary------
 * notifyPolicy none
 * notifyPolicy all
 * notifyPolicy custom
 * notifyCustom -messageOrFile
 * notifyCustom -propertyValue <property-name> <value>
 * notifyCustom -propertyAnyChange <property-name>
 */

void Notification::load(const std::string &path, Notification &notif)
{
    // load a given entry
    std::string buf;
    int n = loadFile(path.c_str(), buf);

    if (n < 0) {
        // error loading the file
        LOG_INFO("Cannot load notification '%s': %s", path.c_str(), strerror(errno));
    }

    std::list<std::list<std::string> > lines = parseConfigTokens(buf.c_str(), buf.size());

    std::list<std::list<std::string> >::iterator line;
    int lineNum = 0;
    for (line=lines.begin(); line != lines.end(); line++) {
        lineNum++;
        // each line should be a key / value pair
        if (line->empty()) continue; // ignore this line

        std::string key = line->front();
        line->pop_front(); // remove key from tokens
        if (line->size() == 0) {
            // error
            LOG_ERROR("Short line in '%s', line %d", path.c_str(), lineNum);
            continue; // skip the erreonous line
        }
        std::string firstArg = line->front();
        line->pop_front(); // remove the first arg

        if (key == K_EMAIL) notif.email = firstArg;
        else if (key == K_GPG_KEY) notif.gpgPublicKey = firstArg;
        else if (key == K_NOTIFY_POLICY) notif.notificationPolicy = firstArg;
        else if (key == K_NOTIFY_CUSTOM) notif.notificationPolicy = firstArg;
        else if (notif.notificationPolicy == K_NOTIFY_POLICY_CUSTOM) {
            if (firstArg == K_NOTIFY_CUSTOM_OPT_MSG_FILE) {
                notif.customPolicy.notifyOnNewMessageOrFile = true;

            } else if (firstArg == K_NOTIFY_CUSTOM_OPT_PROP_VALUE) {
                if (line->size() != 2) {
                    LOG_ERROR("Malformed line in '%s', line %d", path.c_str(), lineNum);
                    continue; // skip the erreonous line
                }
                NotificationRule nr;
                nr.propertyName = line->front();
                nr.verb = RV_BECOME_LEAVES_EQUAL;
                nr.value = line->back();
                notif.customPolicy.rules.push_back(nr);

            } else if (firstArg == K_NOTIFY_CUSTOM_OPT_ANY) {
                if (line->size() != 1) {
                    LOG_ERROR("Malformed line in '%s', line %d", path.c_str(), lineNum);
                    continue; // skip the erreonous line
                }
                NotificationRule nr;
                nr.propertyName = line->front();
                nr.verb = RV_ANY_CHANGE;
                notif.customPolicy.rules.push_back(nr);

            } else {
                LOG_ERROR("Malformed line in '%s', line %d", path.c_str(), lineNum);
                continue; // skip the erreonous line
            }

        }
    }
}

int Notification::store(const std::string &path) const
{
    std::ostringstream serialized;
#define K_NOTIFY_CUSTOM               "notifyCustom"
#define K_NOTIFY_CUSTOM_OPT_MSG_FILE  "-messageOrFile"
#define K_NOTIFY_CUSTOM_OPT_PROP_VALUE   "-propertyValue"
#define K_NOTIFY_CUSTOM_OPT_ANY       "-propertyAnyChange"

    serialized << K_EMAIL <<  " " <<  email <<  "\n";
    serialized <<  K_GPG_KEY << " " << gpgPublicKey << "\n";
    serialized <<  K_NOTIFY_POLICY << " " << notificationPolicy << "\n";
    if (notificationPolicy == K_NOTIFY_POLICY_CUSTOM) {
        if (customPolicy.notifyOnNewMessageOrFile) {
            serialized << K_NOTIFY_CUSTOM << " " << K_NOTIFY_CUSTOM_OPT_MSG_FILE;
            serialized << "\n";
        }

        std::list<NotificationRule>::const_iterator rule;
        FOREACH(rule, customPolicy.rules) {
            if (rule->verb == RV_ANY_CHANGE) {
                serialized << K_NOTIFY_CUSTOM << " " << K_NOTIFY_CUSTOM_OPT_PROP_VALUE << " ";
                serialized << serializeSimpleToken(rule->propertyName) << " ";
                serialized << serializeSimpleToken(rule->value);
                serialized << "\n";
            } else {
                serialized << K_NOTIFY_CUSTOM << " " << K_NOTIFY_CUSTOM_OPT_ANY << " ";
                serialized << serializeSimpleToken(rule->propertyName);
                serialized << "\n";
            }
        }
    }

    int r = writeToFile(path, serialized.str());
    return r;
}
