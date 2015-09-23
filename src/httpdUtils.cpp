/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License v2 as published by
 *   the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <openssl/sha.h>

#include <string>
#include <sstream>
#include <fstream>

#include "logging.h"
#include "stringTools.h"
#include "global.h"
#include "mg_win32.h"
#include "filesystem.h"
#include "httpdUtils.h"



// global variable statictics
HttpStatistics HttpStats;

void initHttpStats()
{
    memset(&HttpStats, 0, sizeof(HttpStatistics));
    HttpStats.startupTime = time(0);
}

void addHttpStat(HttpEvent e)
{
    if (e >= HTTP_EVENT_SIZE || e < 0) return;
    ScopeLocker scopeLocker(HttpStats.lock, LOCK_READ_WRITE);
    HttpStats.httpCodes[e]++;
}

/** Read bytes from the socket, and store in data
  *
  * @return
  *    0 on success
  *   -1 on error (data too big)
  */
int readMgreq(const RequestContext *request, std::string &data, size_t maxSize)
{
    data.clear();
    const int SIZ = 4096;
    char postFragment[SIZ+1];
    int n; // number of bytes read

    while ( (n = request->read(postFragment, SIZ)) > 0) {
        LOG_DEBUG("postFragment size=%d", n);
        data.append(std::string(postFragment, n));
        if (data.size() > maxSize) {
            // data too big. overflow. abort.
            LOG_ERROR("Too much POST data. Abort. maxSize=%lu", L(maxSize));
            return -1;
        }

    }
    LOG_DEBUG("data size=%lu", L(data.size()));

    return 0; // ok
}

/** Get the content type
  *
  * @return
  *     If no content type, then return an empty string.
  */
const char *getContentType(const RequestContext *req)
{
    const char *ct = req->getHeader("Content-Type");
    if (ct) return ct;
    else return "";
}

void sendHttpHeader200(const RequestContext *request)
{
    LOG_FUNC();
    request->printf("HTTP/1.0 200 OK\r\n");
    addHttpStat(H_2XX);
}

void sendHttpHeader201(const RequestContext *request)
{
    LOG_FUNC();
    request->printf("HTTP/1.0 201 Created\r\n\r\n");
    addHttpStat(H_2XX);
}

/**
  *
  * @param extraHeader
  *     Typically a cookie.
  */
void sendHttpHeader204(const RequestContext *request, const char *extraHeader)
{
    LOG_FUNC();
    request->printf("HTTP/1.0 204 No Content\r\n");
    if (extraHeader) request->printf("%s\r\n", extraHeader);
    request->printf("Content-Length: 0\r\n");
    request->printf("Connection: close\r\n");
    request->printf("\r\n");
    addHttpStat(H_2XX);
}

int sendHttpHeader304(const RequestContext *request)
{
    request->printf("HTTP/1.0 304 No Modified\r\n");
    request->printf("Content-Length: 0\r\n");
    request->printf("Connection: close\r\n\r\n");

    return REQUEST_COMPLETED;
}

int sendHttpHeader400(const RequestContext *request, const char *msg)
{
    LOG_DIAG("HTTP 400 Bad Request: %s", msg);
    request->printf("HTTP/1.0 400 Bad Request\r\n\r\n");
    request->printf("400 Bad Request\r\n");
    request->printf("%s\r\n", msg);
    addHttpStat(H_400);

    return REQUEST_COMPLETED; // request completely handled
}
void sendHttpHeader403(const RequestContext *req)
{
    LOG_INFO("HTTP 403 Forbidden: %s %s?%s", req->getMethod(), req->getUri(), req->getQueryString());
    req->printf("HTTP/1.1 403 Forbidden\r\n\r\n");
    req->printf("403 Forbidden\r\n");
    addHttpStat(H_403);
}

void sendHttpHeader413(const RequestContext *request, const char *msg)
{
    LOG_DIAG("HTTP 413 Request Entity Too Large");
    request->printf("HTTP/1.1 413 Request Entity Too Large\r\n\r\n");
    request->printf("413 Request Entity Too Large\r\n%s\r\n", msg);
    addHttpStat(H_413);
}

void sendHttpHeader404(const RequestContext *request)
{
    LOG_DIAG("HTTP 404 Not Found");
    request->printf("HTTP/1.1 404 Not Found\r\n\r\n");
    request->printf("404 Not Found\r\n");
}
void sendHttpHeader409(const RequestContext *request)
{
    LOG_DIAG("HTTP 409 Conflict");
    request->printf("HTTP/1.1 409 Conflict\r\n\r\n");
    request->printf("409 Conflict\r\n");
}

