
#include <sstream>

#include "notification.h"
#include "utils/filesystem.h"
#include "utils/parseConfig.h"
#include "utils/logging.h"
#include "global.h"

#define K_EMAIL                       "email"
#define K_GPG_KEY                     "gpgPublicKey"
#define K_NOTIFY_POLICY               "notifyPolicy"
#define K_NOTIFY_CUSTOM               "notifyCustom"
#define K_NOTIFY_CUSTOM_OPT_MSG_FILE  "-messageOrFile"
#define K_NOTIFY_CUSTOM_OPT_PROP_VALUE   "-propertyValue"
#define K_NOTIFY_CUSTOM_OPT_ANY       "-propertyAnyChange"

/** Load a notification config file
 */
void Notification::load(const std::string &path, Notification &notif)
{
    // load a given entry
    std::string buf;
    int n = loadFile(path.c_str(), buf);

    if (n < 0) {
        // error loading the file
        LOG_DIAG("Cannot load notification '%s': %s", path.c_str(), strerror(errno));
        return;
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
        else if (notif.notificationPolicy == NOTIFY_POLICY_CUSTOM) {
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

int Notification::deleteStorageFile(const std::string &path)
{
    int result = 0;
    int ret = unlink(path.c_str());
    if (ret < 0) {
        if (errno != ENOENT) {
            LOG_ERROR("Cannot remove notification file '%s': %s", path.c_str(), STRERROR(errno));
            result = -1;
        }
        // ENOENT is not an error, as a user that never
        // configured notifications will not have a notification file
    }
    return result;
}

int Notification::store(const std::string &path) const
{
    std::ostringstream serialized;

    if (email.empty() && gpgPublicKey.empty()) {
        int ret = deleteStorageFile(path);
        if (ret != 0) return -1;

        return 0;
    }

    serialized << K_EMAIL <<  " " <<  serializeSimpleToken(email) <<  "\n";
    serialized << K_GPG_KEY << " " << serializeSimpleToken(gpgPublicKey) << "\n";
    serialized << K_NOTIFY_POLICY << " " << serializeSimpleToken(notificationPolicy) << "\n";

    // NOTIFY_POLICY_CUSTOM not supported at the moment

    int r = writeToFile(path, serialized.str());
    return r;
}

std::string Notification::toString() const
{
    if (email.empty()) return "";

    std::string result = email;
    if (!gpgPublicKey.empty()) result += ", GPG";
    result += ", " + notificationPolicy;
    return result;
}

