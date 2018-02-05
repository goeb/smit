
#include <krb5.h>
#include <string.h>

#include "AuthKrb5.h"
#include "utils/logging.h"
#include "utils/parseConfig.h"

/** Authenticate against a Kerberos server
  *
  * @return
  *    0, success
  *   -2, error, password expired
  *   -1, other authentication error
  */
int AuthKrb5::authenticate(const char *password)
{
    std::string localUsername = username;
    if (!alternateUsername.empty()) localUsername = alternateUsername;
    LOG_DIAG("krbAuthenticate(%s@%s)", localUsername.c_str(), realm.c_str());
    krb5_context ctx;
    memset(&ctx, 0, sizeof(ctx));

    krb5_principal user;
    memset(&user, 0, sizeof(user));

    krb5_error_code code = 0;
    //code = krb5_init_context(&ctx);
    code = krb5_init_secure_context(&ctx);
    if (code) {
        LOG_ERROR("krb5_init_context: %d", code);
        return -1;
    }

    std::string principal = localUsername + "@" + realm;
    code = krb5_parse_name(ctx, principal.c_str(), &user);
    if (code) {
        LOG_ERROR("krb5_parse_name: %d", code);
        krb5_free_context(ctx);
        return -1;
    }

    krb5_creds credentials;
    krb5_get_init_creds_opt *options = 0;
    memset(&credentials, 0, sizeof(credentials));
    code = krb5_get_init_creds_opt_alloc(ctx, &options);
    if (code) {
        LOG_ERROR("krb5_get_init_creds_opt_alloc: %d", code);
        krb5_free_principal(ctx, user);
        krb5_free_context(ctx);
        return -1;
    }

    // no need for a long time because no ticket will be used
    krb5_get_init_creds_opt_set_tkt_life(options, 5*60);
    krb5_get_init_creds_opt_set_renew_life(options, 0);
    krb5_get_init_creds_opt_set_forwardable(options, 0);
    krb5_get_init_creds_opt_set_proxiable(options, 0);
    krb5_get_init_creds_opt_set_change_password_prompt(options, 0);

    code = krb5_get_init_creds_password(ctx, &credentials, user, password,
                                        0, 0, 0, 0, options);
    int result = -1;
    if (code) {
        if (code == KRB5KDC_ERR_KEY_EXP) {
            // Password has expired
            LOG_DIAG("krb5_get_init_creds_opt_alloc: password expired for user %s",
                     localUsername.c_str());
            result = -2;
        } else {
            LOG_ERROR("Kerberos authentication failed for user '%s': %d", localUsername.c_str(), code);
            result = -1;
        }
    } else {
        // success
        result = 0;
        LOG_DIAG("Kerberos authentication success for user '%s'", localUsername.c_str());
    }

    krb5_get_init_creds_opt_free(ctx, options);
    krb5_free_principal(ctx, user);
    krb5_free_context(ctx);
    return result;
}

std::string AuthKrb5::serialize()
{
    std::string result;
    result += serializeSimpleToken(AUTH_KRB5);
    result += " -realm " + serializeSimpleToken(realm);
    if (!alternateUsername.empty()) {
        result += " -primary " + serializeSimpleToken(alternateUsername);
    }
    return result;
}

Auth *AuthKrb5::createCopy() const
{
    AuthKrb5 *ah = new AuthKrb5(*this);
    return ah;
}
Auth *AuthKrb5::deserialize(std::list<std::string> &tokens)
{
    std::string realm;
    std::string alternateUsername;
    std::string token;
    while (!tokens.empty()) {
        token = popListToken(tokens);
        if (token == "-realm") realm = popListToken(tokens);
        else if (token == "-primary") alternateUsername = popListToken(tokens);
        else {
            LOG_ERROR("Invalid token '%s'", token.c_str());
            return 0;
        }
    }
    if (realm.empty()) {
        LOG_ERROR("AuthKrb5: empty realm");
        return 0;
    }

    return new AuthKrb5("", realm, alternateUsername);
}

std::string AuthKrb5::getParameter(const char *param)
{
    if (0 == strcmp(param, "primary")) {
        if (!alternateUsername.empty()) return alternateUsername;
        else return username;

    } else if (0 == strcmp(param, "realm")) {
        return realm;

    }

    return "";
}