void sendHttpHeader500(const RequestContext *request, const char *msg)
{
    LOG_ERROR("HTTP 500 Internal Server Error");
    request->printf("HTTP/1.1 500 Internal Server Error\r\n\r\n");
    request->printf("500 Internal Server Error\r\n");
    request->printf("%s\r\n", msg);
    addHttpStat(H_500);
}

/**
  * @redirectUrl
  *    Must be an absolute path (starting with /)
  * @param otherHeader
  *    Must not include the line-terminating \r\n
  *    May be NULL
  */
int sendHttpRedirect(const RequestContext *request, const std::string &redirectUrl, const char *otherHeader)
{
    request->printf("HTTP/1.1 303 See Other\r\n");
    const char *scheme = 0;
    if (request->isSSL()) scheme = "https";
    else scheme = "http";

    const char *host = request->getHeader("Host"); // includes the TCP port (optionally)
    if (!host) {
        LOG_ERROR("No Host header in base request");
        host ="";
    }

    if (redirectUrl[0] != '/') LOG_ERROR("Invalid redirect URL (missing first /): %s", redirectUrl.c_str());

    std::string location = scheme;
    location += "://";
    location += host;
    location += MongooseServerContext::getInstance().getUrlRewritingRoot();
    location += redirectUrl;
    request->printf("Location: %s", location.c_str());

    if (otherHeader) request->printf("\r\n%s", otherHeader);
    request->printf("\r\n\r\n");

    LOG_DIAG("Redirected to: %s", location.c_str());

    return REQUEST_COMPLETED;
}

std::string mangleCookieName(const std::string &prefix)
{
    std::string result = prefix;
    result += MongooseServerContext::getInstance().getListeningPort();
    return result;
}

std::string getServerCookie(const std::string &prefix, const std::string &value, int maxAgeSeconds)
{
    std::ostringstream s;
    s << "Set-Cookie: ";
    s << mangleCookieName(prefix);
    s << "=" << value;
    s << "; Path=" << MongooseServerContext::getInstance().getUrlRewritingRoot() << "/";
    if (maxAgeSeconds > 0) s << ";  Max-Age=" << maxAgeSeconds;
    return s.str();
}

std::string getDeletedCookieString(const std::string &prefix)
{
    std::string cookieString;
    cookieString = "Set-Cookie: ";
    cookieString += mangleCookieName(prefix);
    cookieString += "=deleted";
    cookieString += ";Path=" + MongooseServerContext::getInstance().getUrlRewritingRoot() + "/" ;
    cookieString += ";expires=Thu, 01 Jan 1970 00:00:00 GMT";
    return cookieString;
}

void sendCookie(const RequestContext *request, const std::string &name, const std::string &value, int duration)
{
    std::string s = getServerCookie(name, value, duration);

    request->printf("%s\r\n", s.c_str());
}

int sendHttpHeaderInvalidResource(const RequestContext *request)
{
    const char *uri = request->getUri();
    LOG_INFO("Invalid resource: uri=%s", uri);
    request->printf("HTTP/1.0 400 Bad Request\r\n\r\n");
    request->printf("400 Bad Request");

    return 1; // request processed
}

enum RenderingFormat getFormat(const RequestContext *request)
{
    std::string q = request->getQueryString();
    std::string format = getFirstParamFromQueryString(q, "format");

    if (format == "html") return RENDERING_HTML;
    else if (format == "csv") return RENDERING_CSV;
    else if (format == "text") return RENDERING_TEXT;
    else if (format == "json") return RENDERING_JSON;
    else {
        // look at the Accept header
        const char *contentType = request->getHeader("Accept");

        if (!contentType) return RENDERING_HTML; // no such header, return default value
        if (0 == strcasecmp(contentType, "text/html")) return RENDERING_HTML;
        if (0 == strcasecmp(contentType, "text/plain")) return RENDERING_TEXT;
        if (0 == strcasecmp(contentType, APP_X_SMIT)) return X_SMIT;
    }
    return RENDERING_HTML;
}

/** Get a cookie after its name.
  *
  * @return
  *     0 if cookie found
  *    -1 if no such cookie found
  */
