#ifndef _identifiers_h
#define _identifiers_h

#include <stdint.h>
#include "ustring.h"

std::string computeIdBase34(uint8_t *buffer, size_t length);
std::string convert2base34(const uint8_t *buffer, size_t length);
std::string getSha1(const char *data, size_t len);
std::string getSha1(const std::string &data);
std::string getSha1OfFile(const std::string &path);
std::string getBase64Id(const uint8_t *data, size_t len);


#endif
