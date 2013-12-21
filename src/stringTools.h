#ifndef _stringTools_h
#define _stringTools_h

#include <string>
#include <list>
#include <map>
#include <vector>


std::string popToken(std::string & uri, char separator);
void trimLeft(std::string & s, const char *c);
void trimRight(std::string &s, const char *c);
void trim(std::string &s, const char *c);
void trimBlanks(std::string &s);
std::string toString(const std::list<std::string> &values, const char *sep = 0);
std::string urlDecode(const std::string &src, int is_form_url_encoded=true, char mark='%');
std::string urlEncode(const std::string &src, char mark = '%', const char *dontEscape="._-$,;~()");
std::string pop(std::list<std::string> & L);
std::string getProperty(const std::map<std::string, std::list<std::string> > &properties, const std::string &name);
std::string replaceAll(const std::string &in, char c, const char *replaceBy);
std::string toJavascriptArray(const std::list<std::string> &items);
std::vector<std::string> split(const std::string &s, const char *c, int limit = -1);
std::list<std::string> splitLinesAndTrimBlanks(const std::string &s);

std::string join(const std::list<std::string> &items, const char *separator);


#endif
