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

int sendHttpHeaderInvalidResource(struct mg_connection *conn)
{
    const char *uri = mg_get_request_info(conn)->uri;
    LOG_INFO("Invalid resource: uri=%s", uri);
    mg_printf(conn, "HTTP/1.0 400 Bad Request\r\n\r\n");
    mg_printf(conn, "400 Bad Request");

    return 1; // request processed
}

//
void trimLeft(std::string & s, char c)
{
    size_t i = 0;
    while ( (s.size() > i) && (s[i] == c) ) i++;

    if (i >= s.size()) s = "";
    else s = s.substr(i);
}

// take first token name out of uri
// /a/b/c -> a and b/c
// a/b/c -> a and b/c
std::string popUriToken(std::string & uri)
{
    if (uri.empty()) return "";

    size_t i = 0;
    trimLeft(uri, '/');

    size_t pos = uri.find_first_of("/", i); // skip the first leading / of the uri
    std::string firstToken = uri.substr(i, pos-i);

    if (pos == std::string::npos) uri = "";
    else {
        uri = uri.substr(pos);
        trimLeft(uri, '/');
    }

    return firstToken;
}


void httpGetAdmin(struct mg_connection *conn) {
}

void httpPostAdmin(struct mg_connection *conn) {
}

void httpGetSignin(struct mg_connection *conn) {
}

void httpPostSignin(struct mg_connection *conn) {
}

void httGetUsers(struct mg_connection *conn) {
}

void httPostUsers(struct mg_connection *conn) {
}

void httpGetRoot(struct mg_connection *conn) {
    std::string req = request2string(conn);

    sendHttpHeader200(conn);

    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    mg_printf(conn, "Request=%s\n", req.c_str());
}


void httpGetListOfIssues(struct mg_connection *conn, const std::string & projectName) {
}

void httpPostNewIssue(struct mg_connection *conn, const std::string & projectName) {
}

void httpGetNewIssueForm(struct mg_connection *conn, const std::string & projectName) {
}

void httpGetIssue(struct mg_connection *conn, const std::string & projectName, const std::string & issueId) {
}

void httpPostEntry(struct mg_connection *conn, const std::string & projectName, const std::string & issueId) {
}


// begin_request_handler is the main entry point of an incoming HTTP request
//
// Resources              Methods    Acces Granted     Description
//-------------------------------------------------------------------------
//     /                  GET        user              list of projects
//v1.0 /signin            GET/POST   all               sign-in page
//     /admin             GET/POST   global-admin      management of projects (create, ...)
//v1.0 /users                        global-admin      management of users for all projects
//v1.0 /myp/config        GET/POST   project-admin     configuration of the project
//     /myp/users         GET/POST   project-admin     local configuration of users of the project (names, access rights, etc.)
//v1.0 /myp/issues        GET/POST   user              issues of the project / add new issue
//v1.0 /myp/issues/new    GET        user              page with a form for submitting new issue
//     /myp/issues/XYZ    GET/POST   user              a particular issue: get all entries or add a new entry
//v1.0 /any/other/file    GET        user              any existing file (relatively to the repository)

int begin_request_handler(struct mg_connection *conn) {

    bool handled = true;
    std::string uri = mg_get_request_info(conn)->uri;
    std::string method = mg_get_request_info(conn)->request_method;
    LOG_DEBUG("uri=%s, method=%s", uri.c_str(), method.c_str());
    if      ( (uri == "/admin") && (method == "GET") ) httpGetAdmin(conn);
    else if ( (uri == "/admin") && (method == "POST") ) httpPostAdmin(conn);
    else if ( (uri == "/signin") && (method == "GET") ) httpGetSignin(conn);
    else if ( (uri == "/signin") && (method == "POST") ) httpPostSignin(conn);
    else if ( (uri == "/users") && (method == "GET") ) httGetUsers(conn);
    else if ( (uri == "/users") && (method == "POST") ) httPostUsers(conn);
    else if ( (uri == "/") && (method == "GET") ) httpGetRoot(conn);
    else {
        // check if it is a valid project resource such as /myp/issues, /myp/users, /myp/config
        std::string project = popUriToken(uri);
        if (Database::Db.hasProject(project)) {
            bool handled = true;
            std::string resource = popUriToken(uri);
            if      ( (resource == "issues") && (method == "GET") ) httpGetListOfIssues(conn, project);
            else if ( (resource == "issues") && (method == "POST") ) httpPostNewIssue(conn, project);
            else if ( (resource == "issues") && (uri == "/new") && (method == "GET") ) httpGetNewIssueForm(conn, project);
            else if ( (resource == "issues") && (method == "GET") ) httpGetIssue(conn, project, uri);
            else if ( (resource == "issues") && (method == "POST") ) httpPostEntry(conn, project, uri);
            else handled = false;
        }
    }
    if (handled) return 1;
    else return 0; // let Mongoose handle static file
}

void upload_handler(struct mg_connection *conn, const char *path) {
    mg_printf(conn, "Saved [%s]", path);
    std::string req = request2string(conn);
    mg_printf(conn, "%s", req.c_str());

}

