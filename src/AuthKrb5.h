#ifndef _AuthKrb5_h
#define _AuthKrb5_h

#include <string>

#include "Auth.h"

#define AUTH_KRB5 "krb5"

struct AuthKrb5 : public Auth {
    std::string realm; // must generally be upper case
    virtual int authenticate(char *password);
    virtual std::string serialize();
    virtual Auth *createCopy();
};

#endif
