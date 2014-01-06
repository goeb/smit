/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>


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


std::string exeFile; // path to the executable (used for extracting embedded files)
#define K_ME "me"

std::string readMgConn(struct mg_connection *conn, size_t maxSize)
{
    std::string postData;
    const int SIZ = 4096;
    char postFragment[SIZ+1];
    int n; // number of bytes read

    while ( (n = mg_read(conn, postFragment, SIZ)) > 0) {
        LOG_DEBUG("postFragment size=%d", n);
        if (postData.size() > maxSize) {
            // 10 MByte is too much. overflow. abort.
            LOG_ERROR("Too much POST data. Abort.");
            break;
        }
        postData.append(std::string(postFragment, n));
    }
    LOG_DEBUG("postData size=%u", postData.size());

    return postData;
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

    std::string postData = readMgConn(conn, 10*1024*1024);

    return std::string(content) + postData;

}

void sendHttpHeader200(struct mg_connection *conn)
{
    mg_printf(conn, "HTTP/1.0 200 OK\r\n");
}

void sendHttpHeader400(struct mg_connection *conn, const char *msg)
{
    mg_printf(conn, "HTTP/1.0 400 Bad Request\r\n\r\n");
    mg_printf(conn, "400 Bad Request\r\n");
    mg_printf(conn, "%s\r\n", msg);
}
void sendHttpHeader403(struct mg_connection *conn)
{
    mg_printf(conn, "HTTP/1.1 403 Forbidden\r\n\r\n");
    mg_printf(conn, "403 Forbidden\r\n");
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
    std::string q = queryString;
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

enum RenderingFormat { RENDERING_HTML, RENDERING_TEXT, RENDERING_CSV };
enum RenderingFormat getFormat(struct mg_connection *conn)
{
    const struct mg_request_info *req = mg_get_request_info(conn);
    std::string q;
    if (req->query_string) q = req->query_string;
    std::string format = getFirstParamFromQueryString(q, "format");

    if (format == "html" || format.empty()) return RENDERING_HTML;
    else if (format == "csv") return RENDERING_CSV;
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
        if (format == RENDERING_TEXT || format == RENDERING_CSV) {
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

        if (redirect.empty()) redirect = "/";
        setCookieAndRedirect(conn, "sessid", sessionId.c_str(), redirect.c_str());

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

/** Get a SM embedded file
  */
int httpGetSm(struct mg_connection *conn, const std::string &file)
{
    int r; // return 0 to let mongoose handle static file, 1 otherwise
    FILE *f = cpioOpenArchive(exeFile.c_str());
    if (!f) {
        sendHttpHeader500(conn, "Cannot open CPIO archive");
        return 1;
    }
    std::string internalFile = "sm/" + file;
    r = cpioGetFile(f, internalFile.c_str());
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
        while (remainingBytes > 0) {
            uint32_t nToRead = remainingBytes;
            if (nToRead > BS) nToRead = BS;
            size_t n = fread(buffer, 1, nToRead, f);
            mg_write(conn, buffer, n);

            if (n != nToRead) break; // error
            if (feof(f)) break; // error probably

            remainingBytes -= n;
        }
        r = 1;
    } else {
        r = 0;
    }

    fclose(f);


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

    // parse the posted parameters
    const char *contentType = mg_get_header(conn, "Content-Type");

    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {
        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".

        std::string postData = readMgConn(conn, 4096);

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

        if (newUserConfig.username.empty() || newUserConfig.username == "_") {
            LOG_INFO("Ignore user parameters as username is empty or '_'");
            return sendHttpHeader400(conn, "Invalid user name");
        }

        int r = 0;

        if (passwd1 != passwd2) {
            LOG_INFO("passwd1 (%s) != passwd2 (%s)", passwd1.c_str(), passwd2.c_str());
            sendHttpHeader400(conn, "passwords 1 and 2 do not match");
            return;

        } else if (!signedInUser.superadmin) {
            // if signedInUser is not superadmin, only password is updated

            newUserConfig.username = username; // used below for redirection
            r = UserBase::updatePassword(username, passwd1);
            if (r != 0) LOG_ERROR("Cannot update password of user '%s'", username.c_str());
            else LOG_INFO("Password updated for user '%s'", username.c_str());

        } else {
            // superadmin: update all parameters of the user's configuration
            if (!passwd1.empty()) newUserConfig.setPasswd(passwd1);

            if (username == "_") {
                r = UserBase::addUser(newUserConfig);
            } else {
                r = UserBase::updateUser(username, newUserConfig);
            }
            if (r != 0) LOG_ERROR("Cannot update user '%s'", username.c_str());
            else if (newUserConfig.username != username) {
                LOG_INFO("User '%s' renamed '%s'", username.c_str(), newUserConfig.username.c_str());
            } else LOG_INFO("Parameters updated for user '%s'", username.c_str());
        }

        if (r != 0) {
            sendHttpHeader500(conn, "could not update user's parameters");

        } else {
            // ok, redirect
            std::string redirectUrl = "/users/" + urlEncode(newUserConfig.username);
            sendHttpRedirect(conn, redirectUrl.c_str(), 0);
        }

    } else {
        LOG_ERROR("Bad contentType: %s", contentType);
    }
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
    std::list<std::pair<std::string, Role> > usersRoles;
    std::list<std::pair<std::string, std::string> > pList;
    if (!u.superadmin) pList = u.getProjects();
    else {
        // for a superadmin, get the list of all the projects
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
    else {

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
    pconfig.properties[pspec.name] = pspec;
    pspec.name = "owner";
    pspec.type = F_SELECT_USER;
    pconfig.properties[pspec.name] = pspec;
    pconfig.orderedProperties.push_back("status");
    pconfig.orderedProperties.push_back("owner");
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

        postData = readMgConn(conn, 4096);

        LOG_DEBUG("postData=%s", postData.c_str());
        // parse the posted data
        std::string propertyName;
        std::string type;
        std::string label;
        std::string selectOptions;
        std::string projectName;
        ProjectConfig pc;
        std::list<std::list<std::string> > tokens;
        while (1) {
            std::string tokenPair = popToken(postData, '&');
            std::string key = popToken(tokenPair, '=');
            std::string value = urlDecode(tokenPair);

            if (key == "projectName") {
                projectName = value;

            } else if (key == "propertyName" || postData.empty()) {
                // process previous row
                if (!propertyName.empty()) {

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
                        }
                        tokens.push_back(line);
                    }
                }
                propertyName = value;
                type.clear();
                label.clear();
                selectOptions.clear();
            } else if (key == "type") { type = value; trimBlanks(type); }
            else if (key == "label") { label = value; trimBlanks(label); }
            else if (key == "selectOptions") selectOptions = value;
            else {
                LOG_ERROR("ProjectConfig: invalid posted parameter: '%s'", key.c_str());
            }
            if (postData.empty()) break; // leave the loop
        }

        Project *ptr;
        if (p.getName().empty()) {
            if (!u.superadmin) return sendHttpHeader403(conn);
            if (projectName.empty()) return sendHttpHeader400(conn, "Empty project name");

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
    std::map<std::string, PropertySpec> propertiesSpec = pconfig.properties;
    std::map<std::string, std::list<std::string> >::iterator filter;
    FOREACH(filter, filters) {
        std::map<std::string, PropertySpec>::const_iterator ps = propertiesSpec.find(filter->first);

        if ( (ps != propertiesSpec.end()) && (ps->second.type == F_SELECT_USER) ) {
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
        PredefinedView pv = p.getDefaultView();
        if (!pv.name.empty()) {
            std::string redirectUrl = "/" + p.getUrlName() + "/issues/?" + pv.generateQueryString();
            sendHttpRedirect(conn, redirectUrl.c_str(), 0);
            return;
        }
    }

    std::list<std::string> filterinRaw = getParamListFromQueryString(q, "filterin");
    std::list<std::string> filteroutRaw = getParamListFromQueryString(q, "filterout");
    std::map<std::string, std::list<std::string> > filterIn = parseFilter(filterinRaw);
    std::map<std::string, std::list<std::string> > filterOut = parseFilter(filteroutRaw);
    std::string fulltextSearch = getFirstParamFromQueryString(q, "search");
    std::string sorting = getFirstParamFromQueryString(q, "sort");

    // replace user "me" if any...
    replaceUserMe(filterIn, p, u.username);
    replaceUserMe(filterOut, p, u.username);


    std::vector<struct Issue*> issueList = p.search(fulltextSearch.c_str(), filterIn, filterOut, sorting.c_str());

    std::string full = getFirstParamFromQueryString(q, "full"); // full-contents indicator


    std::string colspec = getFirstParamFromQueryString(q, "colspec");
    std::list<std::string> cols;
    std::list<std::string> allCols = p.getPropertiesNames();

    if (colspec.size() > 0) {
        cols = parseColspec(colspec.c_str(), allCols);
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
        ctx.filterin = filterinRaw;
        ctx.filterout = filteroutRaw;
        ctx.search = fulltextSearch;
        ctx.sort = sorting;

        if (full == "1") {
            RHtml::printPageIssuesFullContents(ctx, issueList);
        } else {
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
        postData = readMgConn(conn, 10*1024*1024);
    }
    LOG_DEBUG("postData=%s\n<br>", postData.c_str());

    std::string deleteMark = getFirstParamFromQueryString(postData, "delete");
    enum Role role = u.getRole(p.getName());
    if (deleteMark == "1") {
        if (role != ROLE_ADMIN) {
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
        if (role != ROLE_ADMIN && role != ROLE_RO && role != ROLE_RW) {
            sendHttpHeader403(conn);
            return;
        }
        // do nothing, just redirect

    } else { // named view
        if (role != ROLE_ADMIN) {
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


int httpGetIssue(struct mg_connection *conn, Project &p, const std::string &issueId, User u)
{
    LOG_DEBUG("httpGetIssue: project=%s, issue=%s", p.getName().c_str(), issueId.c_str());

    Issue issue;
    std::list<Entry*> entries;
    int r = p.get(issueId.c_str(), issue);
    if (r < 0) {
        // issue not found or other error
        return 0; // let mongoose handle it

    } else {
        enum RenderingFormat format = getFormat(conn);

        sendHttpHeader200(conn);

        if (format == RENDERING_TEXT) RText::printIssue(conn, issue);
        else {
            ContextParameters ctx = ContextParameters(conn, u, p);
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

                std::string id = getBase64Id((uint8_t*)p, size);
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
                LOG_DEBUG("name=%s, basename=%s, size=%u", name.c_str(), basename.c_str(), size);

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


#define MAX_SIZE_UPLOAD (10*1024*1024)

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

        // this branch is obsolete. It was the old code before file-upload capability

        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".
        // multiselect is like: "tags=v4.1&tags=v5.0" (same var repeated)

        std::string postData = readMgConn(conn, MAX_SIZE_UPLOAD);
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

        std::string postData = readMgConn(conn, MAX_SIZE_UPLOAD);
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


/** begin_request_handler is the main entry point of an incoming HTTP request
  *
  * Resources               Methods    Acces Granted     Description
  * -------------------------------------------------------------------------
  * /                       GET/POST   user              list of projects / management of projects (create, ...)
  * /public/...             GET        all               public pages, javascript, CSS, logo
  * /signin                 POST       all               sign-in
  * /users                             superadmin        management of users for all projects
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
  * /any/other/file         GET        user              any existing file (relatively to the repository)
  */

int begin_request_handler(struct mg_connection *conn)
{
    LOG_FUNC();

    bool handled = true;
    std::string uri = mg_get_request_info(conn)->uri;
    std::string method = mg_get_request_info(conn)->request_method;
    LOG_DEBUG("uri=%s, method=%s", uri.c_str(), method.c_str());

    std::string resource = popToken(uri, '/');
    LOG_DEBUG("resource=%s, method=%s", resource.c_str(), method.c_str());

    if ((resource == "public") && (method == "GET")) return 0; // let mongoose handle the file request directly
    if ((resource == "sm") && (method == "GET")) return httpGetSm(conn, uri);

    // check acces rights
    std::string sessionId = getSessionIdFromCookie(conn);
    LOG_DEBUG("session id: %s", sessionId.c_str());
    User user = SessionBase::getLoggedInUser(sessionId);
    // if username is empty, then no access is granted (only public pages will be available)

    LOG_DEBUG("User logged: %s", user.username.c_str());

    if ( (resource == "signin") && (method == "POST") ) httpPostSignin(conn);
    else if ( (resource == "signout") && (method == "POST") ) httpPostSignout(conn, sessionId);
    else if (user.username.size() == 0) {

        // user not logged in
        if (getFormat(conn) == RENDERING_HTML) redirectToSignin(conn, 0);
        else handleUnauthorizedAccess(conn, resource);

    }
    else if ( (resource == "signin") && (method == "GET") ) sendHttpRedirect(conn, "/", 0);
    else if ( (resource == "") && (method == "GET") ) httpGetRoot(conn, user);
    else if ( (resource == "") && (method == "POST") ) httpPostRoot(conn, user);
    else if ( (resource == "users") && (method == "GET") ) httpGetUsers(conn, user, uri);
    else if ( (resource == "users") && (method == "POST") ) httpPostUsers(conn, user, uri);
    else if ( (resource == "_") && (method == "GET") ) httpGetNewProject(conn, user);
    else if ( (resource == "_") && (method == "POST") ) httpPostNewProject(conn, user);
    else {
        // check if it is a valid project resource such as /myp/issues, /myp/users, /myp/config
        std::string projectUrl = resource;
        std::string project = Project::urlNameDecode(projectUrl);

        // check if user has at lest read access
        enum Role r = user.getRole(project);
        if (r != ROLE_ADMIN && r != ROLE_RW && r != ROLE_RO && ! user.superadmin) {
            // no access granted for this user to this project
            handleUnauthorizedAccess(conn, resource);

        } else {
            // ressources inside a project
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
                else handled = false;

            } else handled = false; // the resource is not a project
        }
    }
    if (handled) return 1;
    else return 0; // let Mongoose handle static file
}

