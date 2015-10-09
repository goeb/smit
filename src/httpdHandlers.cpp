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

#include "httpdHandlers.h"
#include "httpdUtils.h"
#include "db.h"
#include "logging.h"
#include "identifiers.h"
#include "renderingText.h"
#include "renderingHtml.h"
#include "renderingHtmlIssue.h"
#include "ContextParameters.h"
#include "renderingCsv.h"
#include "renderingJson.h"
#include "parseConfig.h"
#include "stringTools.h"
#include "session.h"
#include "global.h"
#include "mg_win32.h"
#include "cpio.h"
#include "Trigger.h"
#include "filesystem.h"
#include "restApi.h"
#include "embedcpio.h" // generated

#ifdef KERBEROS_ENABLED
  #include "AuthKrb5.h"
#endif

#ifdef LDAP_ENABLED
  #include "AuthLdap.h"
#endif


#define K_ME "me"
#define MAX_SIZE_UPLOAD (10*1024*1024)
#define COOKIE_ORIGIN_VIEW "view-"

void httpPostRoot(const RequestContext *req, User u)
{
}

int httpPostSignin(const RequestContext *request)
{
    LOG_FUNC();

    const char *contentType = getContentType(request);

    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {
        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".

        const int SIZ = 1024;
        char buffer[SIZ+1];
        char password[SIZ+1];
        int n; // number of bytes read
        n = request->read(buffer, SIZ);
        if (n == SIZ) {
            LOG_ERROR("Post data for signin too long. Abort request.");
            return sendHttpHeader400(request, "Post data for signin too long");
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
            return sendHttpHeader400(request, "Missing user name");
        }
        std::string username = buffer;

        // get the password
        r = mg_get_var(postData.c_str(), postData.size(),
                       "password", password, SIZ);

        if (r<0) {
            // error: empty, or too long, or not present
            LOG_DEBUG("Cannot get password. r=%d, postData=%s", r, postData.c_str());
            return sendHttpHeader400(request, "Missing password");
        }

        // check credentials
        std::string sessionId = SessionBase::requestSession(username, password);
        LOG_DIAG("User %s got sessid: %s", username.c_str(), sessionId.c_str());
        memset(password, 0xFF, SIZ+1); // erase password

        if (sessionId.size() == 0) {
            LOG_DEBUG("Authentication refused");
            sendHttpHeader403(request);
            return REQUEST_COMPLETED;
        }

        // Sign-in accepted

        std::string redirect;
        enum RenderingFormat format = getFormat(request);

        if (format == X_SMIT || format == RENDERING_TEXT) {
            std::string cookieSessid = getServerCookie(COOKIE_SESSID_PREFIX, sessionId, SESSION_DURATION);
            sendHttpHeader204(request, cookieSessid.c_str());
        } else {
            // HTML rendering
            // Get the redirection page
            r = mg_get_var(postData.c_str(), postData.size(),
                           "redirect", buffer, SIZ);

            if (r<0) {
                // error: empty, or too long, or not present
                LOG_DEBUG("Cannot get redirect. r=%d, postData=%s", r, postData.c_str());
                return sendHttpHeader400(request, "Cannot get redirection");
            }
            redirect = buffer;

            if (redirect.empty()) redirect = "/";
            std::string s = getServerCookie(COOKIE_SESSID_PREFIX, sessionId, SESSION_DURATION);
            sendHttpRedirect(request, redirect, s.c_str());
        }

    } else {
        LOG_ERROR("Unsupported Content-Type in httpPostSignin: %s", contentType);
        return sendHttpHeader400(request, "Unsupported Content-Type");
    }
    return REQUEST_COMPLETED;
}

void redirectToSignin(const RequestContext *request, const char *resource = 0)
{
    LOG_DIAG("redirectToSignin");
    sendHttpHeader200(request);

    // delete session cookie
    request->printf("%s\r\n", getDeletedCookieString(COOKIE_SESSID_PREFIX).c_str());

    // prepare the redirection parameter
    std::string url;
    if (!resource) {
        url = request->getUri();
        const char *qs = request->getQueryString();
        if (qs && strlen(qs)) url = url + '?' + qs;
        resource = url.c_str();
    }
    User noUser;
    ContextParameters ctx = ContextParameters(request, noUser);
    RHtml::printPageSignin(ctx, resource);
}

void httpPostSignout(const RequestContext *request, const std::string &sessionId)
{
    SessionBase::destroySession(sessionId);

    enum RenderingFormat format = getFormat(request);
    if (format == RENDERING_HTML) {
        redirectToSignin(request, "/");

    } else {
        // delete session cookie
        std::string cookieSessid = getDeletedCookieString(COOKIE_SESSID_PREFIX);
        sendHttpHeader204(request, cookieSessid.c_str());

    }
}


void handleGetStats(const RequestContext *request)
{
    sendHttpHeader200(request);
    request->printf("Content-Type: text/plain\r\n\r\n");
    request->printf("Uptime: %d days\r\n", (time(0)-HttpStats.startupTime)/86400);
    request->printf("HTTP GET:          %4d\r\n", HttpStats.httpCodes[H_GET]);
    request->printf("HTTP POST:         %4d\r\n", HttpStats.httpCodes[H_POST]);
    request->printf("HTTP other:        %4d\r\n", HttpStats.httpCodes[H_OTHER]);
    request->printf("HTTP Responses:\r\n");
    request->printf("HTTP 2xx: %4d\r\n", HttpStats.httpCodes[H_2XX]);
    request->printf("HTTP 400: %4d\r\n", HttpStats.httpCodes[H_400]);
    request->printf("HTTP 403: %4d\r\n", HttpStats.httpCodes[H_403]);
    request->printf("HTTP 413: %4d\r\n", HttpStats.httpCodes[H_413]);
    request->printf("HTTP 500: %4d\r\n", HttpStats.httpCodes[H_500]);
    int others = HttpStats.httpCodes[H_GET] + HttpStats.httpCodes[H_POST];
    others -= HttpStats.httpCodes[H_2XX];
    others -= HttpStats.httpCodes[H_400];
    others -= HttpStats.httpCodes[H_403];
    others -= HttpStats.httpCodes[H_413];
    others -= HttpStats.httpCodes[H_500];
    request->printf("Others:   %4d\r\n", others);
}

void handleMessagePreview(const RequestContext *request)
{
    LOG_FUNC();
    std::string q = request->getQueryString();
    std::string message = getFirstParamFromQueryString(q, "message");
    LOG_DEBUG("message=%s", message.c_str());
    message = RHtmlIssue::convertToRichText(htmlEscape(message));
    sendHttpHeader200(request);
    request->printf("Content-Type: text/html\r\n\r\n");
    request->printf("%s", message.c_str());
}

int httpPostSm(const RequestContext *req, const std::string &resource)
{
    LOG_INFO("httpPostSm: %s", resource.c_str());

    const char *multipart = "multipart/form-data";

    const char *contentType = getContentType(req);
    if (0 == strncmp(multipart, contentType, strlen(multipart))) {

        // extract the boundary
        const char *b = "boundary=";
        const char *p = mg_strcasestr(contentType, b);
        if (!p) {
            LOG_ERROR("Missing boundary in multipart form data");
            return sendHttpHeader400(req, "missing boundary");
        }
        p += strlen(b);
        std::string boundary = p;
        LOG_INFO("Boundary: %s", boundary.c_str());

        std::string postData;
        int rc = readMgreq(req, postData, MAX_SIZE_UPLOAD);
        if (rc < 0) {
            sendHttpHeader413(req, "You tried to upload too much data. Max is 10 MB.");
            return REQUEST_COMPLETED;
        }

        LOG_INFO("data=%s", postData.c_str());
        sendHttpHeader200(req);
        req->printf("Content-Type: text/plain\r\n\r\n");
        req->printf("xxxxxx");


    } else {
        // other Content-Type
        LOG_ERROR("Content-Type '%s' not supported", contentType);
        return sendHttpHeader400(req, "Bad Content-Type");
    }

    return REQUEST_COMPLETED;
}

/** Get a SM embedded file
  *
  * Embbeded files: smit.js, etc.
  * Virtual files: preview
  */
