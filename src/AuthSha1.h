#ifndef _AuthSha1_h
#define _AuthSha1_h

#include <string>

#include "Auth.h"

#define AUTH_SHA1 "sha1"

struct AuthSha1 : public Auth {
    std::string hash;
    std::string salt;
    virtual int authenticate(char *password);
    virtual std::string serialize();
};

#endif
