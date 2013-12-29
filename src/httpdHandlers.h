#ifndef _httpdHandlers_h
#define _httpdHandlers_h

#include <string>

extern std::string exeFile;

int begin_request_handler(struct mg_connection *conn);


#endif