int httpGetSm(const RequestContext *request, const std::string &file)
{
    int r; // return 0 to let mongoose handle static file, 1 otherwise

    LOG_DEBUG("httpGetSm: %s", file.c_str());
    const char *virtualFilePreview = "preview";
    if (0 == strncmp(file.c_str(), virtualFilePreview, strlen(virtualFilePreview))) {
        handleMessagePreview(request);
        return REQUEST_COMPLETED;
    } else if (0 == strcmp(file.c_str(), "stat")) {
        handleGetStats(request);
        return REQUEST_COMPLETED;
    }

    // check if etag does match
    // the etag if the build time for /sm/* files.
    const char *inm = request->getHeader("If-None-Match");
    if (inm && 0 == strcmp(em_binary_etag, inm)) {
        return sendHttpHeader304(request);
    }

    std::string internalFile = "sm/" + file;
    const char *start;
    r = cpioGetFile(internalFile.c_str(), start);

    if (r >= 0) {
        int filesize = r;
        sendHttpHeader200(request);
        const char *mimeType = mg_get_builtin_mime_type(file.c_str());
        LOG_DEBUG("mime-type=%s, size=%d", mimeType, filesize);
        request->printf("ETag: %s\r\n", em_binary_etag);
        request->printf("Content-Type: %s\r\n\r\n", mimeType);

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

            request->write(buffer, nToRead);

            remainingBytes -= nToRead;
        }
    } else {
        sendHttpHeader403(request);
    }

    return REQUEST_COMPLETED;
}

void httpGetUsers(const RequestContext *req, const User &signedInUser)
{
    if (!signedInUser.superadmin) {
        sendHttpHeader403(req);
        return;
    }

    std::list<User> allUsers;
    // get the list of all users
    allUsers = UserBase::getAllUsers();

    // only HTML supported at the moment
    sendHttpHeader200(req);
    ContextParameters ctx = ContextParameters(req, signedInUser);
    RHtml::printPageUserList(ctx, allUsers);
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
void httpGetUser(const RequestContext *request, const User &signedInUser, const std::string &username)
{
    if (username.empty()) {
        return httpGetUsers(request, signedInUser);
    }

    ContextParameters ctx = ContextParameters(request, signedInUser);

    if (username == "_") {
        // display form for a new user
        // only a superadmin may do this
        if (!signedInUser.superadmin) {
            sendHttpHeader403(request);
        } else {
            sendHttpHeader200(request);
            RHtml::printPageUser(ctx, 0);
        }

    } else {
        // look for existing user
        User *editedUser = UserBase::getUser(username);

        if (!editedUser) {
            if (signedInUser.superadmin) sendHttpHeader404(request);
            else sendHttpHeader403(request);

        } else if (username == signedInUser.username || signedInUser.superadmin) {

            // handle an existing user
            // a user may only view his/her own user page

            sendHttpHeader200(request);

            enum RenderingFormat format = getFormat(request);
            if (format == X_SMIT || format == RENDERING_TEXT) {
                // print the permissions of the signed-in user
                request->printf("Content-Type: text/plain\r\n\r\n");
                request->printf("%s\n", editedUser->serializePermissions().c_str());

            } else {
                RHtml::printPageUser(ctx, editedUser);
            }

        } else sendHttpHeader403(request);
    }
}

/** Delete a user
  */
void httpDeleteUser(const RequestContext *request, User signedInUser, const std::string &username)
{
    if (!signedInUser.superadmin) {
        sendHttpHeader403(request);
        return;
    }

    LOG_INFO("User '%s' deleted by '%s'", username.c_str(), signedInUser.username.c_str());

    int r = UserBase::deleteUser(username);

    if (r != 0) {
        sendHttpHeader400(request, "Cannot delete user");

    } else {
        // ok, redirect
        sendHttpRedirect(request, "/" RSRC_USERS, 0);
    }
}

/** Hot reload of users (if hotreload=1)
  */
void httpPostUserEmpty(const RequestContext *req, const User &signedInUser)
{
    if (!signedInUser.superadmin) {
        sendHttpHeader403(req);
        return;
    }

    // check if a hot reload is requested

    // get the posted parameters

    const char *contentType = getContentType(req);
    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {
        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".

        std::string postData;
        int rc = readMgreq(req, postData, 4096);
        if (rc < 0) {
            sendHttpHeader413(req, "You tried to upload too much data. Max is 4096 bytes.");
            return;
        }

        std::string tokenPair = popToken(postData, '&');
        std::string key = popToken(tokenPair, '=');
        std::string value = urlDecode(tokenPair);

        if (key == "hotreload" && value == "1") {
            int r = UserBase::hotReload();
            if (r != 0) {
                sendHttpHeader500(req, "Hot reload failed");
                return;
            } else {
                // Ok, redirect
                sendHttpRedirect(req, "/users", 0);
                return;
            }
        }
    } else {
        LOG_INFO("httpPostUserEmpty: contentType '%s' rejected", contentType);
    }
    sendHttpHeader403(req);
    return;
}

/** Post configuration of a new or existing user
  *
  * Non-superadmin users may only post their password.
  */
void httpPostUser(const RequestContext *request, User signedInUser, const std::string &username)
{
    if (!signedInUser.superadmin && username != signedInUser.username) {
        sendHttpHeader403(request);
        return;
    }

    if (username.empty()) return httpPostUserEmpty(request, signedInUser);

    // get the posted parameters
    const char *contentType = getContentType(request);

    if (0 != strcmp("application/x-www-form-urlencoded", contentType)) {
        LOG_ERROR("Bad contentType: %s", contentType);
        return;
    }
    // application/x-www-form-urlencoded
    // post_data is "var1=val1&var2=val2...".

    std::string postData;
    int rc = readMgreq(request, postData, 4096);
    if (rc < 0) {
        sendHttpHeader413(request, "You tried to upload too much data. Max is 4096 bytes.");
        return;
    }

    User newUserConfig;
    std::string passwd1, passwd2;
    std::string projectWildcard, role;
    std::string authType = "sha1"; // default value
    std::string ldapUri, ldapDname;
    std::string krb5Primary, krb5Realm;

    while (postData.size() > 0) {
        std::string tokenPair = popToken(postData, '&');
        std::string key = popToken(tokenPair, '=');
        std::string value = urlDecode(tokenPair);

        if (key == "name") newUserConfig.username = value;
        else if (key == "sm_superadmin" && value == "on") newUserConfig.superadmin = true;
        else if (key == "sm_auth_type") authType = value;
        else if (key == "sm_passwd1") passwd1 = value;
        else if (key == "sm_passwd2") passwd2 = value;
        else if (key == "sm_ldap_uri") ldapUri = value;
        else if (key == "sm_ldap_dname") ldapDname = value;
        else if (key == "sm_krb5_primary") krb5Primary = value;
        else if (key == "sm_krb5_realm") krb5Realm = value;
        else if (key == "project_wildcard") projectWildcard = value;
        else if (key == "role") role = value;
        else {
            LOG_ERROR("httpPostUsers: unexpected parameter '%s'", key.c_str());
        }

        // look if the pair project/role is complete
        if (!projectWildcard.empty() && !role.empty()) {
            Role r = stringToRole(role);
            if (r != ROLE_NONE) newUserConfig.permissions[projectWildcard] = r;
            projectWildcard.clear();
            role.clear();
        }
    }

    // Check that the username given in the form is not empty.
    // Only superadmin has priviledge for modifying this.
    // For other users, the disabled field makes the username empty.
    if (signedInUser.superadmin) {
        if (newUserConfig.username.empty() || newUserConfig.username == "_") {
            LOG_INFO("Ignore user parameters as username is empty or '_'");
            sendHttpHeader400(request, "Invalid user name");
            return;
        }
    }

    int r = 0;
    std::string error;

    // check the parameters
    if (passwd1 != passwd2) {
        LOG_INFO("passwd1 (%s) != passwd2 (%s)", passwd1.c_str(), passwd2.c_str());
        sendHttpHeader400(request, "passwords 1 and 2 do not match");
        return;
    }


    LOG_INFO("authType=%s", authType.c_str()); // TODO remove this debug

    if (!signedInUser.superadmin) {
        // if signedInUser is not superadmin, only password is updated
        newUserConfig.username = username; // used below for redirection

        if (!passwd1.empty()) {
            r = UserBase::updatePassword(username, passwd1);
            if (r != 0) {
                LOG_ERROR("Cannot update password of user '%s'", username.c_str());
                error = "Cannot update password";
            } else LOG_INFO("Password of user '%s' updated by '%s'", username.c_str(),
                            signedInUser.username.c_str());
        } else {
            LOG_DEBUG("Password not changed (empty)");
        }

    } else {
        // superadmin: update all parameters of the user's configuration
        if (authType == "sha1") {
            if (!passwd1.empty()) newUserConfig.setPasswd(passwd1);

#ifdef LDAP_ENABLED
        } else if (authType == "ldap") {
            if (ldapUri.empty() || ldapDname.empty()) {
                sendHttpHeader400(request, "Missing parameter. Check the LDAP URI and Distinguished Name.");
                return;
            }
            newUserConfig.authHandler = new AuthLdap(newUserConfig.username, ldapUri, ldapDname);
#endif
#ifdef KERBEROS_ENABLED
        } else if (authType == "krb5") {
            if (krb5Realm.empty()) {
                sendHttpHeader400(request, "Empty parameter: Kerberos Realm.");
                return;
            }
            newUserConfig.authHandler = new AuthKrb5(newUserConfig.username, krb5Realm, krb5Primary);
#endif
        } else {
            // unsupported authentication type
            std::string msg = "Unsupported authentication type: " + authType;
            sendHttpHeader400(request, msg.c_str());
            return;
        }

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
        } else LOG_INFO("Parameters of user '%s' updated by '%s'", username.c_str(), signedInUser.username.c_str());
    }

    if (r != 0) {
        sendHttpHeader400(request, error.c_str());

    } else {
        // POST accepted and processed ok
        enum RenderingFormat format = getFormat(request);

        if (format == RENDERING_TEXT) {
            // No redirection
            sendHttpHeader204(request, 0);

        } else {
            // ok, redirect
            std::string redirectUrl = "/users/" + urlEncode(newUserConfig.username);
            sendHttpRedirect(request, redirectUrl.c_str(), 0);
        }
    }
}

