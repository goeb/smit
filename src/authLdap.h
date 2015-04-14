#ifndef _ldap_h
#define _ldap_h

#include <string>
int ldapAuthenticate(const std::string &username, const std::string &server, const std::string password);

#endif
