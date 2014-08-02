
#ifndef _logging_h
#define _logging_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "mg_win32.h"
#include "dateTools.h"

enum LogLevel {
    LL_FATAL,
    LL_ERROR,
    LL_INFO,
    LL_DEBUG
};

extern LogLevel LoggingLevel;
inline void setLoggingLevel(LogLevel level) { LoggingLevel = level; }

bool doPrint(enum LogLevel msgLevel);

#define LOG_ERROR(...) do { if (doPrint(LL_ERROR)) { LOG("ERROR", __VA_ARGS__); } } while (0)
#define LOG_INFO(...)  do { if (doPrint(LL_INFO)) { LOG("INFO ", __VA_ARGS__); } } while (0)
#define LOG_DEBUG(...) do { if (doPrint(LL_DEBUG)) { LOG("DEBUG", __VA_ARGS__); } } while (0)

#define LOG(_level, ...) LOG2(_level, __FILE__, __LINE__, __VA_ARGS__)

#define LOG2(_level, _file, _line, ...) do { \
    long t = gettid(); \
    fprintf(stderr, "%s [%ld] %s %s:%d ", getLocalTimestamp().c_str(), t, _level, _file, _line); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    } while (0)


class FuncScope {
public:
    FuncScope(const char *file, int line, const char *name) {
        funcName = name;  if (doPrint(LL_DEBUG)) { LOG2("FUNC ", file, line, "Entering %s...", funcName); }
    }
    ~FuncScope() { if (doPrint(LL_DEBUG)) LOG("FUNC ", "Leaving %s...", funcName); }
private:
    const char *funcName;
};

#define LOG_FUNC() FuncScope __fs(__FILE__, __LINE__, __func__);


#endif
