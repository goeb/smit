#ifndef _jTools_h
#define _jTools_h

#include <list>
#include <string>

// Javascript conversion functions
std::string enquoteJs(const std::string &in);
std::string toJavascriptArray(const std::list<std::string> &items);

// JSON conversion functions
std::string toJsonString(const std::string &in);


#endif