int handleUnauthorizedAccess(const RequestContext *req, bool signedIn)
{
    if (!signedIn && getFormat(req) == RENDERING_HTML) redirectToSignin(req, 0);
    else sendHttpHeader403(req);

    return REQUEST_COMPLETED;
}

/** Serve a GET request to a file
  *
  * Access restriction must have been done before calling this function.
  * (this function does not verify access rights)
  */
int httpGetFile(const RequestContext *request)
{
    if (getFormat(request) != X_SMIT) return REQUEST_NOT_PROCESSED; // let mongoose handle it

    std::string uri = request->getUri();
    std::string dir = Database::Db.pathToRepository + uri; // uri contains a leading /

    DIR *d = openDir(dir.c_str());
    if (!d) return REQUEST_NOT_PROCESSED; // not a directory: let Mongoose handle it

    // send the directory contents
    // walk through the directory and print the entries
    sendHttpHeader200(request);
    request->printf("Content-Type: text/directory\r\n\r\n");

    std::string f;
    while ((f = getNextFile(d)) != "") request->printf("%s\n", f.c_str());

    closeDir(d);

    return REQUEST_COMPLETED; // the request is completely handled
}

void httpGetProjects(const RequestContext *req, User u)
{
    sendHttpHeader200(req);
    // print list of available projects
    std::list<std::pair<std::string, Role> > usersRoles;
    std::list<std::pair<std::string, std::string> > pList;
    if (!UserBase::isLocalUserInterface() && !u.superadmin) {
        // Get the list of the projects to which the user has permission
        pList = u.getProjects();

    } else {
        // Get the list of all projects
        // (case of a superadmin of a local user)
        std::list<std::string> allProjects = Database::getProjects();
        std::list<std::string>::iterator p;
        FOREACH(p, allProjects) {
            Role r = u.getRole(*p);
            pList.push_back(std::make_pair(*p ,roleToString(r)));
        }
    }

    enum RenderingFormat format = getFormat(req);

    if (format == RENDERING_TEXT) RText::printProjectList(req, pList);
    else if (format == RENDERING_CSV) RCsv::printProjectList(req, pList);
    else if (format == X_SMIT) {
        // print the list of the projects (for cloning tool)
        req->printf("Content-Type: text/directory\r\n\r\n");
        std::list<std::pair<std::string, std::string> >::iterator p;
        FOREACH(p, pList) {
           req->printf("%s\n", p->first.c_str());
        }
        req->printf("public\n");
        req->printf(PATH_REPO "\n");

    } else {

        // get the list of users and roles for each project
        std::map<std::string, std::map<Role, std::set<std::string> > > usersRolesByProject;
        std::list<std::pair<std::string, std::string> >::const_iterator p;
        FOREACH(p, pList) {
            std::map<Role, std::set<std::string> > ur = UserBase::getUsersByRole(p->first);
            usersRolesByProject[p->first] = ur;
        }

        ContextParameters ctx = ContextParameters(req, u);
        RHtml::printPageProjectList(ctx, pList, usersRolesByProject);
    }
}

void httpGetNewProject(const RequestContext *req, User u)
{
    if (! u.superadmin) return sendHttpHeader403(req);

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
    sendHttpHeader200(req);
    ContextParameters ctx = ContextParameters(req, u, p);
    RHtml::printProjectConfig(ctx);
}


void httpGetProjectConfig(const RequestContext *req, Project &p, User u)
{
    if (u.getRole(p.getName()) != ROLE_ADMIN && ! u.superadmin) return sendHttpHeader403(req);

    sendHttpHeader200(req);
    ContextParameters ctx = ContextParameters(req, u, p);
    RHtml::printProjectConfig(ctx);
}

