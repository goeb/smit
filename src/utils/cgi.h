#ifndef _cgi_h
#define _cgi_h

#include <string>

#include "RequestContext.h"
#include "argv.h"

void launchCgi(const RequestContext *req, const std::string &exePath, Argv envp);

#endif