int getFromCookie(const RequestContext *request, const std::string &prefix, std::string &value)
{
    const char *cookies = request->getHeader("Cookie");
    // There is a most 1 cookie as stated in rfc6265 "HTTP State Management Mechanism":
    //     When the user agent generates an HTTP request, the user agent MUST
    //     NOT attach more than one Cookie header field.
    if (cookies) {
        std::string wantedCookie = mangleCookieName(prefix);

        LOG_DEBUG("Cookies found: %s", cookies);
        std::string c = cookies;
        while (c.size() > 0) {
            std::string cookie = popToken(c, ';');
            std::string name = popToken(cookie, '=');
            trim(name, " "); // remove spaces around
            LOG_DIAG("Cookie: name=%s, value=%s", name.c_str(), value.c_str());
            if (name == wantedCookie) {
                value = cookie; // remaining part after the =
                return 0;
            }
        }
    }
    LOG_DEBUG("no Cookie found");
    return -1;
}

/** Parse posted arguments into a list of tokens.
  * Expected input is:
  * token=arg1&token=arg2&...
  * (token is the actual word "token")
  */
std::list<std::list<std::string> > convertPostToTokens(std::string &postData)
{
    LOG_FUNC();
    LOG_DEBUG("convertPostToTokens: %s", postData.c_str());
    std::list<std::list<std::string> > tokens;
    std::list<std::string> line;

    while (postData.size() > 0) {
        std::string tokenPair = popToken(postData, '&');
        std::string token = popToken(tokenPair, '=');
        std::string &value = tokenPair;
        if (token != "token") continue; // ignore this

        if (value == "EOL") {
            if (! line.empty() && ! line.front().empty()) {
                tokens.push_back(line);
                LOG_DEBUG("convertPostToTokens: line=%s", toString(line).c_str());
            }
            line.clear();
        } else {
            // split by \r\n if needed, and trim blanks
            value = urlDecode(value);
            while (strchr(value.c_str(), '\n')) {
                std::string subValue = popToken(value, '\n');
                trimBlanks(subValue);
                line.push_back(subValue);
            }
            trimBlanks(value);
            line.push_back(value); // append the last subvalue
        }
    }
    if (! line.empty() && ! line.front().empty()) {
        tokens.push_back(line);
        LOG_DEBUG("convertPostToTokens: line=%s", toString(line).c_str());
    }
    return tokens;
}



/** remove a parameter from a query string
  */
std::string removeParam(std::string qs, const char *paramName)
{
    std::string newQueryString;
    while (qs.size() > 0) {
        std::string part = popToken(qs, '&');
        if (0 != strncmp(paramName, part.c_str(), strlen(paramName)) ) {
            if (!newQueryString.empty()) newQueryString += "&";
            newQueryString += part;
        }
    }
    return newQueryString;
}



/**
  *
  * @param[out] vars
  *     Associative array where the posted parameters get stored
  *
  * @param pathTmp
  *     Path to a temporary directory where to store the uploaded files
  *     If empty, the uploaded files should be ignored.
  */
int parseFormRequest(const RequestContext *req, std::map<std::string, std::list<std::string> > &vars,
                     const std::string &pathTmp)
{
    const char *multipart = "multipart/form-data";
    const char *contentType = getContentType(req);
    if (0 == strncmp(multipart, contentType, strlen(multipart))) {

        // extract the boundary
        const char *b = "boundary=";
        const char *p = mg_strcasestr(contentType, b);
        if (!p) {
            LOG_ERROR("Missing boundary in multipart form data");
            return -1;
        }
        p += strlen(b);
        std::string boundary = p;
        LOG_DEBUG("Boundary: %s", boundary.c_str());

        std::string postData;
        int rc = readMgreq(req, postData, MAX_SIZE_UPLOAD);
        if (rc < 0) {
            sendHttpHeader413(req, "You tried to upload too much data. Max is 10 MB.");
            return -1;
        }

        // parseMultipartAndStoreUploadedFiles(postData, boundary, vars, pro);
        // TODO parse the posted parameter

    } else {
        // other Content-Type
        LOG_ERROR("Content-Type '%s' not supported", contentType);
        sendHttpHeader400(req, "Bad Content-Type");
        return -1;
    }
    return 0;
}

void parseQueryStringVar(const std::string &var, std::string &key, std::string &value) {
    size_t x = var.find('=');
    if (x != std::string::npos) {
        key = var.substr(0, x);
        value = "";
        if (x+1 < var.size()) {
            value = var.substr(x+1);
        }
    }

    key = urlDecode(key);
    value = urlDecode(value);
}



