#ifndef _httpdHandlers_h
#define _httpdHandlers_h

#include <string>

extern std::string ExeFile;

int begin_request_handler(struct mg_connection *conn);
int log_message_handler(const mg_connection *conn, const char *msg);


#endif
