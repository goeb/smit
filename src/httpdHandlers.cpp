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
#include "renderingText.h"
#include "renderingHtml.h"
#include "parseConfig.h"
#include "stringTools.h"
#include "session.h"


// static members
std::string Rootdir;


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

/**
  * @param otherHeader
  *    Must not include the line-terminating \r\n
  */
void sendHttpRedirect(struct mg_connection *conn, const char *redirectUrl, const char *otherHeader)
{
    mg_printf(conn, "HTTP/1.1 303 See Other\r\n");
    const char *scheme = 0;
    if (mg_get_request_info(conn)->is_ssl) scheme = "https";
    else scheme = "http";

    const char *host = mg_get_header(conn, "Host"); // includes the TCP port (optionally)
    if (!host) {
        LOG_ERROR("No Host header in base request");
        host ="";
    }

    if (redirectUrl[0] == '/') redirectUrl++;
    mg_printf(conn, "Location: %s://%s/%s", scheme, host, redirectUrl);

    if (otherHeader) mg_printf(conn, "\r\n%s", otherHeader);
    mg_printf(conn, "\r\n\r\n");
}


void setCookieAndRedirect(struct mg_connection *conn, const char *name, const char *value, const char *redirectUrl)
{
    std::ostringstream s;
    s << "Set-Cookie: " << name << "=" << value;
    sendHttpRedirect(conn, redirectUrl, s.str().c_str());
}

int sendHttpHeaderInvalidResource(struct mg_connection *conn)
{
    const char *uri = mg_get_request_info(conn)->uri;
    LOG_INFO("Invalid resource: uri=%s", uri);
    mg_printf(conn, "HTTP/1.0 400 Bad Request\r\n\r\n");
    mg_printf(conn, "400 Bad Request");

    return 1; // request processed
}


// if uri is "x=y&a=bcd" and param is "a"
// then return "bcd"
std::string getFirstParamFromQueryString(const std::string & queryString, const char *param)
{
    std::string q = queryString;
    std::string paramEqual = param;
    paramEqual += "=";
    std::string token;
    while ((token = popToken(q, '&')) != "") {
        if (0 == token.compare(0, paramEqual.size(), paramEqual.c_str())) {
            popToken(token, '='); // remove the param= part
            //len = mg_url_decode(p, (size_t)(s - p), dst, dst_len, 1);
            //int mg_url_decode(const char *src, int src_len, char *dst,
            //                  int dst_len, int is_form_url_encoded) {

            return token;
        }
    }
    return "";
}

/** if uri is "x=y&a=bcd&a=efg" and param is "a"
  * then return a list [ "bcd", "efg" ]
  */
std::list<std::string> getParamListFromQueryString(const std::string & queryString, const char *param)
{
    std::list<std::string> result;
    std::string q = queryString; // TODO optimizattion can be done here (no need for copying..)
    std::string paramEqual = param;
    paramEqual += "=";
    std::string token;
    while ((token = popToken(q, '&')) != "") {
        if (0 == token.compare(0, paramEqual.size(), paramEqual.c_str())) {
            popToken(token, '='); // remove the param= part
            //len = mg_url_decode(p, (size_t)(s - p), dst, dst_len, 1);
            //int mg_url_decode(const char *src, int src_len, char *dst,
            //                  int dst_len, int is_form_url_encoded) {

            result.push_back(token);
        }
    }
    return result;
}


void httpGetAdmin(struct mg_connection *conn)
{
    // print list of available projects
    std::list<std::string> pList = getProjectList();

    const struct mg_request_info *req = mg_get_request_info(conn);
    std::string q;
    if (req->query_string) q = req->query_string;
    std::string format = getFirstParamFromQueryString(q, "format");

    sendHttpHeader200(conn);

    if (format == "text") RText::printProjectList(conn, pList);
    else RText::printProjectList(conn, pList);
}

void httpPostAdmin(struct mg_connection *conn)
{
}

void httpPostSignin(struct mg_connection *conn)
{
    // TODO check user name and credentials
    setCookieAndRedirect(conn, "sessid", "12345", "/"); // TODO
}

void httGetUsers(struct mg_connection *conn)
{
}

void httPostUsers(struct mg_connection *conn)
{
}
std::string getSessionIdFromCookie(struct mg_connection *conn)
{
    const char *cookie = mg_get_header(conn, "Cookie");
    if (cookie) {
        LOG_DEBUG("Cookie found: %s", cookie);
        std::string c = cookie;
        while (c.size() > 0) {
            std::string name = popToken(c, '=');
            trim(name, ' '); // remove spaces around
            std::string value = popToken(c, ';');
            trim(value, ' '); // remove spaces around

            LOG_DEBUG("Cookie: name=%s, value=%s", name.c_str(), value.c_str());
            if (name == "sessid") return value;
        }

    } else {
        LOG_DEBUG("no Cookie found");
    }
    return "";
}

