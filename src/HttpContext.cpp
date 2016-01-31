/*   Small Issue Tracker
 *   Copyright (C) 2014 Frederic Hoerni
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

#include <stdarg.h>
#include <string.h>

#include "HttpContext.h"
#include "logging.h"

// static members
int  (*MongooseServerContext::requestHandler)(const RequestContext*) = 0;


MongooseServerContext::MongooseServerContext()
{
    init();
}

MongooseServerContext &MongooseServerContext::getInstance()
{
    static MongooseServerContext *instance = 0;
    if (!instance) instance = new MongooseServerContext();
    return *instance;
}

void MongooseServerContext::init()
{
    int i;
    for (i=0; i<PARAMS_SIZE; i++) params[i] = 0;

    memset(&callbacks, 0, sizeof(callbacks));

    mongooseCtx = 0;

    callbacks.log_message = logMessage;
}

int MongooseServerContext::start()
{
    mongooseCtx = mg_start(&callbacks, NULL, params);
    if (!mongooseCtx) return -1;
    return 0;
}

void MongooseServerContext::stop()
{
    return mg_stop(mongooseCtx);
}

void MongooseServerContext::addParam(const char *param)
{
    size_t i = 0;
    for (i=0; i<PARAMS_SIZE; i++) {
        if (params[i] == 0) {
            params[i] = param;
            return;
        }
    }
    LOG_ERROR("Cannot add parameter %s: table full", param);
}

void MongooseServerContext::setRequestHandler(int (*handler)(const RequestContext*))
{
    callbacks.begin_request = handleRequest;
    requestHandler = handler;
}

int MongooseServerContext::handleRequest(struct mg_connection *conn)
{
    MongooseRequestContext mrc(conn);
    return requestHandler(&mrc);
}

int MongooseServerContext::logMessage(const struct mg_connection *, const char *message)
{
    LOG_ERROR("[MG] %s", message);
    return 1;
}


MongooseRequestContext::MongooseRequestContext(struct mg_connection *c)
{
    conn = c;
}

/** Print text to the HTTP client
  */
int MongooseRequestContext::printf(const char *fmt, ...) const
{
    va_list ap;
    va_start(ap, fmt);
    int r = mg_vprintf(conn, fmt, ap);
    va_end(ap);
    return r;
}

/** Write data to the HTTP client
  */
int MongooseRequestContext::write(const void *buf, size_t len) const
{
    return mg_write(conn, buf, len);
}

/** Read data from the HTTP client
  */
int MongooseRequestContext::read(void *buf, size_t len) const
{
    return mg_read(conn, buf, len);
}

const char *MongooseRequestContext::getQueryString() const
{
    const char *qs = "";
    const struct mg_request_info *req = mg_get_request_info(conn);
    if (req->query_string) qs = req->query_string;
    return qs;
}

void MongooseRequestContext::sendObject(const std::string &basemane, const std::string &realpath) const
{
    mg_send_file(conn, realpath.c_str(), basemane.c_str());
}

