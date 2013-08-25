#ifndef _identifiers_h
#define _identifiers_h

#include <stdint.h>
#include "ustring.h"

ustring computeIdBase34(uint8_t *buffer, size_t length);
std::string convert2base34(const uint8_t *buffer, size_t length, bool skip_io);


#endif
