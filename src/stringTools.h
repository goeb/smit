#ifndef _stringTools_h
#define _stringTools_h

#include <string>
#include <list>

std::string popToken(std::string & uri, char separator);
void trimLeft(std::string & s, char c);
void trimRight(std::string &s, char c);
void trim(std::string &s, char c);
std::string toString(const std::list<std::string> &values);
std::string urlDecode(const std::string &src, int is_form_url_encoded=true);

#endif
