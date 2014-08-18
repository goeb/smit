#ifndef _httpdHandlers_h
#define _httpdHandlers_h

#include <string>

#include "HttpContext.h"

extern std::string ExeFile;

int begin_request_handler(const RequestContext *req);


#endif
