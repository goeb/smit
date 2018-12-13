#ifndef _RequestContext_h
#define _RequestContext_h

#include <string>

class RequestContext {
public:
    virtual int printf(const char *fmt, ...) const = 0;
    virtual int write(const void *buf, size_t len) const = 0;
    virtual const char *getQueryString() const = 0;
    virtual std::string getUrlRewritingRoot() const = 0;
    virtual int read(void *buf, size_t len) const = 0;
    virtual void sendObject(const std::string &basemane, const std::string &realpath) const = 0;
    virtual const char *getMethod() const = 0;
    virtual const char *getUri() const = 0;
    virtual const char *getHeader(const char *h) const = 0;
    virtual bool getHeader(int i, std::string &key, std::string &value) const = 0;
    virtual int isSSL() const = 0;
    virtual std::string getListeningPort() const = 0;
    virtual void sendHttpHeader(int code, const char *fmt, ...) const = 0;
};

#endif
