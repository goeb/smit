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

#include <string>
#include <sstream>
#include "mongoose.h"
#include "httpdHandlers.h"

#include "db.h"
#include "logging.h"
#include "identifiers.h"
#include "renderingText.h"
#include "renderingHtml.h"
#include "renderingCsv.h"
#include "parseConfig.h"
#include "stringTools.h"
#include "session.h"
#include "global.h"
#include "mg_win32.h"
#include "cpio.h"
#include "Trigger.h"

#define K_ME "me"
#define MAX_SIZE_UPLOAD (10*1024*1024)
#define SESSID "sessid"

/** Read bytes from the socket, and store in data
  *
  * @return
  *    0 on success
  *   -1 on error (data too big)
  */
int readMgConn(struct mg_connection *conn, std::string &data, size_t maxSize)
{
    data.clear();
    const int SIZ = 4096;
    char postFragment[SIZ+1];
    int n; // number of bytes read

    while ( (n = mg_read(conn, postFragment, SIZ)) > 0) {
        LOG_DEBUG("postFragment size=%d", n);
        data.append(std::string(postFragment, n));
        if (data.size() > maxSize) {
            // data too big. overflow. abort.
            LOG_ERROR("Too much POST data. Abort. maxSize=%zu", maxSize);
            return -1;
        }

    }
    LOG_DEBUG("data size=%zu", data.size());

    return 0; // ok
}


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

    std::string postData;
    readMgConn(conn, postData, 10*1024*1024);

    return std::string(content) + postData;

}

void sendHttpHeader200(struct mg_connection *conn)
{
    mg_printf(conn, "HTTP/1.0 200 OK\r\n");
}

int sendHttpHeader400(struct mg_connection *conn, const char *msg)
{
    mg_printf(conn, "HTTP/1.0 400 Bad Request\r\n\r\n");
    mg_printf(conn, "400 Bad Request\r\n");
    mg_printf(conn, "%s\r\n", msg);
    return 1; // request completely handled
}
void sendHttpHeader403(struct mg_connection *conn)
{
    mg_printf(conn, "HTTP/1.1 403 Forbidden\r\n\r\n");
    mg_printf(conn, "403 Forbidden\r\n");
}

void sendHttpHeader413(struct mg_connection *conn, const char *msg)
{
    mg_printf(conn, "HTTP/1.1 413 Request Entity Too Large\r\n\r\n");
    mg_printf(conn, "413 Request Entity Too Large\r\n%s\r\n", msg);
}

void sendHttpHeader404(struct mg_connection *conn)
{
    mg_printf(conn, "HTTP/1.1 404 Not Found\r\n\r\n");
    mg_printf(conn, "404 Not Found\r\n");
}

void sendHttpHeader500(struct mg_connection *conn, const char *msg)
{
    mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
    mg_printf(conn, "500 Internal Server Error\r\n");
    mg_printf(conn, "%s\r\n", msg);
}

void sendCookie(mg_connection *conn, const std::string &key, const std::string &value)
{
    mg_printf(conn, "Set-Cookie: %s=%s; Path=/\r\n", key.c_str(), value.c_str());
}


/**
  * @redirectUrl
  *    Must be an absolute path (starting with /)
  * @param otherHeader
  *    Must not include the line-terminating \r\n
  *    May be NULL
  */
void sendHttpRedirect(struct mg_connection *conn, const std::string &redirectUrl, const char *otherHeader)
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

    if (redirectUrl[0] != '/') LOG_ERROR("Invalid redirect URL (missing first /): %s", redirectUrl.c_str());

    mg_printf(conn, "Location: %s://%s%s", scheme, host, redirectUrl.c_str());

    if (otherHeader) mg_printf(conn, "\r\n%s", otherHeader);
    mg_printf(conn, "\r\n\r\n");
}

std::string getServerCookie(const std::string &name, const std::string &value, int maxAgeSeconds)
{
    std::ostringstream s;
    s << "Set-Cookie: " << name << "=" << value;
    s << "; Path=/";
    if (maxAgeSeconds > 0) s << ";  Max-Age=" << maxAgeSeconds;
    return s.str();
}

void setCookieAndRedirect(struct mg_connection *conn, const char *name, const char *value, const char *redirectUrl)
{
    std::string s = getServerCookie(name, value, SESSION_DURATION);
    sendHttpRedirect(conn, redirectUrl, s.c_str());
}

int sendHttpHeaderInvalidResource(struct mg_connection *conn)
{
    const char *uri = mg_get_request_info(conn)->uri;
    LOG_INFO("Invalid resource: uri=%s", uri);
    mg_printf(conn, "HTTP/1.0 400 Bad Request\r\n\r\n");
    mg_printf(conn, "400 Bad Request");

    return 1; // request processed
}



enum RenderingFormat { RENDERING_HTML, RENDERING_TEXT, RENDERING_CSV, X_SMIT };
enum RenderingFormat getFormat(struct mg_connection *conn)
{
    const struct mg_request_info *req = mg_get_request_info(conn);
    std::string q;
    if (req->query_string) q = req->query_string;
    std::string format = getFirstParamFromQueryString(q, "format");

    if (format == "html") return RENDERING_HTML;
    else if (format == "csv") return RENDERING_CSV;
    else if (format == "text") return RENDERING_TEXT;
    else {
        // look at the Accept header
        const char *contentType = mg_get_header(conn, "Accept");
        LOG_DEBUG("Accept header=%s", contentType);

        if (!contentType) return RENDERING_HTML; // no such header, return default value

        if (0 == strcasecmp(contentType, "text/html")) return RENDERING_HTML;
        if (0 == strcasecmp(contentType, "text/plain")) return RENDERING_TEXT;
        if (0 == strcasecmp(contentType, APP_X_SMIT)) return X_SMIT;
    }
    return RENDERING_HTML;
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
        if (format != RENDERING_HTML) {
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

        if (format == X_SMIT) {
            sendHttpHeader200(conn);
            mg_printf(conn, "%s\r\n", getServerCookie(SESSID, sessionId.c_str(), SESSION_DURATION).c_str());
        } else {
            if (redirect.empty()) redirect = "/";
            setCookieAndRedirect(conn, SESSID, sessionId.c_str(), redirect.c_str());
        }

    } else {
        LOG_ERROR("Unsupported Content-Type in httpPostSignin: %s", contentType);
        return;
    }

}

void redirectToSignin(struct mg_connection *conn, const char *resource = 0)
{
    sendHttpHeader200(conn);
    std::string url;
    if (!resource) {
        struct mg_request_info *rq = mg_get_request_info(conn);
        url = rq->uri;
        const char *qs = rq->query_string;
        if (qs && strlen(qs)) url = url + '?' + qs;
        resource = url.c_str();
    }
    RHtml::printPageSignin(conn, resource);
}


void httpPostSignout(struct mg_connection *conn, const std::string &sessionId)
{
    SessionBase::destroySession(sessionId);
    redirectToSignin(conn, "/");
}


