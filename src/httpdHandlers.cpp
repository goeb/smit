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

void sendHttpHeader403(struct mg_connection *conn)
{
    mg_printf(conn, "HTTP/1.1 403 Forbidden\r\n\r\n");
    mg_printf(conn, "403 Forbidden\r\n");
}
void sendHttpHeader500(struct mg_connection *conn)
{
    mg_printf(conn, "HTTP/1.1 500 Forbidden\r\n\r\n");
    mg_printf(conn, "500 Internal Error\r\n");
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
    s << "; Max-Age=" << SESSION_DURATION;
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


/** If uri is "x=y&a=bc+d" and param is "a"
  * then return "bc d".
  * @param param
  *     Must be url-encoded.
  *
  * @return
  *     Url-decoded value
  */
std::string getFirstParamFromQueryString(const std::string & queryString, const char *param)
{
    std::string q = queryString;
    std::string paramEqual = param;
    paramEqual += "=";
    std::string token;
    while ((token = popToken(q, '&')) != "") {
        if (0 == token.compare(0, paramEqual.size(), paramEqual.c_str())) {
            popToken(token, '='); // remove the 'param=' part
            token = urlDecode(token);

            return token;
        }
    }
    return "";
}

/** if uri is "x=y&a=bcd&a=efg" and param is "a"
  * then return a list [ "bcd", "efg" ]
  * @return
  *     Url-decoded values
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
            token = urlDecode(token);

            result.push_back(token);
        }
    }
    return result;
}

enum RenderingFormat { RENDERING_HTML, RENDERING_TEXT };
enum RenderingFormat getFormat(struct mg_connection *conn)
{
    const struct mg_request_info *req = mg_get_request_info(conn);
    std::string q;
    if (req->query_string) q = req->query_string;
    std::string format = getFirstParamFromQueryString(q, "format");

    if (format == "html" || format.empty()) return RENDERING_HTML;
    else return RENDERING_TEXT;
}


void httpPostRoot(struct mg_connection *conn, User u)
{
}

void httpPostSignin(struct mg_connection *conn)
{
    const char *contentType = mg_get_header(conn, "Content-Type");

    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {
        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".

        const int SIZ = 1024;
        char buffer[SIZ+1];
        int n; // number of bytes read
        n = mg_read(conn, buffer, SIZ);
        if (n == SIZ) {
            LOG_ERROR("Post data for signin too long. Abort request.");
            return;
        }
        buffer[n] = 0;
        LOG_DEBUG("postData=%s", buffer);
        std::string postData = buffer;

        // get the username
        int r = mg_get_var(postData.c_str(), postData.size(),
                           "username", buffer, SIZ);
        if (r<=0) {
            // error: empty, or too long, or not present
            LOG_DEBUG("Cannot get username. r=%d, postData=%s", r, postData.c_str());
            return;
        }
        std::string username = buffer;

        // get the password
        r = mg_get_var(postData.c_str(), postData.size(),
                       "password", buffer, SIZ);

        if (r<0) {
            // error: empty, or too long, or not present
            LOG_DEBUG("Cannot get password. r=%d, postData=%s", r, postData.c_str());
            return;
        }
        std::string password = buffer;

        std::string redirect;
        enum RenderingFormat format = getFormat(conn);
        if (format == RENDERING_TEXT) {
            // no need to get the redirect location

        } else {
            // get the redirect page
            r = mg_get_var(postData.c_str(), postData.size(),
                           "redirect", buffer, SIZ);

            if (r<0) {
                // error: empty, or too long, or not present
                LOG_DEBUG("Cannot get redirect. r=%d, postData=%s", r, postData.c_str());
                return;
            }
            redirect = buffer;
        }


        // check credentials
        std::string sessionId = SessionBase::requestSession(username, password);

        if (sessionId.size() == 0) {
            LOG_DEBUG("Authentication refused");
            sendHttpHeader403(conn);
            return;
        }

        setCookieAndRedirect(conn, "sessid", sessionId.c_str(), redirect.c_str());

    } else {
        LOG_ERROR("Unsupported Content-Type in httpPostSignin: %s", contentType);
        return;
    }

}

void redirectToSignin(struct mg_connection *conn, const std::string &resource)
{
    sendHttpHeader200(conn);
    RHtml::printSigninPage(conn, Rootdir.c_str(), resource.c_str());
}


void httpPostSignout(struct mg_connection *conn, const std::string &sessionId)
{
    SessionBase::destroySession(sessionId);
    redirectToSignin(conn, "/");
}


void httGetUsers(struct mg_connection *conn, User u)
{
}

void httPostUsers(struct mg_connection *conn, User u)
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
            trim(name, " "); // remove spaces around
            std::string value = popToken(c, ';');
            trim(value, " "); // remove spaces around

            LOG_DEBUG("Cookie: name=%s, value=%s", name.c_str(), value.c_str());
            if (name == "sessid") return value;
        }

    } else {
        LOG_DEBUG("no Cookie found");
    }
    return "";
}

void handleUnauthorizedAccess(struct mg_connection *conn, const std::string &resource)
{
    sendHttpHeader403(conn);
}

void httpGetRoot(struct mg_connection *conn, User u)
{
    //std::string req = request2string(conn);
    sendHttpHeader200(conn);
    // print list of available projects
    std::list<std::pair<std::string, std::string> > pList = u.getProjects();

    enum RenderingFormat format = getFormat(conn);

    if (format == RENDERING_TEXT) RText::printProjectList(conn, pList);
    else {
        ContextParameters ctx = ContextParameters(u, Database::Db.pathToRepository);
        ctx.rootdir = Rootdir;

        RHtml::printProjectList(conn, ctx, pList);
    }
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
void httpGetProjectConfig(struct mg_connection *conn, Project &p, User u)
{
    if (u.getRole(p.getName()) != ROLE_ADMIN) return sendHttpHeader403(conn);

    enum RenderingFormat format = getFormat(conn);
    sendHttpHeader200(conn);
    if (format == RENDERING_HTML) {
        ContextParameters ctx = ContextParameters(u, p);
        ctx.rootdir = Rootdir;
        RHtml::printProjectConfig(conn, ctx);
    }

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
            tokens.push_back(line);
            LOG_DEBUG("convertPostToTokens: line=%s", toString(line).c_str());
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
    if (line.size() > 0) {
        tokens.push_back(line);
        LOG_DEBUG("convertPostToTokens: line=%s", toString(line).c_str());
    }
    return tokens;
}
std::string readMgConn(struct mg_connection *conn, size_t maxSize)
{
    std::string postData;
    const int SIZ = 4096;
    char postFragment[SIZ+1];
    int n; // number of bytes read

    while ( (n = mg_read(conn, postFragment, SIZ)) ) {
        postFragment[n] = 0;
        LOG_DEBUG("postFragment=%s", postFragment);
        if (postData.size() > maxSize) {
            // 10 MByte is too much. overflow. abort.
            LOG_ERROR("Too much POST data. Abort.");
            break;
        }
        postData += postFragment;
    }
    return postData;
}


void httpPostProjectConfig(struct mg_connection *conn, Project &p, User u)
{
    enum Role r = u.getRole(p.getName());
    if (r != ROLE_ADMIN) {
        sendHttpHeader403(conn);
        return;
    }

    std::string postData;

    const char *contentType = mg_get_header(conn, "Content-Type");

    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {
        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".

        postData = readMgConn(conn, 4096);

        // convert the post data to tokens
        std::list<std::list<std::string> > tokens = convertPostToTokens(postData);
        int r = p.modifyConfig(tokens);
        if (r == 0) {
            // success, redirect to
            std::string redirectUrl = p.getName() + "/config";
            sendHttpRedirect(conn, redirectUrl.c_str(), 0);

        } else { // error
            sendHttpHeader500(conn);
        }
    }
}

void httpGetListOfIssues(struct mg_connection *conn, Project &p, User u)
{
    // get query string parameters:
    //     colspec    which fields are to be displayed in the table, and their order
    //     filter     select issues with fields of the given values
    //     sort       indicate sorting

    const struct mg_request_info *req = mg_get_request_info(conn);
    std::string q;
    if (req->query_string) q = req->query_string;

    std::list<std::string> filterin = getParamListFromQueryString(q, "filterin");
    std::list<std::string> filterout = getParamListFromQueryString(q, "filterout");
    std::map<std::string, std::list<std::string> > filterIn = parseFilter(filterin);
    std::map<std::string, std::list<std::string> > filterOut = parseFilter(filterout);
    std::string fulltextSearch = getFirstParamFromQueryString(q, "search");

    std::string sorting = getFirstParamFromQueryString(q, "sort");

    std::list<struct Issue*> issueList = p.search(fulltextSearch.c_str(), filterIn, filterOut, sorting.c_str());


    std::string colspec = getFirstParamFromQueryString(q, "colspec");
    std::list<std::string> cols;
    std::list<std::string> allCols = p.getPropertiesNames();

    if (colspec.size() > 0) {
        cols = parseColspec(colspec.c_str(), allCols);
    } else {
        cols = allCols;
    }
    enum RenderingFormat format = getFormat(conn);

    sendHttpHeader200(conn);

    if (format == RENDERING_TEXT) RText::printIssueList(conn, issueList, cols);
    else {
        ContextParameters ctx = ContextParameters(u, p);
        ctx.filterin = filterin;
        ctx.filterout = filterout;
        ctx.search = fulltextSearch;
        ctx.sort = sorting;

        RHtml::printIssueList(conn, ctx, issueList, cols);
    }

}

void httpGetProject(struct mg_connection *conn, Project &p, User u)
{
    // redirect to list of issues
    return httpGetListOfIssues(conn, p, u);
}


void httpGetNewIssueForm(struct mg_connection *conn, const Project &p, User u)
{
    enum Role role = u.getRole(p.getName());
    if (role != ROLE_RW && role != ROLE_ADMIN) {
        sendHttpHeader403(conn);
        return;
    }

    sendHttpHeader200(conn);

    ContextParameters ctx = ContextParameters(u, p);

    // only HTML format is needed
    RHtml::printNewIssuePage(conn, ctx);
}

void httpGetIssue(struct mg_connection *conn, Project &p, const std::string & issueId, User u)
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
        enum RenderingFormat format = getFormat(conn);

        sendHttpHeader200(conn);

        if (format == RENDERING_TEXT) RText::printIssue(conn, issue, Entries);
        else {
            ContextParameters ctx = ContextParameters(u, p);
            RHtml::printIssue(conn, ctx, issue, Entries);
        }

    }
}

/** Used for deleting an entry
  * @param details
  *     should be of the form: XYZ/delete
  *
  * @return
  *     0, let Mongoose handle static file
  *     1, do not.
  */
int httpDeleteEntry(struct mg_connection *conn, Project &p, std::string details, User u)
{
    LOG_DEBUG("httpPostEntry: project=%s, details=%s", p.getName().c_str(), details.c_str());

    std::string entryId = popToken(details, '/');
    if (details != "delete") return 0; // let Mongoose handle static file

    enum Role role = u.getRole(p.getName());
    if (role != ROLE_RW && role != ROLE_ADMIN) {
        sendHttpHeader403(conn);
        return 1; // request fully handled
    }

    int r = p.deleteEntry(entryId, u.username);
    if (r < 0) {
        // failure
        LOG_INFO("deleteEntry returned %d", r);
        sendHttpHeader403(conn);

    } else {
        sendHttpHeader200(conn);
        mg_printf(conn, "\r\n"); // no contents
    }

    return 1; // request fully handled
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


void parseQueryString(const std::string & queryString, std::map<std::string, std::list<std::string> > &vars)
{
    size_t n = queryString.size();
    size_t i;
    size_t offsetOfCurrentVar = 0;
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
        }
    }
    // append the latest parameter (if any)
}



