
#include "AuthSha1.h"
#include "logging.h"
#include "parseConfig.h"
#include "identifiers.h"

/** Authenticate
  *
  * @return
  *    0, success
  *   -2, error, password expired
  *   -1, other authentication error
  */
int AuthSha1::authenticate(char *password)
{
    std::string tmp = password;
    tmp += salt;
    std::string sha1 = getSha1(tmp);
    tmp.assign(tmp.size(), 0xFF);
    if (sha1 != hash) {
        LOG_DEBUG("Sha1 do not match %s <> %s", sha1.c_str(), hash.c_str());
        LOG_INFO("Sha1 authentication failure for user '%s'", username.c_str());
        return -1;
    } else {
        return 0;
    }
}

std::string AuthSha1::serialize()
{
    std::string result;
    result += serializeSimpleToken(type) + " ";
    result += serializeSimpleToken(hash);
    if (!salt.empty()) {
        result += " -salt " ;
        result += serializeSimpleToken(salt);
    }
    return result;
}
