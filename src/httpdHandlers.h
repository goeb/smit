#ifndef _httpdHandlers_h
#define _httpdHandlers_h

#include <string>

#include "HttpContext.h"
#include "mutexTools.h"

#define K_PUSH_FILE "pushfile"
extern std::string ExeFile;

int begin_request_handler(const RequestContext *req);

#endif
