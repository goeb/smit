#ifndef _AuthSha1_h
#define _AuthSha1_h

#include <string>
#include <list>

#include "Auth.h"

#define AUTH_SHA1 "sha1"

struct AuthSha1 : Auth {
    std::string hash;
    std::string salt;
    virtual int authenticate(const char *password);
    virtual std::string serialize();
    virtual Auth *createCopy() const;
    static Auth *deserialize(std::list<std::string> &tokens);
    inline ~AuthSha1() { }
    inline AuthSha1(const std::string &u, const std::string &h, const std::string &s) :
        Auth(AUTH_SHA1, u), hash(h), salt(s) { }
};

#endif