void httpGetRoot(struct mg_connection *conn)
{
    std::string sessionId = getSessionIdFromCookie(conn);
    //std::string req = request2string(conn);
    sendHttpHeader200(conn);

    LOG_ERROR("session id not verified (TODO)");
    if (sessionId.size()) { // TODO do a thourough check
        // print list of available projects
        std::list<std::string> pList = getProjectList();

        const struct mg_request_info *req = mg_get_request_info(conn);
        std::string q;
        if (req->query_string) q = req->query_string;
        std::string format = getFirstParamFromQueryString(q, "format");

        if (format == "text") RText::printProjectList(conn, pList);
        else RText::printProjectList(conn, pList);

    } else RHtml::printSigninPage(conn, Rootdir.c_str());

}

/** @param filter
  *     [ "version:v1.0", "version:1.0", "owner:John Doe" ]
  */
std::map<std::string, std::list<std::string> > parseFilter(const std::list<std::string> &filters)
{
    std::map<std::string, std::list<std::string> > result;
    std::list<std::string>::const_iterator i;
    for (i = filters.begin(); i != filters.end(); i++) {
        // split apart from the first colon
        size_t colon = (*i).find_first_of(":");
        std::string propertyName = (*i).substr(0, colon);
        std::string propertyValue = "";
        if (colon != std::string::npos && colon < (*i).size()-1) propertyValue = (*i).substr(colon+1);

        if (result.find(propertyName) == result.end()) result[propertyName] = std::list<std::string>();

        result[propertyName].push_back(propertyValue);
    }

    return result;
}

void httpGetListOfIssues(struct mg_connection *conn, Project &p)
{
    // get query string parameters:
    //     colspec    which fields are to be displayed in the table, and their order
    //     filter     select issues with fields of the given values
    //     sort       indicate sorting

    const struct mg_request_info *req = mg_get_request_info(conn);
    std::string q;
    if (req->query_string) q = req->query_string;

    std::map<std::string, std::list<std::string> > filterIn = parseFilter(getParamListFromQueryString(q, "filterin"));
    std::map<std::string, std::list<std::string> > filterOut = parseFilter(getParamListFromQueryString(q, "filterout"));
    std::string fulltextSearch = getFirstParamFromQueryString(q, "search");

    std::string sorting = getFirstParamFromQueryString(q, "sort");

    std::list<struct Issue*> issueList = p.search(fulltextSearch.c_str(), filterIn, filterOut, sorting.c_str());


    std::string colspec = getFirstParamFromQueryString(q, "colspec");
    std::list<std::string> cols;
    if (colspec.size() > 0) {
        cols = parseColspec(colspec.c_str());
    } else {
        // get default colspec
        cols = p.getDefaultColspec();
    }
    std::string format = getFirstParamFromQueryString(q, "format");

    sendHttpHeader200(conn);

    if (format == "text") RText::printIssueList(conn, issueList, cols);
    else {
        ContextParameters ctx = ContextParameters("xxx-username", 0, p);

        RHtml::printIssueList(conn, ctx, issueList, cols);
    }

}


void httpGetNewIssueForm(struct mg_connection *conn, const Project &p)
{

    sendHttpHeader200(conn);

    ContextParameters ctx = ContextParameters("xxx-username", 0, p);

    // only HTML format is needed
    RHtml::printNewIssuePage(conn, ctx);
}

void httpGetIssue(struct mg_connection *conn, Project &p, const std::string & issueId)
{
    LOG_DEBUG("httpGetIssue: project=%s, issue=%s", p.getName().c_str(), issueId.c_str());

    const struct mg_request_info *req = mg_get_request_info(conn);
    std::string q;
    if (req->query_string) q = req->query_string;

    Issue issue;
    std::list<Entry*> Entries;
    int r = p.get(issueId.c_str(), issue, Entries);
    if (r < 0) {
        // issue not found or other error
        sendHttpHeaderInvalidResource(conn);
    } else {
        std::string format = getFirstParamFromQueryString(q, "format");

        sendHttpHeader200(conn);

        if (format == "text") RText::printIssue(conn, issue, Entries);
        else {
            ContextParameters ctx = ContextParameters("xxx-username", 0, p);
            RHtml::printIssue(conn, ctx, issue, Entries);
        }

    }
}

void urlDecode(std::string &s)
{
    // convert key and value
    const int SIZ = 1024;
    char localBuf[SIZ+1];

    // output of URL decoding takes less characters than input

    if (s.size() <= 1024) { // use local buffer
        int r = mg_url_decode(s.c_str(), s.size(), localBuf, SIZ, 1);
        if (r == -1) LOG_ERROR("Destination of mg_url_decode is too short: input size=%d, destination size=%d",
                               s.size(), SIZ);
        s = localBuf;
    } else {
        // allocated dynamically a buffer
        char *buffer = new char[s.size()];
        int r = mg_url_decode(s.c_str(), s.size(), buffer, s.size(), 1);
        if (r == -1) LOG_ERROR("Destination of mg_url_decode is too short (2): destination size=%d", s.size());
        else s = buffer;
    }
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

    urlDecode(key);
    urlDecode(value);
}