void consolidatePropertyDescription(std::list<std::list<std::string> > &tokens, PropertySpec &pSpec)
{
    LOG_DEBUG("process propertyName=%s", pSpec.name.c_str());

    if (ProjectConfig::isReservedProperty(pSpec.name)) {
        // case of reserved properties (id, ctime, mtime, etc.)
        if (pSpec.label != pSpec.name) {
            std::list<std::string> line;
            line.push_back(KEY_SET_PROPERTY_LABEL);
            line.push_back(pSpec.name);
            line.push_back(pSpec.label);
            tokens.push_back(line);
        }

    } else {
        // case of regular properties
        std::list<std::string> line;
        line.push_back(KEY_ADD_PROPERTY);
        line.push_back(pSpec.name);
        if (pSpec.label != pSpec.name) {
            line.push_back("-label");
            line.push_back(pSpec.label);
        }
        line.push_back(propertyTypeToStr(pSpec.type));
        if (pSpec.type == F_SELECT || pSpec.type == F_MULTISELECT) {
            // add options
            line.insert(line.end(), pSpec.selectOptions.begin(), pSpec.selectOptions.end());

        } else if  (pSpec.type == F_ASSOCIATION) {
            line.push_back("-reverseLabel");
            line.push_back(pSpec.reverseLabel);
        }
        tokens.push_back(line);
    }

}
void consolidateTagDescription(std::list<std::list<std::string> > &tokens, const std::string &tagName,
                                      const std::string &label, const std::string &tagDisplay)
{
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

/** Parse the form parameters
  *
  * @param postData
  *    Eg: "projectName=apollo&propertyName=propx&type=text&propertyName=propy&type=text&tag=analysis"
  *    This data is modified in-place.
  *
  * @param[out] tokens
  * @param[out] projectName
  *
  * @return
  *    0, success
  *   -1, error
  */
void parsePostedProjectConfig(std::string &postData, std::list<std::list<std::string> > &tokens, std::string &projectName)
{
    // parse the posted data
    PropertySpec pSpec;
    std::string tagName;
    std::string tagLabel;
    std::string tagDisplay;

    while (1) {
        std::string tokenPair = popToken(postData, '&');
        std::string key = popToken(tokenPair, '=');
        std::string value = urlDecode(tokenPair);
        trimBlanks(value);

        LOG_DEBUG("key=%s, value=%s", key.c_str(), value.c_str());
        if (key == "projectName") {
            projectName = value;

        } else if (key == "type") {
            int r = strToPropertyType(value, pSpec.type);
            if (r != 0) {
                LOG_ERROR("Unknown property type: %s", value.c_str());
                pSpec.type = F_TEXT;
            }

        } else if (key == "label") {
            // the same key "label" is used for properties and tags
            pSpec.label = value;
            tagLabel = value;

        } else if (key == "selectOptions") {
            pSpec.selectOptions = splitLinesAndTrimBlanks(value);
        }
        else if (key == "reverseAssociation") pSpec.reverseLabel = value;
        else if (key == "tagDisplay") tagDisplay = value;
        else if (key == "propertyName" || key == "tagName") {

            // a starting property or tag description stops any other on-going
            // property description or tag description

            // commit previous propertyName if any
            if (!pSpec.name.empty()) consolidatePropertyDescription(tokens, pSpec);
            // commit previous tagName, if any
            if (!tagName.empty()) consolidateTagDescription(tokens, tagName, tagLabel, tagDisplay);

            // clear parameters
            pSpec.type = F_TEXT;
            pSpec.selectOptions.clear();
            pSpec.label.clear();
            tagName.clear();
            pSpec.name.clear();
            tagDisplay.clear();
            tagLabel.clear();

            if (key == "propertyName") pSpec.name = value; // start new property description
            else if (key == "tagName") tagName = value; // start new tag description

        } else {
            LOG_ERROR("ProjectConfig: invalid posted parameter: '%s'", key.c_str());
        }

        if (postData.empty()) {
            // process last item (property or tag)

            // commit previous propertyName if any
            if (!pSpec.name.empty()) consolidatePropertyDescription(tokens, pSpec);
            // commit previous tagName, if any
            if (!tagName.empty()) consolidateTagDescription(tokens, tagName, tagLabel, tagDisplay);

            break;
        }
    }
}

void httpPostProjectConfig(const RequestContext *req, Project &p, User u)
{
    enum Role role = u.getRole(p.getName());
    if (role != ROLE_ADMIN && ! u.superadmin) {
        sendHttpHeader403(req);
        return;
    }

    std::string postData;

    const char *contentType = getContentType(req);

    if (0 != strcmp("application/x-www-form-urlencoded", contentType)) {
        LOG_ERROR("httpPostProjectConfig: invalid content-type '%s'", contentType);
        return;
    }

    // application/x-www-form-urlencoded
    // post_data is "var1=val1&var2=val2...".

    int rc = readMgreq(req, postData, 4096);
    if (rc < 0) {
        sendHttpHeader413(req, "You tried to upload too much data. Max is 4096 bytes.");
        return;
    }

    LOG_DEBUG("postData=%s", postData.c_str());
    ProjectConfig pc;
    std::string projectName;
    std::list<std::list<std::string> > tokens;
    parsePostedProjectConfig(postData, tokens, projectName);

    Project *ptr;
    // check if project name is valid
    if (!ProjectConfig::isValidProjectName(projectName)) {
        sendHttpHeader400(req, "Invalid project name");
        return;
    }

    if (p.getName().empty()) {
        // request to create a new project
        if (!u.superadmin) return sendHttpHeader403(req);
        if (projectName.empty()) {
            sendHttpHeader400(req, "Empty project name");
            return;
        }

        // request for creation of a new project
        Project *newProject = Database::createProject(projectName);
        if (!newProject) return sendHttpHeader500(req, "Cannot create project");

        ptr = newProject;

        // recalculate permissions of existing users against this new project
        UserBase::computePermissions();

    } else {
        if (p.getName() != projectName) {
            LOG_INFO("Renaming an existing project not supported at the moment (%s -> %s)",
                     p.getName().c_str(), projectName.c_str());
        }
        ptr = &p;
    }
    int r = ptr->modifyConfig(tokens, u.username);

    if (r == 0) {
        enum RenderingFormat format = getFormat(req);
        if (format == RENDERING_HTML) {
            // success, redirect to
            std::string redirectUrl = "/" + ptr->getUrlName() + "/config";
            sendHttpRedirect(req, redirectUrl.c_str(), 0);
        } else {
            sendHttpHeader204(req, 0); // ok, no redirection
        }
    } else { // error
        LOG_ERROR("Cannot modify project config");
        sendHttpHeader500(req, "Cannot modify project config");
    }
}

void httpPostNewProject(const RequestContext *req, User u)
{
    if (! u.superadmin) return sendHttpHeader403(req);

    Project p;
    return httpPostProjectConfig(req, p, u);
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

enum IssueNavigation { ISSUE_NEXT, ISSUE_PREVIOUS };

/** Get redirection URL to next or previous issue
  *
  * If next or previous does not exists, then redirect
  * to the plain view (remove the next= or previous= parameter)

  * @return
  *     a string containing the url to redirect to
  *     "" if the next or previous the redirection could not be done
  */
std::string getRedirectionToIssue(const Project &p, std::vector<Issue> &issueList,
                    const std::string &issueId, IssueNavigation direction, const std::string &qs)
{
    // get current issue
    std::vector<Issue>::const_iterator i;
    FOREACH(i, issueList) {
        if (i->id == issueId) {
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
        redirectUrl = "/" + p.getUrlName() + "/issues/" + i->id;

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

void httpIssuesAccrossProjects(const RequestContext *req, User u, const std::string &uri)
{
    if (uri != "issues") return sendHttpHeader404(req);

    // get list of projects for that user
    std::list<std::pair<std::string, std::string> > projectsAndRoles = u.getProjects();

    // get query string parameters
    std::string q = req->getQueryString();
    PredefinedView v = PredefinedView::loadFromQueryString(q); // unamed view, used as handle on the viewing parameters

    std::vector<Issue> issues;

    // foreach project, get list of issues
    std::list<std::pair<std::string, std::string> >::const_iterator p;
    FOREACH(p, projectsAndRoles) {
        const std::string &project = p->first;

        const Project *p = Database::Db.getProject(project);
        if (!p) continue;

        // replace user "me" if any...
        PredefinedView vcopy = v;
        replaceUserMe(vcopy.filterin, *p, u.username);
        replaceUserMe(vcopy.filterout, *p, u.username);
        if (vcopy.search == "me") vcopy.search = u.username;

        // search, without sorting
        p->search(vcopy.search.c_str(), vcopy.filterin, vcopy.filterout, 0, issues);
    }

    // sort
    std::list<std::pair<bool, std::string> > sSpec = parseSortingSpec(v.sort.c_str());
    Issue::sort(issues, sSpec);

    // get the colspec
    std::list<std::string> cols;
    std::list<std::string> allCols;
    if (v.colspec.size() > 0) {
        cols = parseColspec(v.colspec.c_str(), allCols);
    } else {
        // prevent having no columns, by forcing all of them
        cols = ProjectConfig::getReservedProperties();
    }
    enum RenderingFormat format = getFormat(req);

    sendHttpHeader200(req);

    if (format == RENDERING_TEXT) req->printf("\r\n\r\nnot supported\r\n");
    else if (format == RENDERING_CSV) req->printf("\r\n\r\nnot supported\r\n");
    else {
        ContextParameters ctx = ContextParameters(req, u);
        ctx.filterin = v.filterin;
        ctx.filterout = v.filterout;
        ctx.search = v.search;
        ctx.sort = v.sort;

        RHtml::printPageIssueAccrossProjects(ctx, issues, cols);
    }
    // display page

}

/** Get some meta-data of the smit repository
  *
  * @param uri
  *     - if empty, return the list of allowed sub-directories
  *
  * This service is useful only for cloning (or pulling) a smit repository.
  *
  * Superadmin can clone all. Other users can read only:
  * - templates
  * - users/permissions (only the permissions of the signe-in user)
  *
  */
int httpGetSmitRepo(const RequestContext *req, User u, std::string uri)
{
    if (u.superadmin) return httpGetFile(req); // Superadmin gets all

    std::string subdir = popToken(uri, '/');

    if (subdir.empty()) {
        // Send the list of allowed sub-directories
        sendHttpHeader200(req);
        req->printf("Content-Type: text/directory\r\n\r\n");
        req->printf("%s\n", P_TEMPLATES);
        req->printf("%s\n", P_USERS);
        return REQUEST_COMPLETED;

    } else if (subdir == P_USERS && uri.empty()) {
        // Listing of the "users" directory
        sendHttpHeader200(req);
        req->printf("Content-Type: text/directory\r\n\r\n");
        req->printf("%s\n", P_PERMISSIONS);
        return REQUEST_COMPLETED;

    } else if (subdir == P_USERS && uri == P_PERMISSIONS) {
        // print the permissions of the signed-in user
        sendHttpHeader200(req);
        req->printf("Content-Type: text/plain\r\n\r\n");
        req->printf("%s\n", u.serializePermissions().c_str());
        return REQUEST_COMPLETED;

    } else if (subdir != P_TEMPLATES) {
        sendHttpHeader400(req, "");
        return REQUEST_COMPLETED;
    }

    // serve the template file
    return httpGetFile(req);
}


void httpCloneIssues(const RequestContext *req, const Project &p)
{
    const std::map<std::string, std::list<std::string> > filterIn;
    const std::map<std::string, std::list<std::string> > filterOut;

    sendHttpHeader200(req);
    req->printf("Content-Type: text/directory\r\n\r\n");

    // get all the issues, sorted by id
    std::vector<Issue> issues;
    p.search(0, filterIn, filterOut, "id", issues);
    std::vector<Issue>::const_iterator i;
    FOREACH(i, issues) {
        req->printf("%s\n", i->id.c_str());
    }
}

void httpSendIssueList(const RequestContext *req, const Project &p,
                       const User &u, const std::vector<Issue> &issueList)
{
    std::string q = req->getQueryString();
    PredefinedView v = PredefinedView::loadFromQueryString(q);

    // get the colspec
    std::list<std::string> cols;
    std::list<std::string> allCols = p.getConfig().getPropertiesNames();
    if (v.colspec.size() > 0) {
        cols = parseColspec(v.colspec.c_str(), allCols);
    } else {
        // prevent having no columns, by forcing all of them
        cols = allCols;
    }
    enum RenderingFormat format = getFormat(req);

    sendHttpHeader200(req);

    if (format == RENDERING_TEXT) RText::printIssueList(req, issueList, cols);
    else if (format == RENDERING_JSON) RJson::printIssueList(req, issueList, cols);
    else if (format == RENDERING_CSV) {

        std::string separator = getFirstParamFromQueryString(q, "sep");
        separator = urlDecode(separator);
        RCsv::printIssueList(req, issueList, cols, separator.c_str());
    } else {
        ContextParameters ctx = ContextParameters(req, u, p);
        ctx.filterin = v.filterin;
        ctx.filterout = v.filterout;
        ctx.search = v.search;
        ctx.sort = v.sort;

        std::string full = getFirstParamFromQueryString(q, "full"); // full-contents indicator

        if (full == "1") {
            RHtml::printPageIssuesFullContents(ctx, issueList);
        } else {
            sendCookie(req, COOKIE_ORIGIN_VIEW, q, COOKIE_VIEW_DURATION);
            RHtml::printPageIssueList(ctx, issueList, cols);
        }
    }
}

void httpGetListOfEntries(const RequestContext *req, const Project &p, User u)
{
    std::string q = req->getQueryString();
    PredefinedView v = PredefinedView::loadFromQueryString(q); // unamed view, used as handler on the viewing parameters

    std::vector<Entry> entries;
    p.searchEntries(v.sort.c_str(), entries, v.limit);

    sendHttpHeader200(req);

    // Only HTML supported at the moment

    ContextParameters ctx = ContextParameters(req, u, p);
    ctx.filterin = v.filterin;
    ctx.filterout = v.filterout;
    ctx.search = v.search;
    ctx.sort = v.sort;

    RHtml::printPageEntries(ctx, entries);
}

/** Get the list of issues at the moment indicated by the snapshot
  *
  * @param snapshot
  *     Seconds since 1 Jan 1970 (Epoch) UTC.
  *
  * When taking a snapshot, the following query-string parameters are ignored:
  *   - filterin
  *   - filterout
  *   - search
  *   - sort
  */
void httpGetListOfIssues(const RequestContext *req, const Project &p, User u, const std::string &snapshot)
{
    std::vector<Issue> issueList;
    std::map<std::string, std::list<std::string> > emptyFilter;

    p.search(0, emptyFilter, emptyFilter, 0, issueList);

    time_t datetime = atoi(snapshot.c_str());

    // for each returned issue:
    // - update the issue according to the snapshot datetime
    // - if the ctime is after the snapshot datetime, then remove the issue
    std::vector<Issue>::iterator i = issueList.begin();
    while (i != issueList.end()) {
        int n = i->makeSnapshot(datetime);
        if (n == 0) {
            // Issue has not entry before datetime. ie: not existing.
            // Remove it from the list.
            i = issueList.erase(i);
        } else {
            i++;
        }
    }

    httpSendIssueList(req, p, u, issueList);

}

void httpGetListOfIssues(const RequestContext *req, const Project &p, User u)
{
    if (getFormat(req) == X_SMIT) return httpCloneIssues(req, p); // used for cloning

    // get query string parameters:
    //     colspec    which fields are to be displayed in the table, and their order
    //     filter     select issues with fields of the given values
    //     sort       indicate sorting

    std::string q = req->getQueryString();

    std::string defaultView = getFirstParamFromQueryString(q, "defaultView");
    if (defaultView == "1") {
        // redirect
        std::string redirectUrl = "/" + p.getUrlName() + "/issues/";
        PredefinedView pv = p.getDefaultView();
        if (!pv.name.empty()) {
            redirectUrl += "?" + pv.generateQueryString();
        }
        sendHttpRedirect(req, redirectUrl.c_str(), 0);
        return;
    }

    std::string snapshot = getFirstParamFromQueryString(q, "snapshot");
    if (!snapshot.empty()) {
        httpGetListOfIssues(req, p, u, snapshot);
        return;
    }

    PredefinedView v = PredefinedView::loadFromQueryString(q); // unamed view, used as handler on the viewing parameters

    // replace user "me" if any...
    replaceUserMe(v.filterin, p, u.username);
    replaceUserMe(v.filterout, p, u.username);
    if (v.search == "me") v.search = u.username;

    std::vector<Issue> issueList;
    p.search(v.search.c_str(), v.filterin, v.filterout, v.sort.c_str(), issueList);

    // check for redirection to specific issue (used for previous/next)
    std::string next = getFirstParamFromQueryString(q, QS_GOTO_NEXT);
    std::string previous = getFirstParamFromQueryString(q, QS_GOTO_PREVIOUS);
    std::string redirectionUrl;
    if (next.size()) {
        redirectionUrl = getRedirectionToIssue(p, issueList, next, ISSUE_NEXT, q);
    } else if (previous.size()) {
        redirectionUrl = getRedirectionToIssue(p, issueList, previous, ISSUE_PREVIOUS, q);
    }
    if (redirectionUrl.size()) {
        // clean the query string from next=, previous=
        std::string newQs = removeParam(q, QS_GOTO_NEXT);
        newQs = removeParam(newQs, QS_GOTO_PREVIOUS);
        std::string cookie = getServerCookie(COOKIE_ORIGIN_VIEW, newQs, -1);
        sendHttpRedirect(req, redirectionUrl.c_str(), cookie.c_str());
        return;
    }

    httpSendIssueList(req, p, u, issueList);
}

void httpGetProject(const RequestContext *req, const Project &p, User u)
{

    if (getFormat(req) == X_SMIT) { // used for cloning
        sendHttpHeader200(req);
        req->printf("Content-Type: text/directory\r\n\r\n");

        DIR *d = openDir(p.getPath().c_str());
        std::string f;
        while ((f = getNextFile(d)) != "") req->printf("%s\n", f.c_str());

        closedir(d);
        return;
    }

    // case of HTML web client

    // redirect to list of issues
    std::string url = "/";
    url += p.getUrlName() + "/issues?defaultView=1";
    sendHttpRedirect(req, url.c_str(), 0);
}


void httpGetNewIssueForm(const RequestContext *req, Project &p, User u)
{
    enum Role role = u.getRole(p.getName());
    if (role != ROLE_RW && role != ROLE_ADMIN) {
        sendHttpHeader403(req);
        return;
    }

    sendHttpHeader200(req);

    ContextParameters ctx = ContextParameters(req, u, p);

    // only HTML format is needed
    RHtml::printPageNewIssue(ctx);
}

void httpGetView(const RequestContext *req, Project &p, const std::string &view, User u)
{
    LOG_FUNC();
    sendHttpHeader200(req);

    if (view.empty()) {
        // print the list of all views
        ContextParameters ctx = ContextParameters(req, u, p);
        RHtml::printPageListOfViews(ctx);

    } else {
        // print the form of the given view
        PredefinedView pv = p.getPredefinedView(view);

        if (pv.name.empty()) {
            // in this case (unknown or unnamed view, ie: advanced search)
            // handle the optional origin view
            std::string originView;
            int r = getFromCookie(req, COOKIE_ORIGIN_VIEW, originView);
            if (r == 0) {
                // build a view from origin view
                pv = PredefinedView::loadFromQueryString(originView);
            }
        }
        ContextParameters ctx = ContextParameters(req, u, p);
        RHtml::printPageView(ctx, pv);
    }
}

/** Get an object
  *
  * Read access is supposed to have already been granted.
  *
  * @param object
  *    Must be <id>/<filename>, where:
  *    - <id> is the identifier of the object
  *    - <filename> is the name of the file. The extension is used to determine the type.
  */
void httpGetObject(const RequestContext *req, Project &p, std::string object)
{
    std::string id = popToken(object, '/');
    std::string basemane = object;
    std::string realpath = p.getObjectsDir() + "/" + Object::getSubpath(id);
    LOG_DEBUG("httpGetObject: basename=%s, realpath=%s", basemane.c_str(), realpath.c_str());
    req->sendObject(basemane, realpath);
}

/** Get the HEAD of an object
  *
  * This is used by client pushers to know if a file exists.
  */
void httpGetHeadObject(const RequestContext *req, Project &p, std::string object)
{
    LOG_FUNC();
    std::string id = popToken(object, '/');
    std::string basemane = object;
    std::string realpath = p.getObjectsDir() + "/" + Object::getSubpath(id);

    if (fileExists(realpath)) {
        sendHttpHeader204(req, 0);

    } else {
        sendHttpHeader404(req);
    }
}

void httpGetStat(const RequestContext *req, Project &p, const User &u)
{
    sendHttpHeader200(req);
    ContextParameters ctx = ContextParameters(req, u, p);
    // only HTML format is supported
    RHtml::printPageStat(ctx, u);
}

/** Handle the pushing of a file
  *
  * If the file already exists, then do not overwrite it.
  */
void httpPushAttachedFile(const RequestContext *req, Project &p, const std::string &filename, User u)
{
    enum Role role = u.getRole(p.getName());
    if (role != ROLE_ADMIN && role != ROLE_RW) {
        sendHttpHeader403(req);
        return;
    }

    // store the file in a temporary location
    std::string tmpPath = p.getTmpDir() + '/' + filename;

    std::ofstream tmp(tmpPath.c_str(), std::ios_base::binary);

    // get the data
    const int SIZ = 4096;
    char postFragment[SIZ+1];
    size_t n; // number of bytes read
    int totalBytes = 0;

    while ( (n = req->read(postFragment, SIZ)) > 0) {
        LOG_DEBUG("postFragment size=%lu", L(n));
        tmp.write(postFragment, n);

        totalBytes += n;
        if (totalBytes > MAX_SIZE_UPLOAD) {
            LOG_ERROR("Pushed file too big: %s\n", filename.c_str());
            sendHttpHeader400(req, "Pushed file too big");
            return;
        }
    }
    tmp.close();

    int r = p.addFile(filename);
    if (r != 0) {
        // error, file not added in database
        unlink(tmpPath.c_str());

        if (r == -1) {
            sendHttpHeader400(req, "Bad file name (hash)");
            return;

        } else if (r == -2) return sendHttpHeader403(req);

        else return sendHttpHeader500(req, "cannot rename pushed file");
    }

    LOG_INFO("File pushed: (%s) %s", p.getName().c_str(), filename.c_str());
    sendHttpHeader201(req);
}

/** Handle the POST of a view
  *
  * All users can post these as an advanced search (with no name).
  * But only admin users can post predefined views (with a name).
  */
void httpPostView(const RequestContext *req, Project &p, const std::string &name, User u)
{
    LOG_FUNC();

    std::string postData;
    const char *contentType = getContentType(req);
    if (0 == strcmp("application/x-www-form-urlencoded", contentType)) {
        // application/x-www-form-urlencoded
        // post_data is "var1=val1&var2=val2...".
        int rc = readMgreq(req, postData, MAX_SIZE_UPLOAD);
        if (rc < 0) {
            sendHttpHeader413(req, "You tried to upload too much data. Max is 10 MB.");
            return;
        }
    } else {
        // bad request
        sendHttpHeader400(req, "");
        return;
    }
    //LOG_DEBUG("postData=%s\n<br>", postData.c_str());

    std::string deleteMark = getFirstParamFromQueryString(postData, "delete");
    enum Role role = u.getRole(p.getName());
    if (deleteMark == "1") {
        if (role != ROLE_ADMIN && !u.superadmin) {
            sendHttpHeader403(req);
            return;
        } else {
            // delete the view
            p.deletePredefinedView(name);
            std::string redirectUrl = "/" + p.getUrlName() + "/issues/";
            sendHttpRedirect(req, redirectUrl.c_str(), 0);
            return;
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
        else if (0 == key.compare(0, strlen("filter_value"), "filter_value")) filterValue = value.c_str();
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
        // unnamed view. This is an advanced search
        if (role != ROLE_ADMIN && role != ROLE_RO && role != ROLE_RW && !u.superadmin) {
            sendHttpHeader403(req);
            return;
        }
        // do nothing, just redirect

    } else { // named view
        if (role != ROLE_ADMIN && !u.superadmin) {
            sendHttpHeader403(req);
            return;
        }
        // store the view
        int r = p.setPredefinedView(name, pv);
        if (r < 0) {
            LOG_ERROR("Cannot set predefined view");
            sendHttpHeader500(req, "Cannot set predefined view");
            return;
        }
    }

    enum RenderingFormat format = getFormat(req);
    if (RENDERING_TEXT == format || RENDERING_CSV == format) {
        sendHttpHeader200(req);

    } else {
        // redirect to the result of the search
        std::string redirectUrl = "/" + p.getUrlName() + "/issues/?" + pv.generateQueryString();
        sendHttpRedirect(req, redirectUrl.c_str(), 0);
    }
}

/** Handle the posting of a tag
  *
  * If the ref is not tagged, then this will put a tag on the entry.
  * If the ref is already tagged, then this will remove the tag of the entry.
  * @param ref
  *     The reference of the message: <issue>/<entry>/<tagid>
  *
  */
void httpPostTag(const RequestContext *req, Project &p, std::string &ref, User u)
{
    enum Role role = u.getRole(p.getName());
    if (role != ROLE_RW && role != ROLE_ADMIN) {
        sendHttpHeader403(req);
        return;
    }

    std::string entryId = popToken(ref, '/');
    std::string tagname = ref;

    int r = p.toggleTag(entryId, tagname, u.username);
    if (r == 0) {
        sendHttpHeader200(req);
        req->printf("\r\n");
    } else {
        sendHttpHeader500(req, "cannot toggle tag");
    }
}

/** Reload a project from disk storage
  *
  * This encompasses the configuration and the entries.
  */
void httpReloadProject(const RequestContext *req, Project &p, User u)
{
    enum Role role = u.getRole(p.getName());
    if (role != ROLE_ADMIN) {
        sendHttpHeader403(req);
        return;
    }
    int r = p.reload();

    // redirect to config page of the project
    if (r == 0) {
        // success, redirect to
        std::string redirectUrl = "/" + p.getUrlName() + "/config";
        sendHttpRedirect(req, redirectUrl.c_str(), 0);

    } else { // error
        std::string msg = "Cannot reload project: ";
        msg += p.getName();
        LOG_ERROR("%s", msg.c_str());
        sendHttpHeader500(req, msg.c_str());
    }
}

int httpGetIssue(const RequestContext *req, Project &p, const std::string &issueId, User u)
{
    LOG_DEBUG("httpGetIssue: project=%s, issue=%s", p.getName().c_str(), issueId.c_str());
    enum RenderingFormat format = getFormat(req);

    Issue issue;
    int r = p.get(issueId, issue);
    if (r < 0) {
        // issue not found or other error
        // for example because the issueId has also the entry id: id/entry
        return REQUEST_NOT_PROCESSED;

    } else if (format == X_SMIT) {
        // for cloning, print the entries in the right order
        const Entry *e = issue.first;

        if (!e) {
            std::string msg = "null entry for issue: ";
            msg += issue.id;
            sendHttpHeader500(req, msg.c_str());
            return REQUEST_COMPLETED;
        }

        // print the entries in the rigth order
        sendHttpHeader200(req);
        req->printf("Content-Type: text/directory\r\n\r\n");

        while (e) {
           req->printf("%s\n", e->id.c_str());
           e = e->getNext();
        }

    } else {

        sendHttpHeader200(req);

        if (format == RENDERING_TEXT) RText::printIssue(req, issue);
        else {

            std::string q = req->getQueryString();
            std::string amend = getFirstParamFromQueryString(q, "amend");
            Entry *entryToBeAmended = 0;

            if (!amend.empty()) {
                // look for the entry in the entries of the issue
                Entry *e = issue.first;
                while (e) {
                    if (e->id == amend) break;
                    e = e->getNext();
                }
                entryToBeAmended = e;
            }

            ContextParameters ctx = ContextParameters(req, u, p);
            std::string originView;
            int r = getFromCookie(req, COOKIE_ORIGIN_VIEW, originView);
            if (r == 0) ctx.originView = originView.c_str();

            // clear this cookie, so that getting any other issue
            // without coming from a view does not display get/next
            // (get/next use a redirection from a view, so the cookie will be set for these)
            req->printf("%s\r\n", getDeletedCookieString(COOKIE_ORIGIN_VIEW).c_str());
            if (ctx.originView) LOG_DEBUG("originView=%s", ctx.originView);
            RHtml::printPageIssue(ctx, issue, entryToBeAmended);
        }
    }
    return REQUEST_COMPLETED;
}


void parseMultipartAndStoreUploadedFiles(const std::string &part, std::string boundary,
                                         std::map<std::string, std::list<std::string> > &vars,
                                         const Project &project)
{
    size_t n;
    const char *data;
    size_t dataSize;
    int offset = 0;
    std::string name;
    std::string filename;
    while ( (n = multipartGetNextPart(part.data()+offset, part.size(), boundary.c_str(),
                                      &data, &dataSize, name, filename) )) {
        if (name != K_FILE) {
            // regular property
            LOG_DIAG("Multipart: name=%s", name.c_str());
            std::string value;
            value.assign(data, dataSize);
            vars[name].push_back(value);

        } else if (!filename.empty()) {
            // uploaded file
            LOG_DIAG("Multipart: filename=%s", filename.c_str());

            // case of a file
            // keep only the basename
            size_t lastSlash = filename.find_last_of("\\/");
            if (lastSlash != std::string::npos) filename = filename.substr(lastSlash+1);

            // store to objects directory
            std::string objectid;
            int r = Object::write(project.getObjectsDir(), data, dataSize, objectid);
            if (r < 0) {
                LOG_ERROR("Could not store uploaded file: %s", filename.c_str());
                return;
            }
            std::string base = objectid + "/" + filename;
            vars[name].push_back(base);
        }

        offset += n;
    }
}


void httpPushEntry(const RequestContext *req, Project &p, const std::string &issueId,
                   const std::string &entryId, User u)
{
    LOG_DEBUG("httpPushEntry: %s/%s", issueId.c_str(), entryId.c_str());

    enum Role r = u.getRole(p.getName());
    if (r != ROLE_RW && r != ROLE_ADMIN) {
        sendHttpHeader403(req);
        return;
    }
    const char *multipart = "application/octet-stream";
    const char *contentType = getContentType(req);
    if (0 == strncmp(multipart, contentType, strlen(multipart))) {

        std::string postData;
        int rc = readMgreq(req, postData, MAX_SIZE_UPLOAD);
        if (rc < 0) {
            sendHttpHeader413(req, "You tried to upload too much data. Max is 10 MB.");
            return;
        }

        LOG_DEBUG("Got upload data: %ld bytes", L(postData.size()));

        // store the entry in a temporary location
        std::string tmpPath = p.getTmpDir() + "/" + entryId;
        int r = writeToFile(tmpPath, postData);
        if (r != 0) {
            std::string msg = "Failed to store pushed entry: %s" + entryId;
            sendHttpHeader500(req, msg.c_str());
            return;
        }

        // insert the entry into the database
        std::string id = issueId;
        r = p.pushEntry(id, entryId, u.username, tmpPath);
        if (r == -1) {
            std::string msg = "Cannot push the entry";
            sendHttpHeader400(req, msg.c_str());

        } else if (r == -2) {
            // Internal Server Error
            std::string msg = "pushEntry error";
            sendHttpHeader500(req, msg.c_str());
        } else if (r == -3) {
            // HTTP 409 Conflict
            sendHttpHeader409(req);

        } else {
            // ok, no problem
            sendHttpHeader201(req);
            // give the issue id, as it may be necessary to inform
            // the client that it has been renamed (renumbered)
            req->printf("issue: %s\r\n", id.c_str());
        }

        unlink(tmpPath.c_str()); // clean-up the tmp file

    } else {
        LOG_ERROR("Content-Type '%s' not supported", contentType);
        sendHttpHeader400(req, "bad content-type");
        return;
    }
}

/** Remove empty values for multiselect properties
  *
  * empty values are not relevant
  * and the HTML form forces the use of an empty value.
  */

void cleanMultiselectProperties(const ProjectConfig &config, std::map<std::string, std::list<std::string> > &properties)
{
    std::map<std::string, std::list<std::string> >::iterator p;
    FOREACH(p, properties) {
        // if multiselect
        const PropertySpec *pspec = config.getPropertySpec(p->first);
        if (pspec && pspec->type == F_MULTISELECT) {
            // erase empty values from p->second
            std::list<std::string>::iterator v;
            v = p->second.begin();
            while (v != p->second.end()) {
                if (v->empty()) {
                    // delete the current value
                    std::list<std::string>::iterator itemToErase = v;
                    v++;
                    p->second.erase(itemToErase);

                } else v++;
            }
        }
    }
}


/** Handle the posting of an entry
  * If issueId is empty, then a new issue is created.
  */
void httpPostEntry(const RequestContext *req, Project &pro, const std::string & issueId, User u)
{
    enum Role role = u.getRole(pro.getName());
    if (role != ROLE_RW && role != ROLE_ADMIN) {
        sendHttpHeader403(req);
        return;
    }
    const char *multipart = "multipart/form-data";
    std::map<std::string, std::list<std::string> > vars;

    const char *contentType = getContentType(req);
    if (0 == strncmp(multipart, contentType, strlen(multipart))) {

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
        int rc = readMgreq(req, postData, MAX_SIZE_UPLOAD);
        if (rc < 0) {
            sendHttpHeader413(req, "You tried to upload too much data. Max is 10 MB.");
            return;
        }

        parseMultipartAndStoreUploadedFiles(postData, boundary, vars, pro);

    } else {
        // other Content-Type
        LOG_ERROR("Content-Type '%s' not supported", contentType);
        sendHttpHeader400(req, "Bad Content-Type");
        return;
    }

    bool isNewIssue = false;
    std::string id = issueId;
    Entry *entry = 0;

    std::string amendedEntry = getProperty(vars, K_AMEND);
    int r = 0;
    if (!amendedEntry.empty()) {
        // this post is an amendment to an existing entry
        std::string newMessage = getProperty(vars, K_MESSAGE);
        r = pro.amendEntry(amendedEntry, newMessage, entry, u.username);
        if (r < 0) {
            // failure
            LOG_ERROR("amendEntry returned %d", r);
            sendHttpHeader500(req, "Cannot amend entry");
        }

    } else {
        // nominal post
        cleanMultiselectProperties(pro.getConfig(), vars);

        if (id == "new") {
            id = "";
            isNewIssue = true;
        }
        r = pro.addEntry(vars, id, entry, u.username);
        if (r < 0) {
            // error
            sendHttpHeader500(req, "Cannot add entry");
        }
    }

#if !defined(_WIN32)
    if (entry) {
        // launch the trigger only if a new entry was actually created
        if (! UserBase::isLocalUserInterface()) Trigger::notifyEntry(pro, entry, isNewIssue);
    }
#endif

    if (r >= 0) {
        // redirect to the page of the issue
        if (getFormat(req) == RENDERING_HTML) {
            // HTTP redirect
            std::string redirectUrl = "/" + pro.getUrlName() + "/issues/" + id;
            sendHttpRedirect(req, redirectUrl.c_str(), 0);

        } else {
            sendHttpHeader200(req);
            req->printf("\r\n");
            if (entry) req->printf("%s/%s\r\n", entry->issue->id.c_str(), entry->id.c_str());
            else req->printf("%s/(no change)\r\n", id.c_str());
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
  * /users/                            superadmin        management of users for all projects
  * /users/<user>           GET/POST   user, superadmin  management of a single user
  * /_                      GET/POST   superadmin        new project
  * /.smit                  GET        user              .smit directory
  * /<p>/config             GET/POST   admin             configuration of the project
  * /<p>/views/             GET/POST   admin             list predefined views / create new view
  * /<p>/views/_            GET        admin             form for advanced search / new predefined view
  * /<p>/views/xyz          GET/POST   admin             display / update / rename predefined view
  * /<p>/views/xyz?delete=1 POST       admin             delete predefined view
  * /<p>/issues             GET/POST   user              issues of the project / add new issue
  * /<p>/issues/new         GET        user              page with a form for submitting new issue
  * /<p>/issues/123         GET/POST   user              a particular issue: get all entries or add a new entry
  * /<p>/issues/x/y         POST       user              push an entry
  * /<p>/tags/x/y           POST       user              tag / untag an entry
  * /<p>/reload             POST       admin             reload project from disk storage
  * /<p>/files/<id>/<name>  GET        user              get an attached file
  * /<p>/files/123          POST       user              push a file
  * /<p>/other/file         GET        user              any static file
  * / * /issues             GET        user              issues of all projects
  */

int begin_request_handler(const RequestContext *req)
{
    LOG_FUNC();

    std::string uri = req->getUri();
    std::string method = req->getMethod();
    LOG_DIAG("%s %s", method.c_str(), uri.c_str());

    std::string resource = popToken(uri, '/');

    // increase statistics
    if (method == "GET") addHttpStat(H_GET);
    else if (method == "POST") addHttpStat(H_POST);
    else addHttpStat(H_OTHER);

    // check method
    std::string m = method; // use a shorter name to have a shorter next line
    if (m != "GET" && m != "POST" && m != "HEAD" && m != "DELETE") return sendHttpHeader400(req, "invalid method");

    // public access to /public and /sm
    if    ( (resource == "public") && (method == "GET")) return httpGetFile(req);
    else if (resource == "public") return sendHttpHeader400(req, "invalid method");

    if      ( (resource == "sm") && (method == "GET") ) return httpGetSm(req, uri);
    else if ( (resource == "sm") && (method == "POST") ) return httpPostSm(req, uri);
    else if (resource == "sm") return sendHttpHeader400(req, "invalid method");

    if    ( (resource == "signin") && (method == "POST") ) return httpPostSignin(req);
    else if (resource == "signin") return sendHttpRedirect(req, "/", 0);


    // get signed-in user
    std::string sessionId;
    getFromCookie(req, COOKIE_SESSID_PREFIX, sessionId);
    // even if cookie not found, call getLoggedInUser in order to manage
    // local user interface case (smit ui)
    User user = SessionBase::getLoggedInUser(sessionId);
    LOG_DIAG("Session %s -> user '%s'", sessionId.c_str(), user.username.c_str());
    // if username is empty, then no access is granted (only public pages will be available)

    if (user.username.empty()) return handleUnauthorizedAccess(req, false); // no user signed-in

    // at this point there is a signed-in user
    LOG_DIAG("User signed-in: %s", user.username.c_str());

    bool handled = true; // by default, do not let Mongoose handle the request

    if      ( (resource == "signout") && (method == "POST") ) httpPostSignout(req, sessionId);
    else if ( (resource == "") && (method == "GET") ) httpGetProjects(req, user);
    else if ( (resource == "") && (method == "POST") ) httpPostRoot(req, user);
    else if ( (resource == "users") && (method == "GET") ) httpGetUser(req, user, uri);
    else if ( (resource == "users") && (method == "POST") ) httpPostUser(req, user, uri);
    else if ( (resource == "users") && (method == "DELETE") ) httpDeleteUser(req, user, uri);
    else if ( (resource == "_") && (method == "GET") ) httpGetNewProject(req, user);
    else if ( (resource == "_") && (method == "POST") ) httpPostNewProject(req, user);
    else if ( (resource == "*") && (method == "GET") ) httpIssuesAccrossProjects(req, user, uri);
    else if ( (resource == ".smit") && (method == "GET") ) return httpGetSmitRepo(req, user, uri);
    else {
        // Get the project given by the uri.
        // We need to concatenate back 'resource' and 'uri', as resource was
        // previously popped from the URI.
        uri = resource + "/" + uri;
        Project *p = Database::Db.lookupProject(uri);

        if (!p) {
            // No such project. Bad request
            LOG_DIAG("Unknown project for URI '%s'", uri.c_str());
            // Send same error as for existing project in order to prevent
            // an attacker from deducing existing projects after the http status code
            return handleUnauthorizedAccess(req, true);
        }
        LOG_DIAG("project %s, %p", p->getName().c_str(), p);

        // check if user has at least read access
        enum Role r = user.getRole(p->getName());
        if (r != ROLE_ADMIN && r != ROLE_RW && r != ROLE_RO && ! user.superadmin) {
            // no access granted for this user to this project
            return handleUnauthorizedAccess(req, true);
        }

        // at this point the user has read access to the resource inside the project
        LOG_DIAG("user '%s' has role '%s' on project '%s'", user.username.c_str(),
                 roleToString(r).c_str(), p->getName().c_str());

        bool isdir = false;
        if (!uri.empty() && uri[uri.size()-1] == '/') isdir = true;

        resource = popToken(uri, '/');
        LOG_DEBUG("resource=%s", resource.c_str());
        if      ( resource.empty()       && (method == "GET") ) httpGetProject(req, *p, user);
        else if ( (resource == "issues") && (method == "GET") && uri.empty() ) httpGetListOfIssues(req, *p, user);
        else if ( (resource == "issues") && (method == "POST") ) {
            // /<p>/issues/<issue>/<entry> [/...]
            std::string issueId = popToken(uri, '/');
            std::string entryId = popToken(uri, '/');
            if (entryId.empty()) httpPostEntry(req, *p, issueId, user);
            else if (uri.empty()) httpPushEntry(req, *p, issueId, entryId, user);
            else return sendHttpHeader400(req, "");

        } else if ( (resource == "issues") && (uri == "new") && (method == "GET") ) httpGetNewIssueForm(req, *p, user);
        else if ( (resource == "issues") && (method == "GET") ) return httpGetIssue(req, *p, uri, user);
        else if ( (resource == "entries") && (method == "GET") ) httpGetListOfEntries(req, *p, user);
        else if ( (resource == "config") && (method == "GET") ) httpGetProjectConfig(req, *p, user);
        else if ( (resource == "config") && (method == "POST") ) httpPostProjectConfig(req, *p, user);
        else if ( (resource == "views") && (method == "GET") && !isdir && uri.empty()) return httpGetFile(req);
        else if ( (resource == "views") && (method == "GET")) httpGetView(req, *p, uri, user);
        else if ( (resource == "views") && (method == "POST") ) httpPostView(req, *p, uri, user);
        else if ( (resource == "tags") && (method == "POST") ) httpPostTag(req, *p, uri, user);
        else if ( (resource == "reload") && (method == "POST") ) httpReloadProject(req, *p, user);
        else if ( (resource == RESOURCE_FILES) && (method == "POST") ) httpPushAttachedFile(req, *p, uri, user);
        else if ( (resource == RESOURCE_FILES) && (method == "GET") ) httpGetObject(req, *p, uri);
        else if ( (resource == RESOURCE_FILES) && (method == "HEAD") ) httpGetHeadObject(req, *p, uri);
        else if ( (resource == "stat") && (method == "GET") ) httpGetStat(req, *p, user);
        else handled = false;

    }

    if (handled) return REQUEST_COMPLETED; // do not let Mongoose handle the request
    else return httpGetFile(req);
}

