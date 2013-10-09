
#ifndef _logging_h
#define _logging_h

#include <stdio.h>

#include "dateTools.h"

#define LOG_ERROR(...) LOG("ERROR", __VA_ARGS__)
#define LOG_INFO(...)  LOG("INFO", __VA_ARGS__)
#define LOG_DEBUG(...) LOG("DEBUG", __VA_ARGS__)

#define LOG(_level, ...) do { printf("%s %s %s:%d ", getLocalTimestamp().c_str(), _level, __FILE__, __LINE__); \
    printf(__VA_ARGS__); \
    printf("\n"); \
    } while (0)

#endif
