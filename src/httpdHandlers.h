#ifndef _httpdHandlers_h
#define _httpdHandlers_h

#include <string>

int begin_request_handler(struct mg_connection *conn);
void upload_handler(struct mg_connection *conn, const char *path);


#endif
