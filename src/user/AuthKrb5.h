#ifndef _AuthKrb5_h
#define _AuthKrb5_h

#include <string>
#include <list>

#include "Auth.h"

#define AUTH_KRB5 "krb5"

struct AuthKrb5 : public Auth {
    std::string realm; // Must generally be upper case
    std::string alternateUsername; // Optional. If empty, then username is used.
    virtual int authenticate(const char *password);
    virtual std::string serialize();
    virtual Auth *createCopy() const;
    static Auth *deserialize(std::list<std::string> &tokens);
    inline ~AuthKrb5() { }
    inline AuthKrb5(const std::string &u, const std::string &p, const std::string &r) :
        Auth(AUTH_KRB5, u), realm(r), alternateUsername(p) {}
    virtual std::string getParameter(const char *param);

};

#endif
