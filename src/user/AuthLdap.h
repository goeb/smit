#ifndef _AuthLdap_h
#define _AuthLdap_h

#include <string>
#include <list>

#include "Auth.h"

#define AUTH_LDAP "ldap"


struct AuthLdap : public Auth {
    std::string uri; // eg: ldaps://example.com:389
    std::string dname; // Distinguished name. Eg: uid=john,ou=people,dc=example,dc=com
    virtual int authenticate(const char *password);
    virtual std::string serialize();
    static Auth *deserialize(std::list<std::string> &tokens);
    virtual Auth *createCopy() const;
    inline ~AuthLdap() { }
    inline AuthLdap(const std::string &username, const std::string &ur, const std::string &dn) :
        Auth(AUTH_LDAP, username), uri(ur), dname(dn) { }
};

#endif
