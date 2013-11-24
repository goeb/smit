
#include <string.h>

#include "logging.h"

bool doPrint(enum LogLevel msgLevel)
{
    const char *envLevel = getenv("SMIT_DEBUG");
    enum LogLevel policy = INFO; // default value
    if (envLevel) {
        if (0 == strcmp(envLevel, "FATAL")) policy = FATAL;
        else if (0 == strcmp(envLevel, "ERROR")) policy = ERROR;
        else if (0 == strcmp(envLevel, "INFO")) policy = INFO;
        else if (0 == strcmp(envLevel, "DEBUG")) policy = DEBUG;
        else policy = INFO;
    }
    return (policy >= msgLevel);
}