void handleMessagePreview(struct mg_connection *conn)
{
    LOG_FUNC();
    const struct mg_request_info *req = mg_get_request_info(conn);
    std::string q;
    if (req->query_string) q = req->query_string;
    std::string message = getFirstParamFromQueryString(q, "message");
    LOG_DEBUG("message=%s", message.c_str());
    message = RHtml::convertToRichText(htmlEscape(message));
    sendHttpHeader200(conn);
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    mg_printf(conn, "%s", message.c_str());
}

/** Get a SM embedded file
  *
  * Embbeded files: smit.js, etc.
  * Virtual files: preview
  */
int httpGetSm(struct mg_connection *conn, const std::string &file)
{
    int r; // return 0 to let mongoose handle static file, 1 otherwise

    LOG_DEBUG("httpGetSm: %s", file.c_str());
    const char *virtualFilePreview = "preview";
    if (0 == strncmp(file.c_str(), virtualFilePreview, strlen(virtualFilePreview))) {
        handleMessagePreview(conn);
        return 1;
    }

    std::string internalFile = "sm/" + file;
    const char *start;
    r = cpioGetFile(internalFile.c_str(), start);

    if (r >= 0) {
        int filesize = r;
        sendHttpHeader200(conn);
        const char *mimeType = mg_get_builtin_mime_type(file.c_str());
        LOG_DEBUG("mime-type=%s, size=%d", mimeType, filesize);
        mg_printf(conn, "Content-Type: %s\r\n\r\n", mimeType);

        // file found
        const uint32_t BS = 1024;
        char buffer[BS];
        int remainingBytes = filesize;
        const char *cpioOffset = start;
        const char *cpioEnd = start + filesize;

        while (remainingBytes > 0) {
            uint32_t nToRead = remainingBytes;
            if (nToRead > BS) nToRead = BS;

            if (cpioOffset + nToRead > cpioEnd) {
                LOG_ERROR("cpioExtract: Error while reading contents (eof)");
                return -1;
            }
            memcpy(buffer, cpioOffset, nToRead);
            cpioOffset += nToRead;

            mg_write(conn, buffer, nToRead);

            remainingBytes -= nToRead;
        }
        r = 1;
    } else {
        r = 0;
    }

    return r;
}

/** Get the configuration page of a given user
  *
  * @param signedInUser
  *     currently signed-in user
  *
  * @param username
  *     user whose configuration is requested
  *
  */
void httpGetUsers(struct mg_connection *conn, User signedInUser, const std::string &username)
{
    ContextParameters ctx = ContextParameters(conn, signedInUser);

    if (username.empty() || username == "_") {
        // display form for a new user
        // only a superadmin may do this
        if (!signedInUser.superadmin) {
            sendHttpHeader403(conn);
        } else {
            sendHttpHeader200(conn);
            RHtml::printPageUser(ctx, 0);
        }

    } else {
        // look for existing user
        User *editedUser = UserBase::getUser(username);

        if (!editedUser) {
            if (signedInUser.superadmin) sendHttpHeader404(conn);
            else sendHttpHeader403(conn);

        } else if (username == signedInUser.username || signedInUser.superadmin) {

            // handle an existing user
            // a user may only view his/her own user page
            sendHttpHeader200(conn);
            RHtml::printPageUser(ctx, editedUser);

        } else sendHttpHeader403(conn);
    }
}

/** Post configuration of a new or existing user
  *
  * Non-superadmin users may only post their password.
  */
void httpPostUsers(struct mg_connection *conn, User signedInUser, const std::string &username)
{
    if (!signedInUser.superadmin && username != signedInUser.username) {
        sendHttpHeader403(conn);
        return;
    }
    if (username.empty()) return sendHttpHeader403(conn);

    // get the posted parameters
    const char *contentType = mg_get_header(conn, "Content-Type");

    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {
        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".

        std::string postData;
        int rc = readMgConn(conn, postData, 4096);
        if (rc < 0) {
            sendHttpHeader413(conn, "You tried to upload too much data. Max is 4096 bytes.");
            return;
        }

        User newUserConfig;
        std::string passwd1, passwd2;
        std::string project, role;

        while (postData.size() > 0) {
            std::string tokenPair = popToken(postData, '&');
            std::string key = popToken(tokenPair, '=');
            std::string value = urlDecode(tokenPair);

            if (key == "name") newUserConfig.username = value;
            else if (key == "superadmin" && value == "on") newUserConfig.superadmin = true;
            else if (key == "passwd1") passwd1 = value;
            else if (key == "passwd2") passwd2 = value;
            else if (key == "project") project = value;
            else if (key == "role") role = value;
            else {
                LOG_ERROR("httpPostUsers: unexpected parameter '%s'", key.c_str());
            }

            // look if the pair project/role is complete
            if (!project.empty() && !role.empty()) {
                Role r = stringToRole(role);
                if (r != ROLE_NONE) newUserConfig.rolesOnProjects[project] = r;
                project.clear();
                role.clear();
            }
        }

        // Check that the username given in the form is not empty.
        // Only superadmin has priviledge for modifying this.
        // For other users, the disabled field makes the username empty.
        if (signedInUser.superadmin) {
            if (newUserConfig.username.empty() || newUserConfig.username == "_") {
                LOG_INFO("Ignore user parameters as username is empty or '_'");
                sendHttpHeader400(conn, "Invalid user name");
                return;
            }
        }

        int r = 0;
        std::string error;

        if (passwd1 != passwd2) {
            LOG_INFO("passwd1 (%s) != passwd2 (%s)", passwd1.c_str(), passwd2.c_str());
            sendHttpHeader400(conn, "passwords 1 and 2 do not match");
            return;

        }

        if (!signedInUser.superadmin) {
            // if signedInUser is not superadmin, only password is updated
            newUserConfig.username = username; // used below for redirection

            if (!passwd1.empty()) {
                r = UserBase::updatePassword(username, passwd1);
                if (r != 0) {
                    LOG_ERROR("Cannot update password of user '%s'", username.c_str());
                    error = "Cannot update password";
                } else LOG_INFO("Password updated for user '%s'", username.c_str());
            } else {
                LOG_DEBUG("Password not changed (empty)");
            }

        } else {
            // superadmin: update all parameters of the user's configuration
            if (!passwd1.empty()) newUserConfig.setPasswd(passwd1);

            if (username == "_") {
                r = UserBase::addUser(newUserConfig);
                if (r == -1) error = "Cannot create user with empty name";
                else if (r == -2) error = "Cannot create new user as name already exists";

            } else {
                r = UserBase::updateUser(username, newUserConfig);
                if (r == -1) error = "Cannot create user with empty name";
                else if (r == -2) error = "Cannot change name as new name already exists";
                else if (r < 0) error = "Cannot update non existing user";

            }

            if (r != 0) LOG_ERROR("Cannot update user '%s': %s", username.c_str(), error.c_str());
            else if (newUserConfig.username != username) {
                LOG_INFO("User '%s' renamed '%s'", username.c_str(), newUserConfig.username.c_str());
            } else LOG_INFO("Parameters updated for user '%s'", username.c_str());
        }

        if (r != 0) {
            sendHttpHeader400(conn, error.c_str());

        } else {
            // ok, redirect
            std::string redirectUrl = "/users/" + urlEncode(newUserConfig.username);
            sendHttpRedirect(conn, redirectUrl.c_str(), 0);
        }

    } else {
        LOG_ERROR("Bad contentType: %s", contentType);
    }
}

