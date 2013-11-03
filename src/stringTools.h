#ifndef _stringTools_h
#define _stringTools_h

#include <string>
#include <list>
#include <map>

std::string popToken(std::string & uri, char separator);
void trimLeft(std::string & s, const char *c);
void trimRight(std::string &s, const char *c);
void trim(std::string &s, const char *c);
void trimBlanks(std::string &s);
std::string toString(const std::list<std::string> &values, const char *sep = 0);
std::string urlDecode(const std::string &src, int is_form_url_encoded=true);
std::string urlEncode(const std::string &src);
std::string pop(std::list<std::string> & L);
std::string getProperty(const std::map<std::string, std::list<std::string> > &properties, const std::string &name);
std::string replaceAll(const std::string &in, char c, const char *replaceBy);

#endif
