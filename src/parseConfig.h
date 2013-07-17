
#ifndef _parseConfig_h
#define _parseConfig_h

#include <stdint.h>
#include <string>
#include <list>
#include "ustring.h"

std::list<std::list<ustring> > parseConfig(const uint8_t *buf, size_t len);

int loadFile(const char *filepath, unsigned char **data);

#endif