std::string getFromCookie(struct mg_connection *conn, const std::string &key)
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
            if (name == key) return value;
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

/** Serve a GET request to a file, for x-smit agent (cloning)
  *
  * Access restriction must have been done before calling this function.
  * (this function does not verify access rights)
  */
int httpGetFile(struct mg_connection *conn)
{
    std::string uri = mg_get_request_info(conn)->uri;
    std::string dir = Database::Db.pathToRepository + uri; // uri contains a leading /

    struct dirent *dp;
    DIR *dirp;
    if ((dirp = opendir(dir.c_str())) == NULL) return 0;  // not a directory: let Mongoose handle the GET

    // send the directory contents
    // walk through the directory and print the entries
    sendHttpHeader200(conn);
    mg_printf(conn, "Content-Type: text/directory\r\n\r\n");

    while ((dp = readdir(dirp)) != NULL) {
        // Do not show . and ..
        if (0 == strcmp(dp->d_name, ".")) continue;
        if (0 == strcmp(dp->d_name, "..")) continue;

        mg_printf(conn, "%s\n", dp->d_name);
    }
    closedir(dirp);

    return 1; // the request is completely handled
}


void httpGetRoot(struct mg_connection *conn, User u)
{
    //std::string req = request2string(conn);
    sendHttpHeader200(conn);
    // print list of available projects
    std::list<std::pair<std::string, Role> > usersRoles;
    std::list<std::pair<std::string, std::string> > pList;
    if (!UserBase::isLocalUserInterface() && !u.superadmin) pList = u.getProjects();
    else {
        // for a superadmin, get the list of all the projects
        // or in the case of a local user interface
        std::list<std::string> allProjects = Database::getProjects();
        std::list<std::string>::iterator p;
        FOREACH(p, allProjects) {
            Role r = u.getRole(*p);
            pList.push_back(std::make_pair(*p ,roleToString(r)));
        }
    }

    enum RenderingFormat format = getFormat(conn);

    if (format == RENDERING_TEXT) RText::printProjectList(conn, pList);
    else if (format == RENDERING_CSV) RCsv::printProjectList(conn, pList);
    else if (format == X_SMIT) {
        // print the list of the projects (for cloning tool)
        mg_printf(conn, "Content-Type: text/directory\r\n\r\n");
        std::list<std::pair<std::string, std::string> >::iterator p;
        FOREACH(p, pList) {
            mg_printf(conn, "%s\n", Project::urlNameEncode(p->first).c_str());
        }
        mg_printf(conn, "public\n");


    } else {

        // get the list of users and roles for each project
        std::map<std::string, std::map<std::string, Role> > usersRolesByProject;
        std::list<std::pair<std::string, std::string> >::const_iterator p;
        FOREACH(p, pList) {
            std::map<std::string, Role> ur = UserBase::getUsersRolesOfProject(p->first);
            // remove current user from others
            ur.erase(u.username);
            usersRolesByProject[p->first] = ur;
        }

        std::list<User> allUsers;
        if (u.superadmin) {
            // get the list of all users
            allUsers = UserBase::getAllUsers();
        }

        ContextParameters ctx = ContextParameters(conn, u);
        RHtml::printPageProjectList(ctx, pList, usersRolesByProject, allUsers);
    }
}

void httpGetNewProject(struct mg_connection *conn, User u)
{
    if (! u.superadmin) return sendHttpHeader403(conn);

    Project p;
    // add by default 2 properties : status (open, closed) and owner (selectUser)
    PropertySpec pspec;
    ProjectConfig pconfig;
    pspec.name = "status";
    pspec.type = F_SELECT;
    pspec.selectOptions.push_back("open");
    pspec.selectOptions.push_back("closed");
    pconfig.properties.push_back(pspec);
    pspec.name = "owner";
    pspec.type = F_SELECT_USER;
    pconfig.properties.push_back(pspec);
    p.setConfig(pconfig);
    sendHttpHeader200(conn);
    ContextParameters ctx = ContextParameters(conn, u, p);
    RHtml::printProjectConfig(ctx);
}


