#ifndef _httpdHandlers_h
#define _httpdHandlers_h

#include <string>

#include "HttpContext.h"
#include "mutexTools.h"

extern std::string ExeFile;

int begin_request_handler(const RequestContext *req);

enum HttpEvent {
    H_GET,
    H_POST,
    H_OTHER, // other than GET or POST
    H_2XX, // any request with code 2xx
    H_400,
    H_403,
    H_413, // any other 4xx
    H_500,
    HTTP_EVENT_SIZE
};

struct HttpStatistics {
    time_t startupTime;
    int httpCodes[HTTP_EVENT_SIZE];
    Locker lock;
};

// global variable statictics
extern HttpStatistics HttpStats;

void initHttpStats();

#endif
