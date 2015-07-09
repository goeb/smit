#ifndef _json_h
#define _json_h

#include <list>
#include <string>

std::string enquoteJs(const std::string &in);
std::string toJavascriptArray(const std::list<std::string> &items);


#endif
