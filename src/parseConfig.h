
#ifndef _parseConfig_h
#define _parseConfig_h

#include <stdint.h>
#include <string>
#include <list>

std::list<std::list<std::string> > parseConfigTokens(const char *buf, size_t len);
std::list<std::string> parseColspec(const char *spec, const std::list<std::string> &knownProperties);
std::string serializeSimpleToken(const std::string token);
std::string serializeProperty(const std::string &propertyName, const std::string &value);
std::string serializeProperty(const std::string &key, const std::list<std::string> &values);
std::string doubleQuote(const std::string &input);
std::string popListToken(std::list<std::string> &tokens);
std::string serializeTokens(const std::list<std::string> &tokens);
std::string serializeTokens(const std::list<std::list<std::string> > &linesOfTokens);



#endif