void parseQueryString(const std::string & queryString, std::map<std::string, std::list<std::string> > &vars)
{
    size_t n = queryString.size();
    size_t i;
    size_t offsetOfCurrentVar = 0;
    bool pendingParam = false; // indicate if a remaining param must be process after the 'if' loop
    for (i=0; i<n; i++) {
        if ( (queryString[i] == '&') || (i == n-1) ) {
            // param delimiter encountered or last character reached
            std::string var;
            size_t length;
            if (queryString[i] == '&') length = i-offsetOfCurrentVar; // do not take the '&'
            else length = i-offsetOfCurrentVar+1; // take the last char

            var = queryString.substr(offsetOfCurrentVar, length);

            std::string key, value;
            parseQueryStringVar(var, key, value);
            if (key.size() > 0) {
                if (vars.count(key) == 0) {
                    std::list<std::string> L;
                    L.push_back(value);
                    vars[key] = L;
                } else vars[key].push_back(value);
            }
            offsetOfCurrentVar = i+1;
            pendingParam = false;

        }
    }
    // append the latest parameter (if any)
}

/** Handle the posting of an entry
  * If issueId is empty, then a new issue is created.
  */
void httpPostEntry(struct mg_connection *conn, Project &p, const std::string & issueId)
{
    const char *contentType = mg_get_header(conn, "Content-Type");
    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {
        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".
        // multiselect is like: "tags=v4.1&tags=v5.0" (same var repeated)

        const int SIZ = 4096;
        char postFragment[SIZ+1];
        int n; // number of bytes read
        std::string postData;
        while (n = mg_read(conn, postFragment, SIZ)) {
            postFragment[n] = 0;
            LOG_DEBUG("postFragment=%s", postFragment);
            if (postData.size() > 10*SIZ*SIZ) {
                // 10 MByte is too much. overflow. abort.
                LOG_ERROR("Too much POST data. Abort.");
                return;
            }
            postData += postFragment;
        }
        std::map<std::string, std::list<std::string> > vars;
        parseQueryString(postData, vars);

        std::string id = issueId;
        if (id == "new") id = ""; // TODO check if conflict between "new" and issue ids.
        int r = p.addEntry(vars, id);
        if (r != 0) {
            // error
        } else {
            // HTTP redirect
            std::string redirectUrl = p.getName() + "/issues/" + id;
            sendHttpRedirect(conn, redirectUrl.c_str(), 0);
        }

    } else {
        // multipart/form-data
        LOG_ERROR("Content-Type '%s' not supported", contentType);
    }
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

    LOG_DEBUG("begin_request_handler");
    bool handled = true;
    std::string uri = mg_get_request_info(conn)->uri;
    std::string method = mg_get_request_info(conn)->request_method;
    LOG_DEBUG("uri=%s, method=%s", uri.c_str(), method.c_str());
    if      ( (uri == "/admin") && (method == "GET") ) httpGetAdmin(conn);
    else if ( (uri == "/admin") && (method == "POST") ) httpPostAdmin(conn);
    else if ( (uri == "/signin") && (method == "POST") ) httpPostSignin(conn);
    else if ( (uri == "/users") && (method == "GET") ) httGetUsers(conn);
    else if ( (uri == "/users") && (method == "POST") ) httPostUsers(conn);
    else if ( (uri == "/") && (method == "GET") ) httpGetRoot(conn);
    else {
        // check if it is a valid project resource such as /myp/issues, /myp/users, /myp/config
        std::string project = popToken(uri, '/');
        Project *p = Database::Db.getProject(project);
        LOG_DEBUG("project %s, %p", project.c_str(), p);
        if (p) {
            std::string resource = popToken(uri, '/');
            if      ( (resource == "issues") && (method == "GET") && uri.empty() ) httpGetListOfIssues(conn, *p);
            else if ( (resource == "issues") && (method == "POST") ) httpPostEntry(conn, *p, uri);
            else if ( (resource == "issues") && (uri == "new") && (method == "GET") ) httpGetNewIssueForm(conn, *p);
            else if ( (resource == "issues") && (method == "GET") ) httpGetIssue(conn, *p, uri);
            else handled = false;
        } else handled = false;
    }
    if (handled) return 1;
    else return 0; // let Mongoose handle static file
}

void upload_handler(struct mg_connection *conn, const char *path) {
    mg_printf(conn, "Saved [%s]", path);
    std::string req = request2string(conn);
    mg_printf(conn, "%s", req.c_str());

}

