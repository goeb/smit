
#include <ldap.h>
#include <string.h>

#include "AuthLdap.h"
#include "utils/logging.h"
#include "utils/parseConfig.h"

/** Authenticate against a LDAP server
  *
  * @param dname
  *     Distinguished name
  *     Eg: "uid=John Doo,ou=people,dc=example,dc=com"
  *
  * @param server
  *     Example: ldap://example.com:389
  *
  * @return
  *    0 success
  *   -1 error
  */
int AuthLdap::authenticate(const char *password)
{
    LOG_DIAG("ldapAuthenticate(%s@%s)", dname.c_str(), uri.c_str());

    LDAP *ld;
    int result;

    // Open LDAP Connection
    int r = ldap_initialize(&ld, uri.c_str());
    if (r != LDAP_SUCCESS) {
        LOG_ERROR("ldap_initialize error for server '%s': %s", uri.c_str(), ldap_err2string(r));
        return -1;
    }
    int version = LDAP_VERSION3;
    r = ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    if (r != LDAP_SUCCESS) {
        LOG_ERROR("ldap_set_option error for server '%s': %s", uri.c_str(), ldap_err2string(r));
        ldap_unbind_ext(ld, 0, 0);
        result = -1;
    }

    struct berval cred;
    cred.bv_len = strlen(password);
    cred.bv_val = strdup(password);

    struct berval *servcred = 0;

    // User authentication
    r = ldap_sasl_bind_s(ld, dname.c_str(), 0, &cred, 0, 0, &servcred);
    free(cred.bv_val);
    if (r != LDAP_SUCCESS) {
        LOG_ERROR("ldap_simple_bind_s error for '%s': %s", dname.c_str(), ldap_err2string(r));
        result = -1;
    } else {
        LOG_DIAG("Ldap authentication success for user '%s'", dname.c_str());
        result = 0;
    }
    if (servcred) {
        // free server credentials TODO
    }

    r = ldap_unbind_ext(ld, 0, 0);
    if (r != LDAP_SUCCESS) {
        LOG_ERROR("ldap_unbind error for user '%s': %s",  dname.c_str(), ldap_err2string(r));
    }

    return result;
}

std::string AuthLdap::serialize()
{
    std::string result;
    result += serializeSimpleToken(AUTH_LDAP);
    result += " -uri " + serializeSimpleToken(uri);
    result += " -dname " + serializeSimpleToken(dname);
    return result;
}

Auth *AuthLdap::deserialize(std::list<std::string> &tokens)
{
    std::string uri;
    std::string dname;
    std::string token;
    while (!tokens.empty()) {
        token = popListToken(tokens);
        if (token == "-uri") uri = popListToken(tokens);
        else if (token == "-dname") dname = popListToken(tokens);
        else {
            LOG_ERROR("Invalid token '%s'", token.c_str());
            return 0;
        }
    }
    if (uri.empty()) {
        LOG_ERROR("AuthLdap: empty uri");
        return 0;
    }
    if (dname.empty()) {
        LOG_ERROR("AuthLdap: empty dname");
        return 0;
    }

    return new AuthLdap("", uri, dname);
}


Auth *AuthLdap::createCopy() const
{
    AuthLdap *ah = new AuthLdap(*this);
    return ah;
}

std::string AuthLdap::getParameter(const char *param)
{
    if (0 == strcmp(param, "uri")) return uri;
    if (0 == strcmp(param, "dname")) return dname;

    return "";
}
