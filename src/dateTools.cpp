#include <sys/time.h>
#include <time.h>
#include <stdio.h>

#include "dateTools.h"

std::string getLocalTimestamp()
{

    struct tm t;
    time_t now = time(0);
    localtime_r(&now, &t);

    const int SIZ = 30;
    char buffer[SIZ+1];
    size_t n = strftime(buffer, SIZ, "%Y-%m-%d %H:%M:%S", &t);

    if (n != 19) return "invalid-timestamp";

    // add milliseconds
    struct timeval tv;
    gettimeofday(&tv, 0);
    int milliseconds = tv.tv_usec/1000;

    snprintf(buffer+n, SIZ-n, ".%03d", milliseconds);

    return buffer;
}
