#ifndef _stringTools_h
#define _stringTools_h

#include <string>
#include <list>

std::string popToken(std::string & uri, char separator);
void trimLeft(std::string & s, const char *c);
void trimRight(std::string &s, const char *c);
void trim(std::string &s, const char *c);
void trimBlanks(std::string &s);
std::string toString(const std::list<std::string> &values);
std::string urlDecode(const std::string &src, int is_form_url_encoded=true);
std::string urlEncode(const std::string &src);
std::string pop(std::list<std::string> & L);



#endif
