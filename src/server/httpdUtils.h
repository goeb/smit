#ifndef _httpdUtils_h
#define _httpdUtils_h

#include <string>
#include <list>
#include <map>

#include "HttpContext.h"
#include "utils/mutexTools.h"

#define MAX_SIZE_UPLOAD (10*1024*1024)
#define COOKIE_ORIGIN_VIEW "view-"


enum { REQUEST_NOT_PROCESSED = 0, // let the httpd engine handle the request
       REQUEST_COMPLETED = 1      // do not let the httpd engine handle the request
     };

enum RenderingFormat { RENDERING_HTML, RENDERING_TEXT, RENDERING_CSV, RENDERING_JSON, X_SMIT };

enum HttpEvent {
    H_GET,
    H_POST,
    H_OTHER, // other than GET or POST
    H_2XX, // any request with code 2xx
    H_400,
    H_403,
    H_413, // any other 4xx
    H_500,
    HTTP_EVENT_SIZE
};

struct HttpStatistics {
    time_t startupTime;
    int httpCodes[HTTP_EVENT_SIZE];
    Locker lock;
};

enum ContentType {
    CT_WWW_FORM_URLENCODED, // application/x-www-form-urlencoded
    CT_MULTIPART_FORM_DATA, // multipart/form-data
    CT_OCTET_STREAM, //application/octet-stream
    CT_UNKNOWN
};

// global variable statictics
extern HttpStatistics HttpStats;

void initHttpStats();
void addHttpStat(HttpEvent e);

int readMgreq(const RequestContext *request, std::string &data, size_t maxSize);
const char *getContentTypeString(const RequestContext *req);
ContentType getContentType(const RequestContext *req);
ContentType getContentType(const RequestContext *req, std::string &boundary);
void sendHttpHeader200(const RequestContext *request);
void sendHttpHeader201(const RequestContext *request);
void sendHttpHeader204(const RequestContext *request, const char *extraHeader);
int sendHttpHeader304(const RequestContext *request);
int sendHttpHeader400(const RequestContext *request, const char *msg);
void sendHttpHeader403(const RequestContext *req);
void sendHttpHeader413(const RequestContext *request, const char *msg);
void sendHttpHeader404(const RequestContext *request);
void sendHttpHeader409(const RequestContext *request);
void sendHttpHeader500(const RequestContext *request, const char *msg);
int sendHttpRedirect(const RequestContext *request, const std::string &redirectUrl, const char *otherHeader);
std::string mangleCookieName(const std::string &prefix);
std::string getServerCookie(const std::string &prefix, const std::string &value, int maxAgeSeconds);
std::string getDeletedCookieString(const std::string &prefix);
void sendCookie(const RequestContext *request, const std::string &name, const std::string &value, int duration);
int sendHttpHeaderInvalidResource(const RequestContext *request);
RenderingFormat getFormat(const RequestContext *request);
int getFromCookie(const RequestContext *request, const std::string &prefix, std::string &value);
std::list<std::list<std::string> > convertPostToTokens(std::string &postData);
std::string removeParam(std::string qs, const char *paramName);
void parseQueryStringVar(const std::string &var, std::string &key, std::string &value);

size_t multipartGetNextPart(const char * const buffer, size_t bufferSize, const char *sboundary,
                           const char **data, size_t *dataSize,
                           std::string &name, std::string &filename);

#endif
