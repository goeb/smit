// Copyright (c) 2004-2012 Sergey Lyubka
// This file is a part of mongoose project, http://github.com/valenok/mongoose

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define strtoll strtol
typedef __int64 int64_t;
#else
#include <inttypes.h>
#include <unistd.h>
#endif // !_WIN32

#include <string>
#include <sstream>
#include "mongoose.h"
#include "httpdHandlers.h"

#include "db.h"
#include "logging.h"
#include "identifiers.h"

std::string request2string(struct mg_connection *conn)
{
    const struct mg_request_info *request_info = mg_get_request_info(conn);

    const int SIZEX = 1000;
    char content[SIZEX+1];
    int content_length = snprintf(content, sizeof(content),
                                  "<pre>Hello from mongoose! Remote port: %d",
                                  request_info->remote_port);

    int L = content_length;
    L += snprintf(content+L, SIZEX-L, "request_method=%s\n", request_info->request_method);
    L += snprintf(content+L, SIZEX-L, "uri=%s\n", request_info->uri);
    L += snprintf(content+L, SIZEX-L, "http_version=%s\n", request_info->http_version);
    L += snprintf(content+L, SIZEX-L, "query_string=%s\n", request_info->query_string);
    L += snprintf(content+L, SIZEX-L, "remote_user=%s\n", request_info->remote_user);

    for (int i=0; i<request_info->num_headers; i++) {
        struct mg_request_info::mg_header H = request_info->http_headers[i];
        L += snprintf(content+L, SIZEX-L, "header: %30s = %s\n", H.name, H.value);
    }
    L += snprintf(content+L, SIZEX-L, "</pre>");

    return std::string(content);

}

void sendHttpHeader200(struct mg_connection *conn)
{
    mg_printf(conn, "HTTP/1.0 200 OK\r\n");
}
int handleResourceAdmin(struct mg_connection *conn)
{
    sendHttpHeader200(conn);
    LOG_ERROR("Resource 'admin' not implemented");
    return 1;
}
int handleResourceSignin(struct mg_connection *conn)
{
    sendHttpHeader200(conn);
    LOG_ERROR("Resource 'signin' not implemented");
    return 1;
}
int handleResourceUsers(struct mg_connection *conn)
{
    sendHttpHeader200(conn);
    LOG_ERROR("Resource 'users' not implemented");
    return 1;
}

int handleResourceRoot(struct mg_connection *conn)
{
    sendHttpHeader200(conn);
    LOG_ERROR("Resource '/' not implemented");
    return 1;
}

int handleInvalidResource(struct mg_connection *conn)
{
    const char *uri = mg_get_request_info(conn)->uri;
    LOG_INFO("Invalid resource: uri=%s", uri);
    mg_printf(conn, "HTTP/1.0 400 Bad Request\r\n\r\n");
    mg_printf(conn, "400 Bad Request");

    return 1; // request processed
}

// @return
//         1 if processed
//         0 if error (another handler may try handling the request
int handleProjectResource(struct mg_connection *conn)
{
    // uri should be: /project/resource

    const char *uri = mg_get_request_info(conn)->uri;
    // project related URI
    const int LOCAL_URI_SIZE = 256;
    char uriLocal[LOCAL_URI_SIZE+1];
    if (strlen(uri) > LOCAL_URI_SIZE) {
        LOG_ERROR("URI too long: %d bytes / %s", strlen(uri), uri);
        return handleInvalidResource(conn);
    }
    memcpy(uriLocal, uri, strlen(uri));

    char *context = 0;
    const char *projectName = strtok_r(uriLocal, "/", &context);
    if (!projectName) {
        // not a project resource. not processed here but in handleResourceRoot()
        LOG_DEBUG("Empty project resource.");
        return 0; // not processed
    }

    // check if this project is loaded
    if (!Database::hasProject(projectName)) {
        // unkown project
        LOG_DEBUG("No such project '%s'.", projectName);
        return 0; // not process. Another handler may process it (eg: static file)
    }

    const char *resourceKind = strtok_r(0, "/", &context);
    if (!resourceKind) resourceKind = "issues"; // default
    LOG_DEBUG("projectName=%s, resourceKind=%s", projectName, resourceKind);


    std::string req = request2string(conn);

    sendHttpHeader200(conn);

    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    mg_printf(conn, "Request=%s\n", req.c_str());
    mg_printf(conn, "projectName=%s, resourceKind=%s", projectName, resourceKind);
    return 1;
}

int begin_request_handler(struct mg_connection *conn) {

    const char *uri = mg_get_request_info(conn)->uri;
    LOG_DEBUG("uri=%s", uri);
    if (0 == strcmp(uri, "/admin")) return handleResourceAdmin(conn);
    else  if (0 == strcmp(uri, "/signin")) return handleResourceSignin(conn);
    else  if (0 == strcmp(uri, "/users")) return handleResourceUsers(conn);
    else  if (0 == strcmp(uri, "/")) return handleResourceRoot(conn);
    else return handleProjectResource(conn);

    // Mark request as processed
    return 1;
}

void upload_handler(struct mg_connection *conn, const char *path) {
    mg_printf(conn, "Saved [%s]", path);
    std::string req = request2string(conn);
    mg_printf(conn, "%s", req.c_str());

}

