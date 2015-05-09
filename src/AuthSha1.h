#ifndef _AuthSha1_h
#define _AuthSha1_h

#include <string>
#include <list>

#include "Auth.h"

#define AUTH_SHA1 "sha1"

struct AuthSha1 : Auth {
    std::string hash;
    std::string salt;
    virtual int authenticate(char *password);
    virtual std::string serialize();
    virtual Auth *createCopy();
    static Auth *load(std::list<std::string> &tokens);
};

#endif