/** Handle the posting of an entry
  * If issueId is empty, then a new issue is created.
  */
void httpPostEntry(struct mg_connection *conn, Project &p, const std::string & issueId, User u)
{
    enum Role r = u.getRole(p.getName());
    if (r != ROLE_RW && r != ROLE_ADMIN) {
        sendHttpHeader403(conn);
        return;
    }

    const char *contentType = mg_get_header(conn, "Content-Type");
    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {
        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".
        // multiselect is like: "tags=v4.1&tags=v5.0" (same var repeated)

        std::string postData = readMgConn(conn, 10*1024*1024);
        std::map<std::string, std::list<std::string> > vars;
        parseQueryString(postData, vars);

        std::string id = issueId;
        if (id == "new") id = ""; // TODO check if conflict between "new" and issue ids.
        int r = p.addEntry(vars, id, u.username);
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


/** begin_request_handler is the main entry point of an incoming HTTP request
  *
  * Resources               Methods    Acces Granted     Description
  * -------------------------------------------------------------------------
  * /                       GET/POST   user              list of projects / management of projects (create, ...)
  * /public/...             GET        all               public pages, javascript, CSS, logo
  * /signin                 POST       all               sign-in
  * /users                             superadmin        management of users for all projects
  * /myp/config             GET/POST   project-admin     configuration of the project
  * /myp/views              GET/POST   user              configuration of predefined views
  * /myp/issues             GET/POST   user              issues of the project / add new issue
  * /myp/issues/new         GET        user              page with a form for submitting new issue
  * /myp/issues/XYZ         GET/POST   user              a particular issue: get all entries or add a new entry
  * /myp/entries/XYZ/delete POST       user              delete an entry
  * /any/other/file         GET        user              any existing file (relatively to the repository)
  */

int begin_request_handler(struct mg_connection *conn) {

    LOG_DEBUG("begin_request_handler");

    // check acces rights
    std::string sessionId = getSessionIdFromCookie(conn);
    LOG_DEBUG("session id: %s", sessionId.c_str());
    User user = SessionBase::getLoggedInUser(sessionId);
    // if username is empty, then no access is granted (only public pages will be available)

    LOG_DEBUG("User logged: %s", user.username.c_str());

    bool handled = true;
    std::string uri = mg_get_request_info(conn)->uri;
    std::string method = mg_get_request_info(conn)->request_method;
    LOG_DEBUG("uri=%s, method=%s", uri.c_str(), method.c_str());

    std::string resource = popToken(uri, '/');

    if      ( (resource == "public") && (method == "GET") ) return 0; // let Mongoose handle static file
    else if ( (resource == "signin") && (method == "POST") ) httpPostSignin(conn);
    else if ( (resource == "signout") && (method == "POST") ) httpPostSignout(conn, sessionId);
    else if (user.username.size() == 0) {

        // user not logged in
        if (getFormat(conn) == RENDERING_HTML) redirectToSignin(conn, resource + "/" + uri);
        else handleUnauthorizedAccess(conn, resource);

    }
    else if ( (resource == "") && (method == "GET") ) httpGetRoot(conn, user);
    else if ( (resource == "") && (method == "POST") ) httpPostRoot(conn, user);
    else if ( (resource == "users") && (method == "GET") ) httGetUsers(conn, user);
    else if ( (resource == "users") && (method == "POST") ) httPostUsers(conn, user);
    else {
        // check if it is a valid project resource such as /myp/issues, /myp/users, /myp/config
        std::string project = resource;

        // check if user has at lest read access
        enum Role r = user.getRole(project);
        if (r != ROLE_ADMIN && r != ROLE_RW && r != ROLE_RO) {
            // no access granted for this user to this project
            handleUnauthorizedAccess(conn, resource);

        } else {

            Project *p = Database::Db.getProject(project);
            LOG_DEBUG("project %s, %p", project.c_str(), p);
            if (p) {
                resource = popToken(uri, '/');
                if      ( resource.empty()       && (method == "GET") ) httpGetProject(conn, *p, user);
                else if ( (resource == "issues") && (method == "GET") && uri.empty() ) httpGetListOfIssues(conn, *p, user);
                else if ( (resource == "issues") && (method == "POST") ) httpPostEntry(conn, *p, uri, user);
                else if ( (resource == "issues") && (uri == "new") && (method == "GET") ) httpGetNewIssueForm(conn, *p, user);
                else if ( (resource == "issues") && (method == "GET") ) httpGetIssue(conn, *p, uri, user);
                else if ( (resource == "entries") && (method == "POST") ) return httpDeleteEntry(conn, *p, uri, user);
                else if ( (resource == "config") && (method == "GET") ) httpGetProjectConfig(conn, *p, user);
                else if ( (resource == "config") && (method == "POST") ) httpPostProjectConfig(conn, *p, user);
                else handled = false;


            } else handled = false; // the resource is not a project
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

