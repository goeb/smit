
#ifndef _logging_h
#define _logging_h

#include <stdio.h>
#include <stdlib.h>

#include "dateTools.h"

enum LogLevel {
    FATAL,
    ERROR,
    INFO,
    DEBUG
};

bool doPrint(enum LogLevel msgLevel);


#define LOG_ERROR(...) { if (doPrint(ERROR)) { LOG("ERROR", __VA_ARGS__); } }
#define LOG_INFO(...)  { if (doPrint(INFO)) { LOG("INFO", __VA_ARGS__); } }
#define LOG_DEBUG(...) { if (doPrint(DEBUG)) { LOG("DEBUG", __VA_ARGS__); } }

#define LOG(_level, ...) LOG2(_level, __FILE__, __LINE__, __VA_ARGS__)

#define LOG2(_level, _file, _line, ...) do { \
    fprintf(stderr, "%s %s %s:%d ", getLocalTimestamp().c_str(), _level, _file, _line); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    } while (0)


class FuncScope {
public:
    FuncScope(const char *file, int line, const char *name) {
        funcName = name;  if (doPrint(DEBUG)) { LOG2("FUNC", file, line, "Entering %s...", funcName); }
    }
    ~FuncScope() { if (doPrint(DEBUG)) LOG("FUNC", "Leaving %s...", funcName); }
private:
    const char *funcName;
};

#define LOG_FUNC() FuncScope __fs(__FILE__, __LINE__, __func__);


#endif
