
#include <ldap.h>
#include <string.h>

#include "authLdap.h"
#include "logging.h"

/** Authenticate against a Kerberos server
  *
  * @param username
  *     Common name
  *
  * @param server
  *     Example: ldap://example.com:389
  *
  * @return
  *    0 success
  *   -1 error
  */
int ldapAuthenticate(const std::string &username, const std::string &server, const std::string password)
{
    LOG_DIAG("ldapAuthenticate(%s@%s)", username.c_str(), server.c_str());

    LDAP *ld;
    int result;

    // Open LDAP Connection
    int r = ldap_initialize(&ld, server.c_str());
    if (r != LDAP_SUCCESS) {
        LOG_ERROR("ldap_initialize error: %s", ldap_err2string(r));
        return -1;
    }

    // copy password in a mutable buffer (ldap API constraint)
    const size_t PASSWD_MAX_SIZE = 512;
    if (password.size() > PASSWD_MAX_SIZE) {
        LOG_ERROR("password too long: %d characters", password.size());
        r = ldap_unbind_ext(ld, 0, 0);
        return -1;
    }
    char pw[PASSWD_MAX_SIZE+1];
    strncpy(pw, password.c_str(), PASSWD_MAX_SIZE+1);

    struct berval cred;
    cred.bv_len = password.size();
    cred.bv_val = pw;

    // User authentication
    r = ldap_sasl_bind_s(ld, username.c_str(), LDAP_SASL_SIMPLE, &cred, 0, 0, 0);
    if (r != LDAP_SUCCESS) {
        LOG_ERROR("ldap_simple_bind_s error for '%s': %s", username.c_str(), ldap_err2string(r));
        result = -1;
    } else {
        result = 0;
        LOG_DIAG("Ldap authentication success for user '%s'", username.c_str());
    }
    memset(pw, 0, sizeof(pw));

    r = ldap_unbind_ext(ld, 0, 0);
    if (r != LDAP_SUCCESS) {
        LOG_ERROR("ldap_unbind error for user '%s': %s",  username.c_str(), ldap_err2string(r));
    }

    return 0; // success
}
