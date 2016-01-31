#ifndef _HttpContext_h
#define _HttpContext_h

#include <string>
#include "civetweb.h"

// functions not officially exposed by mongoose
extern "C" {
    int mg_vprintf(struct mg_connection *conn, const char *fmt, va_list ap);
}

#define PARAMS_SIZE 30

class MongooseServerContext; // forward declaration

/** class that handles the context of a request
  *
  * It can be later replaced by a class dedicated to fast cgi, for instance.
  */
class MongooseRequestContext {
public:
    MongooseRequestContext(struct mg_connection *conn);
    int printf(const char *fmt, ...) const;
    int write(const void *buf, size_t len) const;
    int read(void *buf, size_t len) const;

    inline const char *getUri() const { return mg_get_request_info(conn)->uri; }
    inline const char *getMethod() const { return mg_get_request_info(conn)->request_method; }
    inline const char *getHeader(const char *h) const { return mg_get_header(conn, h); }
    inline int isSSL() const { return mg_get_request_info(conn)->is_ssl; }
    const char *getQueryString() const;
    void sendObject(const std::string &basemane, const std::string &realpath) const;

private:
    mutable struct mg_connection *conn;
};

typedef MongooseRequestContext RequestContext;


/** class that handles the web server context
  *
  * It can be later replaced by a class dedicated to fast cgi, for instance.
  */
class MongooseServerContext {
public:
    MongooseServerContext();
    static MongooseServerContext &getInstance(); // create of get the singleton instance
    void init();
    int start();
    void stop();

    void addParam(const char *param);
    void setRequestHandler(int  (*handler)(const RequestContext*));

    static int logMessage(const struct mg_connection *, const char *message);
    static int handleRequest(struct mg_connection *);

    void setUrlRewritingRoot(const std::string &r) { urlRewritingRoot = r; }
    std::string getUrlRewritingRoot() { return urlRewritingRoot; }
    inline void setListeningPort(const std::string &p) { listeningPort = p; }
    inline std::string getListeningPort() { return listeningPort; }

private:
    struct mg_context *mongooseCtx; // mongoose inetrnal context, returned by mg_start
    struct mg_callbacks callbacks;
    const char *params[PARAMS_SIZE];
    static int (*requestHandler)(const RequestContext*);
    std::string urlRewritingRoot;
    std::string listeningPort;
};


#endif
