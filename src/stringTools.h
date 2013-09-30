#ifndef _stringTools_h
#define _stringTools_h

#include <string>

std::string popToken(std::string & uri, char separator);
void trimLeft(std::string & s, char c);
void trimRight(std::string &s, char c);
void trim(std::string &s, char c);

#endif