void httpGetProjectConfig(struct mg_connection *conn, Project &p, User u)
{
    if (u.getRole(p.getName()) != ROLE_ADMIN && ! u.superadmin) return sendHttpHeader403(conn);

    sendHttpHeader200(conn);
    ContextParameters ctx = ContextParameters(conn, u, p);
    RHtml::printProjectConfig(ctx);
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

void httpPostProjectConfig(struct mg_connection *conn, Project &p, User u)
{
    enum Role r = u.getRole(p.getName());
    if (r != ROLE_ADMIN && ! u.superadmin) {
        sendHttpHeader403(conn);
        return;
    }

    std::string postData;

    const char *contentType = mg_get_header(conn, "Content-Type");

    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {
        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".

        int rc = readMgConn(conn, postData, 4096);
        if (rc < 0) {
            std::ostringstream s;
            sendHttpHeader413(conn, "You tried to upload too much data. Max is 4096 bytes.");
            return;
        }


        LOG_DEBUG("postData=%s", postData.c_str());
        // parse the posted data
        std::string propertyName;
        std::string tagName;
        std::string type;
        std::string label;
        std::string selectOptions;
        std::string projectName;
        std::string reverseAssociationName;
        std::string tagDisplay;
        ProjectConfig pc;
        std::list<std::list<std::string> > tokens;
        while (1) {
            std::string tokenPair = popToken(postData, '&');
            std::string key = popToken(tokenPair, '=');
            std::string value = urlDecode(tokenPair);

            if (key == "projectName") {
                projectName = value;

            } else if (key == "propertyName" || key == "tagName" || postData.empty()) {
                // process previous row
                if (!propertyName.empty()) {
                    // the previous row was a property

                    if (type.empty()) {
                        // case of reserved properties (id, ctime, mtime, etc.)
                        if (label != propertyName) {
                            std::list<std::string> line;
                            line.push_back("setPropertyLabel");
                            line.push_back(propertyName);
                            line.push_back(label);
                            tokens.push_back(line);
                        }
                    } else {
                        // case of regular properties

                        // check that it is not a reserved property
                        if (ProjectConfig::isReservedProperty(propertyName)) {
                            // error
                            std::string msg = "Cannot add reserved property: ";
                            msg += propertyName;
                            sendHttpHeader400(conn, msg.c_str());
                            return;

                        }

                        std::list<std::string> line;
                        line.push_back("addProperty");
                        line.push_back(propertyName);
                        if (label != propertyName) {
                            line.push_back("-label");
                            line.push_back(label);
                        }
                        line.push_back(type);
                        PropertyType ptype;
                        int r = strToPropertyType(type, ptype);
                        if (r == 0 && (ptype == F_SELECT || ptype == F_MULTISELECT) ) {
                            // add options
                            std::list<std::string> so = splitLinesAndTrimBlanks(selectOptions);
                            line.insert(line.end(), so.begin(), so.end());
                        } else if  (r== 0 && ptype == F_ASSOCIATION) {
                            line.push_back("-reverseLabel");
                            line.push_back(reverseAssociationName);
                        }
                        tokens.push_back(line);
                    }
                } else if (!tagName.empty()) {
                    // the previous row was a tag
                    std::list<std::string> line;
                    line.push_back("tag");
                    line.push_back(tagName);
                    if (!label.empty()) {
                        line.push_back("-label");
                        line.push_back(label);
                    }
                    if (tagDisplay == "on") line.push_back("-display");
                    tokens.push_back(line);
                }
                propertyName.clear();
                tagName.clear();
                if (key == "propertyName") propertyName = value;
                else if (key == "tagName") tagName = value;
                type.clear();
                label.clear();
                selectOptions.clear();
                tagDisplay.clear();

            } else if (key == "type") { type = value; trimBlanks(type); }
            else if (key == "label") { label = value; trimBlanks(label); }
            else if (key == "selectOptions") selectOptions = value;
            else if (key == "reverseAssociation") reverseAssociationName = value;
            else if (key == "tagDisplay") tagDisplay = value;
            else {
                LOG_ERROR("ProjectConfig: invalid posted parameter: '%s'", key.c_str());
            }
            if (postData.empty()) break; // leave the loop
        }

        Project *ptr;
        if (p.getName().empty()) {
            if (!u.superadmin) return sendHttpHeader403(conn);
            if (projectName.empty()) {
                sendHttpHeader400(conn, "Empty project name");
                return;
            }

            // request for creation of a new project
            Project *newProject = Database::createProject(projectName);
            if (!newProject) return sendHttpHeader500(conn, "Cannot create project");

            ptr = newProject;

        } else {
            if (p.getName() != projectName) {
                LOG_INFO("Renaming an existing project not supported at the moment (%s -> %s)",
                         p.getName().c_str(), projectName.c_str());
            }
            ptr = &p;
        }
        int r = ptr->modifyConfig(tokens);

        if (r == 0) {
            // success, redirect to
            std::string redirectUrl = "/" + ptr->getUrlName() + "/config";
            sendHttpRedirect(conn, redirectUrl.c_str(), 0);

        } else { // error
            LOG_ERROR("Cannot modify project config");
            sendHttpHeader500(conn, "Cannot modify project config");
        }
    }
}

void httpPostNewProject(struct mg_connection *conn, User u)
{
    if (! u.superadmin) return sendHttpHeader403(conn);

    Project p;
    return httpPostProjectConfig(conn, p, u);
}


void replaceUserMe(std::map<std::string, std::list<std::string> > &filters, const Project &p, const std::string &username)
{
    ProjectConfig pconfig = p.getConfig();
    std::map<std::string, std::list<std::string> >::iterator filter;
    FOREACH(filter, filters) {
        const PropertySpec *ps = pconfig.getPropertySpec(filter->first);
        if ( ps && (ps->type == F_SELECT_USER) ) {
            std::list<std::string>::iterator v;
            FOREACH(v, filter->second) {
                if ((*v) == K_ME) {
                    // replace with username
                    *v = username;
                }
            }
        }
    }
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

enum IssueNavigation { ISSUE_NEXT, ISSUE_PREVIOUS };
/** Get redirection URL to next or previous issue
  *
  * If next or previous does not exists, then redirect
  * to the plain view (remove the next= or previous= parameter)

  * @return
  *     a string containing the url to redirect to
  *     "" if the next or previous the redirection could not be done
  */
std::string getRedirectionToIssue(mg_connection *conn, const Project &p, std::vector<struct Issue*> issueList,
                    const std::string &issueId, IssueNavigation direction, const std::string &qs)
{
    // get current issue
    std::vector<struct Issue*>::const_iterator i;
    FOREACH(i, issueList) {
        if ((*i)->id == issueId) {
            break;
        }
    }

    if (direction == ISSUE_NEXT) {
        if (i != issueList.end()) i++; // get next

    } else {
        if (i != issueList.begin()) i--; // get previous
        else i = issueList.end();
    }

    std::string redirectUrl;
    if (i != issueList.end()) {
        // redirect
        redirectUrl = "/" + p.getUrlName() + "/issues/" + (*i)->id;

    } else {
        // no next nor previous issue.
        // redirect to the plain view without the next= nor previous= parameter
        // remove the next/previous redirections from the query string
        std::string newQueryString = removeParam(qs, QS_GOTO_NEXT);
        newQueryString = removeParam(newQueryString, QS_GOTO_PREVIOUS);
        redirectUrl = "/" + p.getUrlName() + "/issues/" + "?" + newQueryString;
    }
    return redirectUrl;
}

void httpIssuesAccrossProjects(struct mg_connection *conn, User u, const std::string &uri)
{
    if (uri != "issues") return sendHttpHeader404(conn);

    // get list of projects for that user
    std::list<std::pair<std::string, std::string> > projectsAndRoles = u.getProjects();

    // get query string parameters
    const struct mg_request_info *req = mg_get_request_info(conn);
    std::string q;
    if (req->query_string) q = req->query_string;
    PredefinedView v = PredefinedView::loadFromQueryString(q); // unamed view, used as handle on the viewing parameters

    std::map<std::string, std::vector<struct Issue*> > issues;

    // foreach project, get list of issues
    std::list<std::pair<std::string, std::string> >::const_iterator p;
    FOREACH(p, projectsAndRoles) {
        const std::string &project = p->first;

        Project *p = Database::Db.getProject(project);
        if (!p) continue;

        // replace user "me" if any...
        PredefinedView vcopy = v;
        replaceUserMe(vcopy.filterin, *p, u.username);
        replaceUserMe(vcopy.filterout, *p, u.username);
        if (vcopy.search == "me") vcopy.search = u.username;

        std::vector<struct Issue*> issueList = p->search(vcopy.search.c_str(),
                                                         vcopy.filterin, vcopy.filterout, vcopy.sort.c_str());
        issues[project] = issueList;
    }

    // get the colspec
    std::list<std::string> cols;
    std::list<std::string> allCols;
    if (v.colspec.size() > 0) {
        cols = parseColspec(v.colspec.c_str(), allCols);
    } else {
        // prevent having no columns, by forcing all of them
        cols = ProjectConfig::getReservedProperties();
    }
    enum RenderingFormat format = getFormat(conn);

    sendHttpHeader200(conn);

    if (format == RENDERING_TEXT) mg_printf(conn, "\r\n\r\nnot supported\r\n");
    else if (format == RENDERING_CSV) mg_printf(conn, "\r\n\r\nnot supported\r\n");
    else {
        ContextParameters ctx = ContextParameters(conn, u);
        std::list<std::string> filterinRaw = getParamListFromQueryString(q, "filterin");
        std::list<std::string> filteroutRaw = getParamListFromQueryString(q, "filterout");
        // it would be better for code maintainability to pass v.filterin/out
        ctx.filterin = filterinRaw;
        ctx.filterout = filteroutRaw;
        ctx.search = v.search;
        ctx.sort = v.sort;

        RHtml::printPageIssueAccrossProjects(ctx, issues, cols);
    }
    // display page

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

    std::string defaultView = getFirstParamFromQueryString(q, "defaultView");
    if (defaultView == "1") {
        // redirect
        std::string redirectUrl = "/" + p.getUrlName() + "/issues/";
        PredefinedView pv = p.getDefaultView();
        if (!pv.name.empty()) {
            redirectUrl += "?" + pv.generateQueryString();
        }
        sendHttpRedirect(conn, redirectUrl.c_str(), 0);
        return;
    }

    // should use loadViewFromQueryString (code maintainability improvement)
    PredefinedView v = PredefinedView::loadFromQueryString(q); // unamed view, used as handler on the viewing parameters

    // replace user "me" if any...
    replaceUserMe(v.filterin, p, u.username);
    replaceUserMe(v.filterout, p, u.username);
    if (v.search == "me") v.search = u.username;


    std::vector<struct Issue*> issueList = p.search(v.search.c_str(),
                                                    v.filterin, v.filterout, v.sort.c_str());

    // check for redirection to specific issue (used for previous/next)
    std::string next = getFirstParamFromQueryString(q, QS_GOTO_NEXT);
    std::string previous = getFirstParamFromQueryString(q, QS_GOTO_PREVIOUS);
    std::string redirectionUrl;
    if (next.size()) {
        redirectionUrl = getRedirectionToIssue(conn, p, issueList, next, ISSUE_NEXT, q);
    } else if (previous.size()) {
        redirectionUrl = getRedirectionToIssue(conn, p, issueList, previous, ISSUE_PREVIOUS, q);
    }
    if (redirectionUrl.size()) {
        // clean the query string from next=, previous=
        std::string newQs = removeParam(q, QS_GOTO_NEXT);
        newQs = removeParam(newQs, QS_GOTO_PREVIOUS);
        std::string cookie = getServerCookie(QS_ORIGIN_VIEW, newQs, -1);
        sendHttpRedirect(conn, redirectionUrl.c_str(), cookie.c_str());
        return;
    }


    std::string full = getFirstParamFromQueryString(q, "full"); // full-contents indicator

    // get the colspec
    std::list<std::string> cols;
    std::list<std::string> allCols = p.getConfig().getPropertiesNames();
    if (v.colspec.size() > 0) {
        cols = parseColspec(v.colspec.c_str(), allCols);
    } else {
        // prevent having no columns, by forcing all of them
        cols = allCols;
    }
    enum RenderingFormat format = getFormat(conn);

    sendHttpHeader200(conn);

    if (format == RENDERING_TEXT) RText::printIssueList(conn, issueList, cols);
    else if (format == RENDERING_CSV) RCsv::printIssueList(conn, issueList, cols);
    else {
        ContextParameters ctx = ContextParameters(conn, u, p);
        std::list<std::string> filterinRaw = getParamListFromQueryString(q, "filterin");
        std::list<std::string> filteroutRaw = getParamListFromQueryString(q, "filterout");
        // it would be better for code maintainability to pass v.filterin/out
        ctx.filterin = filterinRaw;
        ctx.filterout = filteroutRaw;
        ctx.search = v.search;
        ctx.sort = v.sort;

        if (full == "1") {
            RHtml::printPageIssuesFullContents(ctx, issueList);
        } else {
            sendCookie(conn, QS_ORIGIN_VIEW, q);
            RHtml::printPageIssueList(ctx, issueList, cols);
        }
    }
}

void httpGetProject(struct mg_connection *conn, Project &p, User u)
{
    // redirect to list of issues
    std::string url = "/";
    url += p.getUrlName() + "/issues?defaultView=1";
    sendHttpRedirect(conn, url.c_str(), 0);
}


void httpGetNewIssueForm(struct mg_connection *conn, Project &p, User u)
{
    enum Role role = u.getRole(p.getName());
    if (role != ROLE_RW && role != ROLE_ADMIN) {
        sendHttpHeader403(conn);
        return;
    }

    sendHttpHeader200(conn);

    ContextParameters ctx = ContextParameters(conn, u, p);

    // only HTML format is needed
    RHtml::printPageNewIssue(ctx);
}

void httpGetView(struct mg_connection *conn, Project &p, const std::string &view, User u)
{
    LOG_FUNC();
    sendHttpHeader200(conn);

    if (view.empty()) {
        // print the list of all views
        ContextParameters ctx = ContextParameters(conn, u, p);
        RHtml::printPageListOfViews(ctx);

    } else {
        // print the form of the given view
        std::string viewName = urlDecode(view);
        PredefinedView pv = p.getPredefinedView(viewName);

        if (pv.name.empty()) {
            // in this case (unnamed view, ie: advanced search)
            // handle the optional origin view
            std::string originView = getFromCookie(conn, QS_ORIGIN_VIEW);
            if (originView.size()) {
                // build a view from QS_ORIGIN_VIEW
                pv = PredefinedView::loadFromQueryString(originView);
            }
        }
        ContextParameters ctx = ContextParameters(conn, u, p);
        RHtml::printPageView(ctx, pv);
    }
}

/** Handle the POST of a view
  *
  * All user can post these as an advanced search (with no name).
  * But only admin users can post predefined views (with a name).
  */
void httpPostView(struct mg_connection *conn, Project &p, const std::string &name, User u)
{
    LOG_FUNC();

    std::string postData;
    const char *contentType = mg_get_header(conn, "Content-Type");
    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {
        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".
        int rc = readMgConn(conn, postData, MAX_SIZE_UPLOAD);
        if (rc < 0) {
            sendHttpHeader413(conn, "You tried to upload too much data. Max is 10 MB.");
            return;
        }

    }
    //LOG_DEBUG("postData=%s\n<br>", postData.c_str());

    std::string deleteMark = getFirstParamFromQueryString(postData, "delete");
    enum Role role = u.getRole(p.getName());
    if (deleteMark == "1") {
        if (role != ROLE_ADMIN && !u.superadmin) {
            sendHttpHeader403(conn);
            return;
        } else {
            // delete the view
            p.deletePredefinedView(name);
            std::string redirectUrl = "/" + p.getUrlName() + "/issues/";
            sendHttpRedirect(conn, redirectUrl.c_str(), 0);
        }
    }

    // parse the parameters
    PredefinedView pv;

    // parse the filterin. Eg:
    // postData=name=Test+View&search=toto+tutu&filterin=status&filter_value=closed&
    // filterin=status&filter_value=open&filterout=target_version&filter_value=v1.xxx&
    // colspec=id&colspec=ctime&colspec=mtime&colspec=summary&colspec=status&colspec=target_version&
    // colspec=owner&sort_direction=Ascending&sort_property=id&default=on

    std::string sortDirection, sortProperty;
    std::string filterinPropname, filteroutPropname;
    const char *filterValue = 0; // pointer indicates if it has been encountered or not in the loop

    while (postData.size() > 0) {
        std::string tokenPair = popToken(postData, '&');
        std::string key = popToken(tokenPair, '=');
        std::string value = urlDecode(tokenPair);

        trimBlanks(key);
        trimBlanks(value);

        if (key == "name") pv.name = value;
        else if (key == "colspec" && !value.empty()) {
            if (! pv.colspec.empty()) pv.colspec += "+";
            pv.colspec += value;
        } else if (key == "search") pv.search = value;
        else if (key == "filterin") { filterinPropname = value; filterValue = 0; }
        else if (key == "filterout") { filteroutPropname = value; filterValue = 0; }
        else if (key == "filter_value") filterValue = value.c_str();
        else if (key == "sort_direction") sortDirection = value;
        else if (key == "sort_property") sortProperty = value;
        else if (key == "default" && value == "on") pv.isDefault = true;

        else continue; // ignore invalid keys

        if (sortDirection.empty()) sortProperty.clear();
        else if (!sortProperty.empty()) {
            pv.sort += PredefinedView::getDirectionSign(sortDirection);
            pv.sort += sortProperty;
            sortDirection.clear();
            sortProperty.clear();
        }

        if (!filterinPropname.empty() && filterValue) {
            pv.filterin[filterinPropname].push_back(filterValue);
            filterinPropname.clear();
            filterValue = 0;
        }
        if (!filteroutPropname.empty() && filterValue) {
            pv.filterout[filteroutPropname].push_back(filterValue);
            filteroutPropname.clear();
            filterValue = 0;
        }
    }

    if (pv.name.empty()) {
        // unnamed view
        if (role != ROLE_ADMIN && role != ROLE_RO && role != ROLE_RW && !u.superadmin) {
            sendHttpHeader403(conn);
            return;
        }
        // do nothing, just redirect

    } else { // named view
        if (role != ROLE_ADMIN && !u.superadmin) {
            sendHttpHeader403(conn);
            return;
        }
        // store the view
        int r = p.setPredefinedView(name, pv);
        if (r < 0) {
            LOG_ERROR("Cannot set predefined view");
            sendHttpHeader500(conn, "Cannot set predefined view");
            return;
        }
    }

    enum RenderingFormat format = getFormat(conn);
    if (RENDERING_TEXT == format || RENDERING_CSV == format) {
        sendHttpHeader200(conn);

    } else {
        // redirect to the result of the search
        std::string redirectUrl = "/" + p.getUrlName() + "/issues/?" + pv.generateQueryString();
        sendHttpRedirect(conn, redirectUrl.c_str(), 0);
    }
}

/** Handled the posting of a tag
  *
  * If the ref is not tagged, then this will put a tag on the entry.
  * If the ref is already tagged, then this will remove the tag of the entry.
  * @param ref
  *     The reference of the message: <issue>/<entry>/<tagid>
  *
  */
void httpPostTag(struct mg_connection *conn, Project &p, std::string &ref, User u)
{
    enum Role role = u.getRole(p.getName());
    if (role != ROLE_RW && role != ROLE_ADMIN) {
        sendHttpHeader403(conn);
        return;
    }

    std::string issueId = popToken(ref, '/');
    std::string entryId = popToken(ref, '/');
    std::string tagid = ref;

    int r = p.toggleTag(issueId, entryId, tagid);
    if (r == 0) {
        sendHttpHeader200(conn);
        mg_printf(conn, "\r\n");
    } else {
        sendHttpHeader500(conn, "cannot toggle tag");
    }
}

/** Reload a project from disk storage
  *
  * This encompasses the configuration and the entries.
  */
void httpReloadProject(struct mg_connection *conn, Project &p, User u)
{
    enum Role role = u.getRole(p.getName());
    if (role != ROLE_ADMIN) {
        sendHttpHeader403(conn);
        return;
    }
    int r = p.reload();

    // redirect to config page of the project
    if (r == 0) {
        // success, redirect to
        std::string redirectUrl = "/" + p.getUrlName() + "/config";
        sendHttpRedirect(conn, redirectUrl.c_str(), 0);

    } else { // error
        std::string msg = "Cannot reload project: ";
        msg += p.getName();
        LOG_ERROR("%s", msg.c_str());
        sendHttpHeader500(conn, msg.c_str());
    }
}


int httpGetIssue(struct mg_connection *conn, Project &p, const std::string &issueId, User u)
{
    LOG_DEBUG("httpGetIssue: project=%s, issue=%s", p.getName().c_str(), issueId.c_str());

    Issue issue;
    int r = p.get(issueId, issue);
    if (r < 0) {
        // issue not found or other error
        return 0; // let mongoose handle it

    } else {
        enum RenderingFormat format = getFormat(conn);

        sendHttpHeader200(conn);

        if (format == RENDERING_TEXT) RText::printIssue(conn, issue);
        else {
            ContextParameters ctx = ContextParameters(conn, u, p);
            ctx.originView = getFromCookie(conn, QS_ORIGIN_VIEW);
            // clear this cookie, so that getting any other issue
            // without coming from a view does not display get/next
            // (get/next use a redirection from a view, so the cookie will be set for these)
            mg_printf(conn, "Set-Cookie: %s=deleted; Path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT\r\n", QS_ORIGIN_VIEW);
            LOG_DEBUG("originView=%s", ctx.originView.c_str());
            RHtml::printPageIssue(ctx, issue);
        }
        return 1; // done
    }
}

/** Used for deleting an entry
  * @param details
  *     should be of the form: iid/eid/delete
  *
  * @return
  *     0, let Mongoose handle static file
  *     1, do not.
  */
void httpDeleteEntry(struct mg_connection *conn, Project &p, const std::string &issueId,
                    std::string details, User u)
{
    LOG_DEBUG("httpDeleteEntry: project=%s, issueId=%s, details=%s", p.getName().c_str(),
              issueId.c_str(), details.c_str());

    std::string entryId = popToken(details, '/');
    if (details != "delete") {
        sendHttpHeader404(conn);
        return;
    }

    enum Role role = u.getRole(p.getName());
    if (role != ROLE_RW && role != ROLE_ADMIN) {
        sendHttpHeader403(conn);
        return;
    }

    int r = p.deleteEntry(issueId, entryId, u.username);
    if (r < 0) {
        // failure
        LOG_INFO("deleteEntry returned %d", r);
        sendHttpHeader403(conn);

    } else {
        sendHttpHeader200(conn);
        mg_printf(conn, "\r\n"); // no contents
    }

    return;
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

void parseMultipartAndStoreUploadedFiles(const std::string &data, std::string boundary,
                                         std::map<std::string, std::list<std::string> > &vars,
                                         const std::string &tmpDirectory)
{
    const char *p = data.data();
    size_t len = data.size();
    const char *end = p + len;

    boundary = "--" + boundary; // prefix with --

    // normally, data starts with boundary
    if ((p+2<=end) && (0 == strncmp(p, "\r\n", 2)) ) p += 2;
    if ((p+boundary.size()<=end) && (0 == strncmp(p, boundary.c_str(), boundary.size())) ) {
        p += boundary.size();
    }

    boundary.insert(0, "\r\n"); // add CRLF in the prefix

    while (1) {
        if ((p+2<=end) &&  (0 == strncmp(p, "\r\n", 2)) ) p += 2; // skip CRLF
        // line expected:
        // Content-Disposition: form-data; name="summary"
        // or
        // Content-Disposition: form-data; name="file"; filename="accum.png"
        // Content-Type: image/png

        // get end of line
        const char *endOfLine = p;
        while ( (endOfLine+2<=end) && (0 != strncmp(endOfLine, "\r\n", 2)) ) endOfLine++;
        if (endOfLine+2>end) {
            LOG_ERROR("Malformed multipart (0). Abort.");
            return;
        }
        std::string line(p, endOfLine-p); // get the line
        LOG_DEBUG("line=%s", line.c_str());

        // get name
        const char *nameToken = "name=\"";
        size_t namePos = line.find(nameToken);
        if (namePos == std::string::npos) {
            LOG_ERROR("Malformed multipart. Missing name.");
            return;
        }
        std::string name = line.substr(namePos+strlen(nameToken));
        // remove ".*
        size_t quotePos = name.find('"');
        if (quotePos == std::string::npos) {
            LOG_ERROR("Malformed multipart. Missing quote after name.");
            return;
        }
        name = name.substr(0, quotePos);
        trimBlanks(name);

        // get filename (optional)
        // filename containing " (double-quote) is not supported
        const char *filenameToken = "filename=\"";
        size_t filenamePos = line.find(filenameToken);
        std::string filename;
        if (filenamePos != std::string::npos) {
            filename = line.substr(filenamePos+strlen(filenameToken));
            // remove ".*
            quotePos = filename.find('"');
            if (quotePos == std::string::npos) {
                LOG_ERROR("Malformed multipart. Missing quote after name.");
                return;
            }
            filename = filename.substr(0, quotePos);
        }

        p = endOfLine;
        if ((p+2<=end) &&  (0 == strncmp(p, "\r\n", 2)) ) p += 2; // skip CRLF

        // get contents
        // search the end
        const char *endOfPart = p;
        while (endOfPart <= end-boundary.size()) {
            if (0 == memcmp(endOfPart, boundary.data(), boundary.size())) {
                // boundary found
                break;
            }
            endOfPart++;
        }
        if (endOfPart > end-boundary.size()) {
            LOG_ERROR("Malformed multipart. Premature end.");
            return;
        }
        if (filenamePos == std::string::npos) {
            // regular property of the form
            std::string value(p, endOfPart-p);
            trimBlanks(value);
            vars[name].push_back(value);

            LOG_DEBUG("name=%s, value=%s", name.c_str(), value.c_str());

        } else {
            // uploaded file
            // skip line Content-Type
            endOfLine = p; // initialize pointer
            while ( (endOfLine+2<=end) && (0 != strncmp(endOfLine, "\r\n", 2)) ) endOfLine++;
            if (endOfLine+2>end) {
                LOG_ERROR("Malformed multipart (0). Abort.");
                return;
            }
            p = endOfLine;
            if ((p+2<=end) &&  (0 == strncmp(p, "\r\n", 2)) ) p += 2; // skip CRLF
            if ((p+2<=end) &&  (0 == strncmp(p, "\r\n", 2)) ) p += 2; // skip CRLF

            size_t size = endOfPart-p; // size of the file
            if (size) {
                // get the file extension
                size_t lastSlash = filename.find_last_of("\\/");
                if (lastSlash != std::string::npos) filename = filename.substr(lastSlash+1);

                std::string id = getSha1(p, size);
                std::string basename = id + "." + filename;

                LOG_DEBUG("New filename: %s", basename.c_str());

                // store to tmpDirectory
                std::string path = tmpDirectory;
                mg_mkdir(tmpDirectory.c_str(), S_IRUSR | S_IWUSR | S_IXUSR); // create dir if needed
                path += "/";
                path += basename;
                int r = writeToFile(path.c_str(), p, size);
                if (r < 0) {
                    LOG_ERROR("Could not store uploaded file.");
                    return;
                }

                vars[name].push_back(basename);
                LOG_DEBUG("name=%s, basename=%s, size=%zu", name.c_str(), basename.c_str(), size);

            } // else: empty file, ignore.
        }

        p = endOfPart;
        p += boundary.size();
        if (p >= end) {
            LOG_ERROR("Malformed multipart. (4).");
            return;
        }
        if ( (p+4<=end) && (0 == strncmp(p, "--\r\n", 4)) ) {
            // end of multipart
            break;
        }
    }
}

void parseQueryString(const std::string &queryString, std::map<std::string, std::list<std::string> > &vars)
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
                trimBlanks(value);
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
void httpPostEntry(struct mg_connection *conn, Project &pro, const std::string & issueId, User u)
{
    enum Role r = u.getRole(pro.getName());
    if (r != ROLE_RW && r != ROLE_ADMIN) {
        sendHttpHeader403(conn);
        return;
    }
    const char *multipart = "multipart/form-data";
    std::map<std::string, std::list<std::string> > vars;

    const char *contentType = mg_get_header(conn, "Content-Type");
    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {

        // this branch is obsolete. It was the old code without file-upload capability

        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".
        // multiselect is like: "tags=v4.1&tags=v5.0" (same var repeated)

        std::string postData;
        int rc = readMgConn(conn, postData, MAX_SIZE_UPLOAD);
        if (rc < 0) {
            sendHttpHeader413(conn, "You tried to upload too much data. Max is 10 MB.");
            return;
        }

        parseQueryString(postData, vars);

    } else if (0 == strncmp(multipart, contentType, strlen(multipart))) {
        //std::string x = request2string(conn);
        //mg_printf(conn, "%s", x.c_str());

        // extract the boundary
        const char *b = "boundary=";
        const char *p = mg_strcasestr(contentType, b);
        if (!p) {
            LOG_ERROR("Missing boundary in multipart form data");
            return;
        }
        p += strlen(b);
        std::string boundary = p;
        LOG_DEBUG("Boundary: %s", boundary.c_str());

        std::string postData;
        int rc = readMgConn(conn, postData, MAX_SIZE_UPLOAD);
        if (rc < 0) {
            sendHttpHeader413(conn, "You tried to upload too much data. Max is 10 MB.");
            return;
        }

        std::string tmpDir = pro.getPath() + "/tmp";
        parseMultipartAndStoreUploadedFiles(postData, boundary, vars, tmpDir);

    } else {
        // multipart/form-data
        LOG_ERROR("Content-Type '%s' not supported", contentType);
    }

    std::string id = issueId;
    if (id == "new") id = "";
    std::string entryId;
    int status = pro.addEntry(vars, id, entryId, u.username);
    if (status != 0) {
        // error
        sendHttpHeader500(conn, "Cannot add entry");

    } else {

#if !defined(_WIN32)
        // launch the trigger, if any
        // launch the trigger only if a new entry was actually created
        if (!entryId.empty()) Trigger::notifyEntry(pro, issueId, entryId);
#endif


        if (getFormat(conn) == RENDERING_HTML) {
            // HTTP redirect
            std::string redirectUrl = "/" + pro.getUrlName() + "/issues/" + id;
            sendHttpRedirect(conn, redirectUrl.c_str(), 0);
        } else {
            sendHttpHeader200(conn);
            mg_printf(conn, "\r\n");
            mg_printf(conn, "%s/%s\r\n", id.c_str(), entryId.c_str());
        }
    }

}

int log_message_handler(const struct mg_connection *conn, const char *msg)
{
    LOG_ERROR("[MG] %s", msg);
    return 1;
}



/** begin_request_handler is the main entry point of an incoming HTTP request
  *
  * Resources               Methods    Acces Granted     Description
  * -------------------------------------------------------------------------
  * /                       GET/POST   user              list of projects / management of projects (create, ...)
  * /public/...             GET        all               public pages, javascript, CSS, logo
  * /signin                 POST       all               sign-in
  * /users                             superadmin        management of users for all projects
  * /_                      GET/POST   superadmin        new project
  * /users/<user>           GET/POST   user, superadmin  management of a single user
  * /myp/config             GET/POST   admin             configuration of the project
  * /myp/views              GET/POST   admin             list predefined views / create new view
  * /myp/views/_            GET        admin             form for advanced search / new predefined view
  * /myp/views/xyz          GET/POST   admin             display / update / rename predefined view
  * /myp/views/xyz?delete=1 POST       admin             delete predefined view
  * /myp/issues             GET/POST   user              issues of the project / add new issue
  * /myp/issues/new         GET        user              page with a form for submitting new issue
  * /myp/issues/123         GET/POST   user              a particular issue: get all entries or add a new entry
  * /myp/issues/x/y/delete  POST       user              delete an entry y of issue x
  * /myp/tags/x/y           POST       user              tag / untag an entry
  * /myp/reload             POST       admin             reload project from disk storage
  * / * /issues               GET        user              issues of all projects
  * /any/other/file         GET        user              any existing file (in the repository)
  */

int begin_request_handler(struct mg_connection *conn)
{
    LOG_FUNC();

    std::string uri = mg_get_request_info(conn)->uri;
    std::string method = mg_get_request_info(conn)->request_method;
    LOG_DEBUG("uri=%s, method=%s", uri.c_str(), method.c_str());

    if (method != "GET" && method != "POST") return sendHttpHeader400(conn, "invalid method");

    std::string resource = popToken(uri, '/');

    // public access to /public and /sm
    if (resource == "public") {
        if ( (method == "GET") && (getFormat(conn) == X_SMIT) ) return httpGetFile(conn);
        if (method == "GET") return 0; // let mongoose handle it
        else return sendHttpHeader400(conn, "invalid method");
    }
    if (resource == "sm") {
        if (method == "GET") return httpGetSm(conn, uri);
        else return sendHttpHeader400(conn, "invalid method");
    }

    // check access rights
    std::string sessionId = getFromCookie(conn, SESSID);
    User user = SessionBase::getLoggedInUser(sessionId);
    // if username is empty, then no access is granted (only public pages will be available)

    bool handled = true; // by default, do not let Mongoose handle the request

    if ( (resource == "signin") && (method == "POST") ) httpPostSignin(conn);
    else if ( (resource == "signin") && (method == "GET") ) sendHttpRedirect(conn, "/", 0);

    else if ( (resource == "signout") && (method == "POST") ) httpPostSignout(conn, sessionId);
    else if (user.username.size() == 0) {

        // user not logged in
        if (getFormat(conn) == RENDERING_HTML) redirectToSignin(conn, 0);
        else handleUnauthorizedAccess(conn, resource);
    }
    else if ( (resource == "") && (method == "GET") ) httpGetRoot(conn, user);
    else if ( (resource == "") && (method == "POST") ) httpPostRoot(conn, user);
    else if ( (resource == "users") && (method == "GET") ) httpGetUsers(conn, user, uri);
    else if ( (resource == "users") && (method == "POST") ) httpPostUsers(conn, user, uri);
    else if ( (resource == "_") && (method == "GET") ) httpGetNewProject(conn, user);
    else if ( (resource == "_") && (method == "POST") ) httpPostNewProject(conn, user);
    else if ( (resource == "*") && (method == "GET") ) httpIssuesAccrossProjects(conn, user, uri);
    else {
        // check if it is a valid project resource such as /myp/issues, /myp/users, /myp/config
        std::string projectUrl = resource;
        std::string project = Project::urlNameDecode(projectUrl);

        // check if user has at least read access
        enum Role r = user.getRole(project);
        if (r != ROLE_ADMIN && r != ROLE_RW && r != ROLE_RO && ! user.superadmin) {
            // no access granted for this user to this project
            handleUnauthorizedAccess(conn, resource);

        } else {
            // access granted to ressources inside a project

            if (getFormat(conn) == X_SMIT && method == "GET") return httpGetFile(conn); // used for cloning

            Project *p = Database::Db.getProject(project);
            LOG_DEBUG("project %s, %p", project.c_str(), p);
            if (p) {
                bool isdir = false;
                if (!uri.empty() && uri[uri.size()-1] == '/') isdir = true;

                resource = popToken(uri, '/');
                LOG_DEBUG("resource=%s", resource.c_str());
                if      ( resource.empty()       && (method == "GET") ) httpGetProject(conn, *p, user);
                else if ( (resource == "issues") && (method == "GET") && uri.empty() ) httpGetListOfIssues(conn, *p, user);
                else if ( (resource == "issues") && (method == "POST") ) {
                    std::string issueId = popToken(uri, '/');
                    if (uri.empty()) httpPostEntry(conn, *p, issueId, user);
                    else httpDeleteEntry(conn, *p, issueId, uri, user);

                } else if ( (resource == "issues") && (uri == "new") && (method == "GET") ) httpGetNewIssueForm(conn, *p, user);
                else if ( (resource == "issues") && (method == "GET") ) return httpGetIssue(conn, *p, uri, user);
                else if ( (resource == "config") && (method == "GET") ) httpGetProjectConfig(conn, *p, user);
                else if ( (resource == "config") && (method == "POST") ) httpPostProjectConfig(conn, *p, user);
                else if ( (resource == "views") && (method == "GET") && !isdir && uri.empty()) sendHttpRedirect(conn, "/" + projectUrl + "/views/", 0);
                else if ( (resource == "views") && (method == "GET")) httpGetView(conn, *p, uri, user);
                else if ( (resource == "views") && (method == "POST") ) httpPostView(conn, *p, uri, user);
                else if ( (resource == "tags") && (method == "POST") ) httpPostTag(conn, *p, uri, user);
                else if ( (resource == "reload") && (method == "POST") ) httpReloadProject(conn, *p, user);
                else handled = false;

            } else handled = false; // the resource is not a project
        }
    }

    if (handled) return 1; // handled here. Do not let Mongoose handle the request
    else return 0; // let Mongoose handle the request
}

