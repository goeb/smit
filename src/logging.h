
#ifndef _logging_h
#define _logging_h

#include <stdio.h>

#define LOG_ERROR(...) do { printf("ERROR %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } while (0)
#define LOG_INFO(...)  do { printf("INFO  %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } while (0)
#define LOG_DEBUG(...) do { printf("DEBUG %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } while (0)

#endif
