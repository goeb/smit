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
int  (*MongooseServerContext::requestHandler)(MongooseRequestContext*) = 0;


MongooseServerContext::MongooseServerContext()
{
    init();
}

void MongooseServerContext::init()
{
    int i;
    for (i=0; i<PARAMS_SIZE; i++) params[i] = 0;

    memset(&callbacks, 0, sizeof(callbacks));

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

void MongooseServerContext::setRequestHandler(int  (*handler)(MongooseRequestContext*))
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

int MongooseRequestContext::printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    return mg_vprintf(conn, fmt, ap);
}

int MongooseRequestContext::write(const void *buf, size_t len)
{
    return mg_write(conn, buf, len);
}

int MongooseRequestContext::read(void *buf, size_t len)
{
    return mg_read(conn, buf, len);
}

const char *MongooseRequestContext::getQueryString()
{
    const char *qs = "";
    const struct mg_request_info *req = mg_get_request_info(conn);
    if (req->query_string) qs = req->query_string;
    return qs;
}
