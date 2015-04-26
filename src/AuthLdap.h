#ifndef _AuthLdap_h
#define _AuthLdap_h

#include <string>

#include "Auth.h"

#define AUTH_LDAP "ldap"


struct AuthLdap : public Auth {
    std::string dname; // Distinguished name. Eg: uid=john,ou=people,dc=example,dc=com
    std::string uri; // eg: ldaps://example.com:389
    virtual int authenticate(char *password);
    virtual std::string serialize();
};

#endif
