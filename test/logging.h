#define _logging_h

#include <errno.h>
#include <string.h>
#include <stdio.h>

// stubs for logging macros

#define STRERROR(_err) strerror(_err)

#define LOG(_level, ...) do { printf("%s: ", _level); printf(__VA_ARGS__); printf("\n"); } while (0)

#define LOG_ERROR(...) LOG("error", __VA_ARGS__)
#define LOG_INFO(...) LOG("info", __VA_ARGS__)
#define LOG_DIAG(...) LOG("diag", __VA_ARGS__)
#define LOG_DEBUG(...) LOG("debug", __VA_ARGS__)

