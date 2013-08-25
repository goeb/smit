
#ifndef _parseConfig_h
#define _parseConfig_h

#include <stdint.h>
#include <string>
#include <list>

std::list<std::list<std::string> > parseConfig(const char *buf, size_t len);

int loadFile(const char *filepath, char **data);

std::list<std::string> parseColspec(const char *spec);
std::list<std::pair<char, std::string> > parseFieldSpec(const char *fieldSpec);


#endif
