
#ifndef _logging_h
#define _logging_h

#include <stdio.h>

#define LOG_ERROR(...) { printf("ERROR %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); }
#define LOG_INFO(...)  { printf("INFO  %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); }
#define LOG_DEBUG(...) { printf("DEBUG %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); }

#endif
