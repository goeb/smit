#ifndef _json_h
#define _json_h

#include <list>
#include <string>

// TODO these do not produce JSON compatible syntax
// They produce only javascript compatible syntax (strings delimited by simple quotes)
std::string enquoteJs(const std::string &in);
std::string toJavascriptArray(const std::list<std::string> &items);

std::string toJsonString(const std::string &in);


#endif
