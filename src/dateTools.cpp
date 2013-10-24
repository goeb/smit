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

std::string epochToString(time_t t)
{
    struct tm *tmp;
    tmp = localtime(&t);
    char datetime[100+1]; // should be enough
    //strftime(datetime, sizeof(datetime)-1, "%Y-%m-%d %H:%M:%S", tmp);
    if (time(0) - t > 48*3600) {
        // date older than 2 days
        strftime(datetime, sizeof(datetime)-1, "%d %b %Y", tmp);
    } else {
        strftime(datetime, sizeof(datetime)-1, "%d %b %Y, %H:%M:%S", tmp);
    }
    return std::string(datetime);
}

/** Return the duration since that time
  * @return "x days", or "x months", or "x hours", etc.
  */
std::string epochToStringDelta(time_t t)
{
    char datetime[100+1]; // should be enough
    //strftime(datetime, sizeof(datetime)-1, "%Y-%m-%d %H:%M:%S", tmp);
    time_t delta = time(0) - t;
    if (delta < 60) snprintf(datetime, sizeof(datetime)-1, "%ld s", delta);
    else if (delta < 60*60) snprintf(datetime, sizeof(datetime)-1, "%ld min", delta/60);
    else if (delta < 60*60*24) snprintf(datetime, sizeof(datetime)-1, "%ld h", delta/60/60);
    else if (delta < 60*60*24*7) snprintf(datetime, sizeof(datetime)-1, "%ld d", delta/60/60/24);
    else if (delta < 60*60*24*30) snprintf(datetime, sizeof(datetime)-1, "%ld wk", delta/60/60/24/7);
    else if (delta < 60*60*24*365) snprintf(datetime, sizeof(datetime)-1, "%ld m", delta/60/60/24/30);
    else if (delta > 0) snprintf(datetime, sizeof(datetime)-1, "%ld yr", delta/60/60/24/30/365);
    else return epochToString(t);


    return std::string(datetime);
}
