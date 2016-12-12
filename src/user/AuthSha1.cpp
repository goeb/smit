
#include "AuthSha1.h"
#include "utils/logging.h"
#include "utils/parseConfig.h"
#include "utils/identifiers.h"

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
    result += serializeSimpleToken(type);
    result += " -hash " + serializeSimpleToken(hash);
    if (!salt.empty()) {
        result += " -salt " ;
        result += serializeSimpleToken(salt);
    }
    return result;
}

Auth *AuthSha1::createCopy() const
{
    AuthSha1 *ah = new AuthSha1(*this);
    return ah;
}


Auth *AuthSha1::deserialize(std::list<std::string> &tokens)
{
    std::string hash;
    std::string salt;
    std::string token;
    while (!tokens.empty()) {
        token = popListToken(tokens);
        if (token == "-hash") hash = popListToken(tokens);
        else if (token == "-salt") salt = popListToken(tokens);
        else {
            LOG_ERROR("Invalid token '%s'", token.c_str());
            return 0;
        }
    }
    if (hash.empty()) {
        LOG_ERROR("AuthSha1: empty hash");
        return 0;
    }

    AuthSha1 *ah = new AuthSha1("", hash, salt);
    return dynamic_cast<Auth*>(ah);
}
