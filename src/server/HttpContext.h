#ifndef _HttpContext_h
#define _HttpContext_h

#include <string>
#include "mongoose.h"

// functions not officially exposed by mongoose
extern "C" {
    int mg_vprintf(struct mg_connection *conn, const char *fmt, va_list ap);
}

#define PARAMS_SIZE 30

class MongooseRequestContext; // forward declaration
typedef MongooseRequestContext RequestContext;

/** class that handles the web server context
  *
  * It can be later replaced by a class dedicated to fast cgi, for instance.
  */
class MongooseServerContext {
public:
    MongooseServerContext();
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

class ResponseContext {
public:
    virtual int printf(const char *fmt, ...) const = 0;
    virtual int write(const void *buf, size_t len) const = 0;
    inline virtual const char *getQueryString() const { return "";}
    inline virtual std::string getUrlRewritingRoot() const { return ""; }
};

/** class that handles the context of a request
  *
  * It can be later replaced by a class dedicated to fast cgi, for instance.
  */
class MongooseRequestContext : public ResponseContext {
public:
    MongooseRequestContext(struct mg_connection *conn);

    virtual int printf(const char *fmt, ...) const;
    virtual int write(const void *buf, size_t len) const;

    const char *getQueryString() const;
    inline std::string getUrlRewritingRoot() const { return serverContext->getUrlRewritingRoot(); }

    // Methods for the base request handler (ie: not the rendering parts)
    int read(void *buf, size_t len) const;
    void sendObject(const std::string &basemane, const std::string &realpath) const;
    inline const char *getMethod() const { return mg_get_request_info(conn)->request_method; }
    inline const char *getUri() const { return mg_get_request_info(conn)->uri; }
    inline const char *getHeader(const char *h) const { return mg_get_header(conn, h); }
    inline int isSSL() const { return mg_get_request_info(conn)->is_ssl; }
    inline std::string getListeningPort() const { return serverContext->getListeningPort(); }
    inline void setServerContext(MongooseServerContext *sc) { serverContext = sc; }
private:
    mutable struct mg_connection *conn;
    MongooseServerContext *serverContext;
};

#endif
