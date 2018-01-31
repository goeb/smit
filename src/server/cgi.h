#ifndef _cgi_h
#define _cgi_h

#include <string>

#include "HttpContext.h"
#include "utils/argv.h"

void launchCgi(const RequestContext *req, const std::string &exePath, Argv envp);

#endif
