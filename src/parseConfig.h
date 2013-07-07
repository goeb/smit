
#ifndef _parseConfig_h
#define _parseConfig_h

#include <string>
#include <list>
#include "ustring.h"

std::list<std::list<ustring> > parseConfig(const unsigned char *buf, size_t len);


#endif
