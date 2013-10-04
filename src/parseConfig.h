
#ifndef _parseConfig_h
#define _parseConfig_h

#include <stdint.h>
#include <string>
#include <list>

std::list<std::list<std::string> > parseConfig(const char *buf, size_t len);

int loadFile(const char *filepath, char **data);
int writeToFile(const char *filepath, const std::string &data, bool allowOverwrite);

std::list<std::string> parseColspec(const char *spec);
std::list<std::pair<char, std::string> > parseFieldSpec(const char *fieldSpec);
std::string serializeProperty(const std::string &key, const std::list<std::string> &values);
std::string doubleQuote(const std::string &input);
std::string popListToken(std::list<std::string> &tokens);



#endif
