
#include <ldap.h>
#include <string.h>

#include "authLdap.h"
#include "logging.h"

/** Authenticate against a Kerberos server
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
int ldapAuthenticate(const std::string &dname, const std::string &server,
                     const std::string password)
{
    LOG_DIAG("ldapAuthenticate(%s@%s)", dname.c_str(), server.c_str());

    LDAP *ld;
    int result;

    // Open LDAP Connection
    int r = ldap_initialize(&ld, server.c_str());
    if (r != LDAP_SUCCESS) {
        LOG_ERROR("ldap_initialize error for server '%s': %s", server.c_str(), ldap_err2string(r));
        return -1;
    }
    int version = LDAP_VERSION3;
    r = ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    if (r != LDAP_SUCCESS) {
        LOG_ERROR("ldap_set_option error for server '%s': %s", server.c_str(), ldap_err2string(r));
        ldap_unbind_ext(ld, 0, 0);
        result = -1;
    }

    // copy password in a mutable buffer (ldap API constraint)
    const size_t PASSWD_MAX_SIZE = 512;
    if (password.size() > PASSWD_MAX_SIZE) {
        LOG_ERROR("password too long: %d characters", password.size());
        ldap_unbind_ext(ld, 0, 0);
        return -1;
    }
    char pw[PASSWD_MAX_SIZE+1];
    strncpy(pw, password.c_str(), PASSWD_MAX_SIZE+1);

    struct berval cred;
    cred.bv_len = password.size();
    cred.bv_val = pw;

    struct berval *servcred = 0;

    // User authentication
    r = ldap_sasl_bind_s(ld, dname.c_str(), 0, &cred, 0, 0, &servcred);
    if (r != LDAP_SUCCESS) {
        LOG_ERROR("ldap_simple_bind_s error for '%s': %s", dname.c_str(), ldap_err2string(r));
        result = -1;
    } else {
        LOG_DIAG("Ldap authentication success for user '%s'", dname.c_str());
        result = 0;
    }
    memset(pw, 0, sizeof(pw));
    if (servcred) {
        // free server credentials TODO
    }

    r = ldap_unbind_ext(ld, 0, 0);
    if (r != LDAP_SUCCESS) {
        LOG_ERROR("ldap_unbind error for user '%s': %s",  dname.c_str(), ldap_err2string(r));
    }

    return result;
}
