#ifndef _kerberos_h
#define _kerberos_h

#include <string>
int krbAuthenticate(const std::string &username, const std::string &realm, const std::string password);

#endif
