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
#include "httpdHandlerGit.h"
#include "repository/db.h"
#include "utils/logging.h"
#include "utils/identifiers.h"
#include "utils/parseConfig.h"
#include "utils/stringTools.h"
#include "utils/cpio.h"
#include "utils/filesystem.h"
#include "rendering/renderingText.h"
#include "rendering/renderingHtml.h"
#include "rendering/renderingZip.h"
#include "rendering/renderingHtmlIssue.h"
#include "rendering/ContextParameters.h"
#include "rendering/renderingCsv.h"
#include "rendering/renderingJson.h"
#include "project/gitdb.h"
#include "user/session.h"
#include "user/Recipient.h"
#include "user/AuthSha1.h"
#ifdef KERBEROS_ENABLED
  #include "user/AuthKrb5.h"
#endif
#ifdef LDAP_ENABLED
  #include "user/AuthLdap.h"
#endif
#include "user/notification.h"
#include "global.h"
#include "mg_win32.h"
#include "server/Trigger.h"
#include "restApi.h"
#include "embedcpio.h" // generated


#define K_ME "me"
#define MAX_SIZE_UPLOAD (10*1024*1024)
#define COOKIE_ORIGIN_VIEW "view-"

int httpPostSignin(const RequestContext *req)
{
    LOG_FUNC();

    ContentType ct = getContentType(req);

    if (ct != CT_WWW_FORM_URLENCODED) {
        LOG_ERROR("Unsupported Content-Type in httpPostSignin: %s", getContentTypeString(req));
        return sendHttpHeader400(req, "Unsupported Content-Type");
    }

    // application/x-www-form-urlencoded
    // post_data is "var1=val1&var2=val2...".

    const int SIZ = 1024;
    char buffer[SIZ+1];
    char password[SIZ+1];
    int n; // number of bytes read
    n = req->read(buffer, SIZ);
    if (n < 0) {
        LOG_ERROR("httpPostSignin: read error %d", n);
        sendHttpHeader500(req, "httpPostSignin: read error");
        return REQUEST_COMPLETED;
    }
    if (n == SIZ) {
        LOG_ERROR("Post data for signin too long. Abort request.");
        return sendHttpHeader400(req, "Post data for signin too long");
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
        return sendHttpHeader400(req, "Missing user name");
    }
    std::string username = buffer;

    // get the password
    r = mg_get_var(postData.c_str(), postData.size(),
                   "password", password, SIZ);

    if (r<0) {
        // error: empty, or too long, or not present
        LOG_DEBUG("Cannot get password. r=%d, postData=%s", r, postData.c_str());
        return sendHttpHeader400(req, "Missing password");
    }

    // check credentials
    std::string sessionId = SessionBase::requestSession(username, password);
    LOG_DIAG("User %s got sessid: %s", username.c_str(), sessionId.c_str());
    memset(password, 0xFF, SIZ+1); // erase password

    if (sessionId.size() == 0) {
        LOG_DEBUG("Authentication refused");
        sendHttpHeader403(req);
        return REQUEST_COMPLETED;
    }

    // Sign-in accepted

    std::string redirect;
    enum RenderingFormat format = getFormat(req);

    if (format == RENDERING_TEXT) {
        std::string cookieSessid = getServerCookie(req, COOKIE_SESSID_PREFIX, sessionId,
                                                   Database::getSessionDuration());
        sendHttpHeader204(req, cookieSessid.c_str());
    } else {
        // HTML rendering
        // Get the redirection page
        r = mg_get_var(postData.c_str(), postData.size(),
                       "redirect", buffer, SIZ);

        if (r<0) {
            // error: empty, or too long, or not present
            LOG_DEBUG("Cannot get redirect. r=%d, postData=%s", r, postData.c_str());
            return sendHttpHeader400(req, "Cannot get redirection");
        }
        redirect = buffer;

        if (redirect.empty()) redirect = "/";
        std::string s = getServerCookie(req, COOKIE_SESSID_PREFIX, sessionId,
                                        Database::getSessionDuration());
        sendHttpRedirect(req, redirect, s.c_str());
    }

    return REQUEST_COMPLETED;
}

void redirectToSignin(const RequestContext *req, const char *resource = 0)
{
    LOG_DIAG("redirectToSignin");
    sendHttpHeader200(req);

    // delete session cookie
    req->printf("%s\r\n", getDeletedCookieString(req, COOKIE_SESSID_PREFIX).c_str());

    // prepare the redirection parameter
    std::string url;
    if (!resource) {
        url = req->getUri();
        const char *qs = req->getQueryString();
        if (qs && strlen(qs)) url = url + '?' + qs;
        resource = url.c_str();
    }
    User noUser;
    ContextParameters ctx = ContextParameters(req, noUser);
    RHtml::printPageSignin(ctx, resource);
}

void httpPostSignout(const RequestContext *req, const std::string &sessionId)
{
    SessionBase::destroySession(sessionId);

    enum RenderingFormat format = getFormat(req);
    if (format == RENDERING_HTML) {
        redirectToSignin(req, "/");

    } else {
        // delete session cookie
        std::string cookieSessid = getDeletedCookieString(req, COOKIE_SESSID_PREFIX);
        sendHttpHeader204(req, cookieSessid.c_str());
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

void handleMessagePreview(const RequestContext *req)
{
    LOG_FUNC();
    std::string boundary;
    ContentType ct = getContentType(req, boundary);

    if (ct != CT_MULTIPART_FORM_DATA) {
        LOG_ERROR("Content-Type '%s' not supported", getContentTypeString(req));
        sendHttpHeader400(req, "Bad Content-Type");
        return;
    }

    if (boundary.empty()) {
        LOG_ERROR("Missing boundary in multipart form data");
        sendHttpHeader400(req, "Missing boundary");
        return;
    }
    LOG_DEBUG("Boundary: %s", boundary.c_str());

    std::string postData;
    int rc = readMgreq(req, postData, 1024*1024); // max 1 MB
    if (rc < 0) {
        sendHttpHeader413(req, "Too much data for preview (max 1 MB)");
        return;
    }

    size_t n;
    const char *data;
    size_t dataSize;
    int offset = 0;
    std::string name;
    std::string filename;
    std::string message;

    while ( (n = multipartGetNextPart(postData.data()+offset, postData.size(), boundary.c_str(),
                                      &data, &dataSize, name, filename) )) {
        if (name == K_MESSAGE) {
            message.assign(data, dataSize);
        }

        offset += n;
    }

    trim(message);
    LOG_DIAG("message=%s", message.c_str());
    message = RHtmlIssue::convertToRichText(htmlEscape(message));
    LOG_DIAG("rich message=%s", message.c_str());
    sendHttpHeader200(req);
    req->printf("Content-Type: text/html\r\n\r\n");
    req->printf("%s", message.c_str());
}


int httpPostSm(const RequestContext *req, const std::string &resource)
{
    LOG_INFO("httpPostSm: %s", resource.c_str());

    if (resource != "preview") {
        LOG_ERROR("httpPostSm: Unsupported resource '%s'", resource.c_str());
        return sendHttpHeader400(req, "");
    }

    handleMessagePreview(req);

    return REQUEST_COMPLETED;
}

/** Get a SM embedded file
  *
  * Embbeded files: smit.js, etc.
  */
int httpGetSm(const RequestContext *request, const std::string &file)
{
    int r; // return 0 to let mongoose handle static file, 1 otherwise

    LOG_DEBUG("httpGetSm: %s", file.c_str());
    if (0 == strcmp(file.c_str(), "stat")) {
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
            if (format == RENDERING_TEXT) {
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
void httpDeleteUser(const RequestContext *request, const User &signedInUser, const std::string &username)
{
    if (!signedInUser.superadmin) {
        sendHttpHeader403(request);
        return;
    }

    LOG_INFO("User '%s' deleted by '%s'", username.c_str(), signedInUser.username.c_str());

    int r = UserBase::deleteUser(username, signedInUser.username);

    if (r != 0) {
        sendHttpHeader400(request, "Cannot delete user");

    } else {
        // ok, redirect
        sendHttpRedirect(request, "/" RSRC_USERS, 0);
    }
}

/** Hot reload of users (if hotreload=1)
 *
 * Verification that the signed-in user is a superadmin
 * must have been done before calling this function.
 */
void httpUserHotReload(const RequestContext *req)
{
    // execute the hot reload
    int r = UserBase::hotReload();
    if (r != 0) {
        sendHttpHeader500(req, "Hot reload failed");
        return;
    }

    // Ok, redirect
    sendHttpRedirect(req, "/users", 0);
}

struct PairKeyValue {
    std::string key;
    std::string value;
};
typedef std::list<PairKeyValue> PairKeyValueList;
typedef std::list<PairKeyValue>::const_iterator PairKeyValueListIt;

static bool pkvHasKey(const PairKeyValueList &params, const std::string &key)
{
    PairKeyValueListIt param;
    FOREACH(param, params) {
        if (param->key == key) return true;
    }
    return false;
}

/** Get the first value of the list
 */
static std::string pkvGetValue(const PairKeyValueList &params, const std::string &key)
{
    PairKeyValueListIt param;
    FOREACH(param, params) {
        if (param->key == key) {
            LOG_DIAG("pkvGetValue(%s) -> %s", key.c_str(), param->value.c_str());
            std::string value = param->value;
            trim(value); // consider only-blanks as empty
            return value;
        }
    }
    LOG_DIAG("pkvGetValue(%s) -> (null)", key.c_str());
    return "";
}
static std::string pkvGetNext(const PairKeyValueList &params, PairKeyValueListIt &it, const std::string &key)
{
    if (it == params.end()) it = params.begin();
    else it++;

    while (it != params.end()) {
        if (it->key == key) return it->value;
        it++;
    }
    return "";
}

/** Parse the data received wihtin the request
 *
 * The expected input format is "application/x-www-form-urlencoded".
 *
 * @param[out] params
 *   A key-value map, where the value is a list of the values encountered for the key.
 *   Most of the time the value will contain a single item.
 *
 * @return
 *    0 : success
 *   -1 : error
 */
int parseFormUrlEncoded(const RequestContext *req, PairKeyValueList &params)
{
    ContentType ct = getContentType(req);

    if (ct != CT_WWW_FORM_URLENCODED) {
        LOG_ERROR("parseFormUrlEncoded: bad content type: %s", getContentTypeString(req));
        return -1;
    }

    // Receive the whole data
    std::string data;
    int rc = readMgreq(req, data, 4096*10); // allow size of one GPG key
    if (rc < 0) {
        LOG_ERROR("parseFormUrlEncoded: too much data uploaded");
        return -1;
    }

    // Parse the data (expected in format application/x-www-form-urlencoded)
    // Format is : var1=val1&var2=val2...

    while (data.size() > 0) {
        std::string tokenPair = popToken(data, '&');

        PairKeyValue pkv;
        pkv.key = popToken(tokenPair, '=');
        pkv.key = urlDecode(pkv.key);
        pkv.value = urlDecode(tokenPair);
        params.push_back(pkv);
    }
    return 0;
}

/** Process a user notification config POST request
 *
 *  @param postedParams
 *      The parameters of the POST request
 *
 *  @param[out] newUserConfig
 *      The object where the configuration is stored
 */
static void processNofiticationConfig(const PairKeyValueList &postedParams, User &newUserConfig)
{
    const PairKeyValueList &params = postedParams;

    // Process notifications
    if (pkvHasKey(params, "sm_email")) newUserConfig.notification.email = pkvGetValue(params, "sm_email");

    if (pkvHasKey(params, "sm_gpg_key")) newUserConfig.notification.gpgPublicKey = pkvGetValue(params, "sm_gpg_key");

    if (pkvHasKey(params, "sm_notif_policy")) {
        newUserConfig.notification.notificationPolicy = pkvGetValue(params, "sm_notif_policy");

        // NOTIFY_POLICY_CUSTOM not supported at the moment.

        if (newUserConfig.notification.notificationPolicy == NOTIFY_POLICY_ALL) {
            // ok, nothing to do
        } else if (newUserConfig.notification.notificationPolicy == NOTIFY_POLICY_NONE) {
            // ok, nothing to do
        } else if (newUserConfig.notification.notificationPolicy == NOTIFY_POLICY_ME) {
            // ok, nothing to do
        } else {
            LOG_ERROR("Invalid notification policy: %s", newUserConfig.notification.notificationPolicy.c_str());
            newUserConfig.notification.notificationPolicy = NOTIFY_POLICY_NONE;
        }
    }
}

/** Process POST request on /users
 *
 * Verification that the signed-in user is a superadmin
 * must have been done before calling this function.
 */
void httpPostUserAsSuperadmin(const RequestContext *req, const std::string &username, const std::string &superadminName)
{
    PairKeyValueList params;
    int r = parseFormUrlEncoded(req, params);
    if (r < 0) {
        sendHttpHeader400(req, "Invalid submitted data");
        return;
    }

    // Check if hot reload request
    if (username.empty() && pkvGetValue(params, "hotreload") == "1") {
        return httpUserHotReload(req);
    }

    User newUserConfig;
    if (!username.empty() && username != "_") {
        // Copy existing config into newUserConfig
        User *existingUser = UserBase::getUser(username);
        if (!existingUser) {
            sendHttpHeader400(req, "No such user");
            return;
        }
        newUserConfig = *existingUser; // copy
    }

    // Process 'sm_username'
    const char *key = "sm_username";
    if (pkvHasKey(params, key)) {

        newUserConfig.username = pkvGetValue(params, key);

        // Check that the username given in the form is not empty.
        if (newUserConfig.username.empty() || newUserConfig.username == "_") {
            LOG_INFO("Ignore user parameters as username is empty or '_'");
            sendHttpHeader400(req, "Invalid user name");
            return;
        }

        LOG_DIAG("Username changing?: '%s' -> '%s'", username.c_str(), newUserConfig.username.c_str());
    }

    // Process 'superadmin'
    key = "sm_superadmin";
    if (pkvHasKey(params, key)) {
        if (pkvGetValue(params, key) == "on") newUserConfig.superadmin = true;
        else newUserConfig.superadmin = false;

    } else {
        // Some browsers do not post checkboxes 'off', so deactivate 'superadmin' if not present
        newUserConfig.superadmin = false;
    }
    LOG_INFO("Superadmin=%d for user '%s'", newUserConfig.superadmin, username.c_str());


    // Process authentication parameters

    std::string authType = pkvGetValue(params, "sm_auth_type");
    std::string passwd1 = pkvGetValue(params, "sm_passwd1");
    if (!passwd1.empty()) {
        // This forces authentication scheme SHA1
        if (pkvGetValue(params, "sm_passwd2") != passwd1) {
            const char *msg = "passwords 1 and 2 do not match";
            LOG_ERROR("PhttpPostUser: %s", msg);
            sendHttpHeader400(req, msg);
            return;
        }
        LOG_DIAG("Password changing for user '%s'", username.c_str());
        newUserConfig.setPasswd(passwd1);

    } else if (authType != AUTH_SHA1 && ! authType.empty()) {

        if (authType == AUTH_NONE) {
               // Remove authentication: the user will not be able to sign in
               LOG_INFO("Remove authentication from user: %s", username.c_str());
               if (newUserConfig.authHandler) delete newUserConfig.authHandler;
               newUserConfig.authHandler = NULL;
        }

#ifdef KERBEROS_ENABLED
        else if (authType == AUTH_KRB5) {

            AuthKrb5 authKrb5(newUserConfig.username,
                              pkvGetValue(params, "sm_krb5_primary"),
                              pkvGetValue(params, "sm_krb5_realm"));
            newUserConfig.authHandler = authKrb5.createCopy();
        }
#endif

#ifdef LDAP_ENABLED
        else if (authType == AUTH_LDAP) {

            AuthLdap authLdap(newUserConfig.username,
                              pkvGetValue(params, "sm_ldap_uri"),
                              pkvGetValue(params, "sm_ldap_dname"));

            newUserConfig.authHandler = authLdap.createCopy();
        }
#endif
        else {
            std::string msg = "Unsupported authentication scheme: ";
            msg += authType;
            LOG_ERROR("User Config: %s", msg.c_str());
            sendHttpHeader400(req, msg.c_str());
            return;
        }
    }

    // Process permissions parameters
    PairKeyValueListIt pit = params.end();
    pkvGetNext(params, pit, "project_wildcard");
    if (pit != params.end()) {

        // The parameters require a configuration for permissions/roles

        newUserConfig.permissions.clear(); // the new config will replace the old one

        while (pit != params.end()) {
            std::string wildcard = pit->value;
            pit++;
            if (pit == params.end() || pit->key != "role") {
                LOG_ERROR("User Config: Missing role");
            } else {
                std::string role = pit->value;
                Role r = stringToRole(role);
                if (r != ROLE_NONE) newUserConfig.permissions[wildcard] = r;
            }
            pkvGetNext(params, pit, "project_wildcard");
        }
    }

    // Notifications
    processNofiticationConfig(params, newUserConfig);

    // If no error, then take into account the new configuration
    std::string error;
    if (username == "_") {
        r = UserBase::addUser(newUserConfig, superadminName);
        if (r == -1) error = "Cannot create user with empty name";
        else if (r == -2) error = "Cannot create new user as name already exists";

    } else {
        r = UserBase::updateUser(username, newUserConfig, superadminName);
        if (r == -1) error = "Cannot create user with empty name";
        else if (r == -2) error = "Cannot change name as new name already exists";
        else if (r < 0) error = "Cannot update non existing user";

    }

    if (r != 0) LOG_ERROR("Cannot update user '%s': %s", username.c_str(), error.c_str());
    else if (newUserConfig.username != username) {
        LOG_INFO("User '%s' renamed '%s'", username.c_str(), newUserConfig.username.c_str());
    } else LOG_INFO("Parameters of user '%s' updated by '%s'", username.c_str(), superadminName.c_str());

    if (r != 0) {
        sendHttpHeader500(req, error.c_str());
        return;
    }

    // the request has been accepted and processed ok
    enum RenderingFormat format = getFormat(req);

    if (format == RENDERING_TEXT) {
        // No redirection
        sendHttpHeader204(req, 0);

    } else {
        // Redirect
        std::string redirectUrl = "/users/" + urlEncode(newUserConfig.username);
        sendHttpRedirect(req, redirectUrl.c_str(), 0);
    }
}

/** Process POST request on /users/<self>
 *
 * This considers the case of a regular user requesting to modify
 * his/her own profile.
 *
 * Permission given to the signed-in user
 * must have been verified before calling this function.
 */
void httpPostUserSelf(const RequestContext *req, const std::string &username)
{
    PairKeyValueList params;
    int r = parseFormUrlEncoded(req, params);
    if (r < 0) {
        sendHttpHeader400(req, "Invalid submitted data");
        return;
    }

    User newUserConfig;

    User *existingUser = UserBase::getUser(username);
    if (!existingUser) {
        sendHttpHeader400(req, "No such user");
        return;
    }
    newUserConfig = *existingUser; // Copy existing config into newUserConfig

    // Process password

    std::string passwd1 = pkvGetValue(params, "sm_passwd1");
    if (!passwd1.empty()) {
        // A password is submitted

        if (pkvGetValue(params, "sm_passwd2") != passwd1) {
            const char *msg = "passwords 1 and 2 do not match";
            LOG_ERROR("PhttpPostUser: %s", msg);
            sendHttpHeader400(req, msg);
            return;
        }
        LOG_DIAG("Password changing for user '%s'", username.c_str());
        newUserConfig.setPasswd(passwd1);

    } // else, do not modify the password, nor any other authentication parameter

    // Notifications
    processNofiticationConfig(params, newUserConfig);

    std::string error;
    r = UserBase::updateUser(username, newUserConfig, username);
    if (r == -1) error = "Cannot create user with empty name";
    else if (r == -2) error = "Cannot change name as new name already exists";
    else if (r < 0) error = "Cannot update non existing user";

    if (r != 0) {
        LOG_ERROR("Cannot update user '%s': %s", username.c_str(), error.c_str());
        sendHttpHeader500(req, error.c_str());
        return;
    }

    LOG_INFO("Parameters of user '%s' updated by self", username.c_str());

    // the request has been accepted and processed ok
    enum RenderingFormat format = getFormat(req);

    if (format == RENDERING_TEXT) {
        // No redirection
        sendHttpHeader204(req, 0);

    } else {
        // Redirect
        std::string redirectUrl = "/users/" + urlEncode(username);
        sendHttpRedirect(req, redirectUrl.c_str(), 0);
    }
}

/** Post configuration of a new or existing user
  *
  * Non-superadmin users may only post:
  * - their new password
  * - their notification configuration
  */
void httpPostUser(const RequestContext *req, const User &signedInUser, const std::string &username)
{
    // Only superadmin has permission to configure other users
    if (signedInUser.superadmin) return httpPostUserAsSuperadmin(req, username, signedInUser.username);

    if (username != signedInUser.username) {
        sendHttpHeader403(req);
        return;
    }

    return httpPostUserSelf(req, username);
}

int handleUnauthorizedAccess(const RequestContext *req, bool signedIn)
{
    if (!signedIn && getFormat(req) == RENDERING_HTML) redirectToSignin(req, 0);
    else sendHttpHeader403(req);

    return REQUEST_COMPLETED;
}

/** Get a list of the projects to which a user may access
  *
  * @param[out] pList
  *     A list of pairs (project-name, user-role)
  */
void getProjects(const User &u, std::list<ProjectSummary> &pList)
{
    pList.clear();

    std::list<std::pair<std::string, std::string> > userProjects;

    if (!u.superadmin) {
        // Get the list of the projects to which the user has permission
        userProjects = u.getProjects();

    } else {
        // Superadmin
        // Get the list of all projects
        // (case of a superadmin of a local user)
        std::list<std::string> allProjects = Database::getProjects();
        std::list<std::string>::iterator p;
        FOREACH(p, allProjects) {
            Role r = u.getRole(*p);
            userProjects.push_back(std::make_pair(*p ,roleToString(r)));
        }
    }

    // retrieve the # issues and lastModified
    std::list<std::pair<std::string, std::string> >::iterator p;
    FOREACH(p, userProjects) {
        ProjectSummary ps;
        ps.name = p->first;
        ps.myRole = p->second;
        Project *p = Database::getProject(ps.name);
        if (!p) {
            LOG_ERROR("Cannot find project '%s', stats will be invalid", ps.name.c_str());
        } else {
            ps.lastModified = p->getLastModified();
            ps.nIssues = p->getNumIssues();
            ps.triggerCmdline = p->getTriggerCmdline();
        }

        pList.push_back(ps);
    }
}

void httpGetProjects(const RequestContext *req, const User &u)
{
    sendHttpHeader200(req);
    // print list of available projects
    std::list<ProjectSummary> pList;
    getProjects(u, pList);
    enum RenderingFormat format = getFormat(req);

    if (format == RENDERING_TEXT) RText::printProjectList(req, pList);
    else if (format == RENDERING_CSV) RCsv::printProjectList(req, pList);
    else {

        // get the list of users and roles for each project
        std::map<std::string, std::map<Role, std::set<std::string> > > usersRolesByProject;
        std::list<ProjectSummary>::const_iterator p;
        FOREACH(p, pList) {
            std::map<Role, std::set<std::string> > ur = UserBase::getUsersByRole(p->name);
            usersRolesByProject[p->name] = ur;
        }

        ContextParameters ctx = ContextParameters(req, u);
        RHtml::printPageProjectList(ctx, pList, usersRolesByProject);
    }
}

void httpGetNewProject(const RequestContext *req, const User &u)
{
    if (! u.superadmin) return sendHttpHeader403(req);

    ProjectConfig pconfig;
    const Project *pPtr = 0;

    std::string q = req->getQueryString();
    std::string copyConfigFrom = getFirstParamFromQueryString(q, "copy-config-from");
    if (!copyConfigFrom.empty()) {
        // initiate a new config, copied from this one
        pPtr = Database::Db.getProject(copyConfigFrom);
    }

    if (pPtr) {
        pconfig = pPtr->getConfig();
    } else {
        // add by default 2 properties : status (open, closed) and owner (selectUser)
        PropertySpec pspec;
        pspec.name = "status";
        pspec.type = F_SELECT;
        pspec.selectOptions.push_back("open");
        pspec.selectOptions.push_back("closed");
        pconfig.properties.push_back(pspec);
        pspec.name = "owner";
        pspec.type = F_SELECT_USER;
        pconfig.properties.push_back(pspec);
    }

    Project newEmptyProject;
    newEmptyProject.setConfig(pconfig);

    std::list<ProjectSummary> pList;
    getProjects(u, pList);

    sendHttpHeader200(req);
    ContextParameters ctx = ContextParameters(req, u, newEmptyProject.getProjectParameters());
    RHtml::printProjectConfig(ctx, pList);
}

void httpGetProjectConfig(const RequestContext *req, const Project &p, const User &u)
{
    if (u.getRole(p.getName()) != ROLE_ADMIN && ! u.superadmin) return sendHttpHeader403(req);

    std::list<ProjectSummary> pList;
    getProjects(u, pList);

    // handle taking/copying config from another project
    const Project *pPtr = 0;
    std::string q = req->getQueryString();
    std::string copyConfigFrom = getFirstParamFromQueryString(q, "copy-config-from");
    ProjectConfig alternateConfigInstance;
    ProjectConfig *alternateConfig = NULL;
    if (!copyConfigFrom.empty()) {
        // initiate a new config, copied from this one
        pPtr = Database::Db.getProject(copyConfigFrom);
        if (pPtr) {
            alternateConfigInstance = pPtr->getConfig();
            alternateConfig = &alternateConfigInstance;
        }
    }

    sendHttpHeader200(req);
    ContextParameters ctx = ContextParameters(req, u, p.getProjectParameters());
    RHtml::printProjectConfig(ctx, pList, alternateConfig);
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

        } else if  (pSpec.type == F_ASSOCIATION && !pSpec.reverseLabel.empty()) {
            line.push_back("-reverseLabel");
            line.push_back(pSpec.reverseLabel);

        } else if  (pSpec.type == F_TEXTAREA2 && !pSpec.ta2Template.empty()) {
            line.push_back("-template");
            line.push_back(pSpec.ta2Template);
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

void consolidateIssueNumberingPolicy(std::list<std::list<std::string> > &tokens,
                                     const std::string &value)
{
    // numbering is global if checkox is "on"
    if (value != "on") return;

    std::list<std::string> line;
    line.push_back(KEY_NUMBER_ISSUES);
    line.push_back("global");
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
        else if (key == "textarea2Template") pSpec.ta2Template = value;
        else if (key == "reverseAssociation") pSpec.reverseLabel = value;
        else if (key == "tagDisplay") tagDisplay = value;
        else if (key == "propertyName" || key == "tagName" || key == "sm_numberIssues") {

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
            else if (key == "sm_numberIssues") consolidateIssueNumberingPolicy(tokens, value);

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

void httpPostProjectConfig(const RequestContext *req, Project &p, const User &u)
{
    enum Role role = u.getRole(p.getName());
    if (role != ROLE_ADMIN && ! u.superadmin) {
        sendHttpHeader403(req);
        return;
    }

    std::string postData;

    ContentType ct = getContentType(req);

    if (ct != CT_WWW_FORM_URLENCODED) {
        LOG_ERROR("httpPostProjectConfig: invalid content-type '%s'", getContentTypeString(req));
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
        Project *newProject = Database::createProject(projectName, u.username);
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

void httpPostNewProject(const RequestContext *req, const User &u)
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
std::string getRedirectionToIssue(const Project &p, std::vector<IssueCopy> &issueList,
                    const std::string &issueId, IssueNavigation direction, const std::string &qs)
{
    // get current issue
    std::vector<IssueCopy>::const_iterator i;
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
void httpIssuesAccrossProjects(const RequestContext *req, const User &u, const std::string &uri, const std::list<Project *> &projects)
{
    if (uri != "issues") return sendHttpHeader404(req);

    // get query string parameters
    std::string q = req->getQueryString();
    PredefinedView v = PredefinedView::loadFromQueryString(q); // unamed view, used as handle on the viewing parameters

    std::vector<IssueCopy> issues;

    // foreach project, get list of issues
    std::list<Project *>::const_iterator p;
    FOREACH(p, projects) {
        // replace user "me" if any...
        PredefinedView vcopy = v;
        replaceUserMe(vcopy.filterin, **p, u.username);
        replaceUserMe(vcopy.filterout, **p, u.username);
        if (vcopy.search == "me") vcopy.search = u.username;

        // search, without sorting
        (*p)->search(vcopy.search.c_str(), vcopy.filterin, vcopy.filterout, 0, issues);
    }

    // sort
    std::list<std::pair<bool, std::string> > sSpec = parseSortingSpec(v.sort.c_str());
    IssueCopy::sort(issues, sSpec);

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

void httpSendIssueList(const RequestContext *req, const Project &p,
                       const User &u, const std::vector<IssueCopy> &issueList)
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
        RCsv::printIssueList(req, issueList, cols);
    } else {
        ContextParameters ctx = ContextParameters(req, u, p.getProjectParameters());
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
/** Get a list of entries
  *
  * Query-String: ?sort=-ctime&limit=20
  */
void httpGetListOfEntries(const RequestContext *req, const Project &p, const User &u)
{
    std::string q = req->getQueryString();
    PredefinedView v = PredefinedView::loadFromQueryString(q); // unamed view, used as handler on the viewing parameters

    std::vector<Entry> entries;
    p.searchEntries(v.sort.c_str(), entries, v.limit);

    enum RenderingFormat format = getFormat(req);

    sendHttpHeader200(req);

    if (format == RENDERING_JSON) {
        RJson::printEntryList(req, entries);

    } else {
        ContextParameters ctx = ContextParameters(req, u, p.getProjectParameters());
        //ctx.filterin = v.filterin; not available for entries
        //ctx.filterout = v.filterout; not available for entries
        //ctx.search = v.search; not available for entries
        ctx.sort = v.sort;

        RHtml::printPageEntries(ctx, entries);
    }
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
void httpGetListOfIssues(const RequestContext *req, const Project &p, const User &u, const std::string &snapshot)
{
    std::vector<IssueCopy> issueList;
    std::map<std::string, std::list<std::string> > emptyFilter;

    p.search(0, emptyFilter, emptyFilter, 0, issueList);

    time_t datetime = atoi(snapshot.c_str());

    // for each returned issue:
    // - update the issue according to the snapshot datetime
    // - if the ctime is after the snapshot datetime, then remove the issue
    std::vector<IssueCopy>::iterator i = issueList.begin();
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

void httpGetListOfIssues(const RequestContext *req, const Project &p, const User &u)
{
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

    std::vector<IssueCopy> issueList;
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
        std::string cookie = getServerCookie(req, COOKIE_ORIGIN_VIEW, newQs, -1);
        sendHttpRedirect(req, redirectionUrl.c_str(), cookie.c_str());
        return;
    }

    httpSendIssueList(req, p, u, issueList);
}

void httpGetProject(const RequestContext *req, const Project &p, const User &u)
{
    // case of HTML web client

    // redirect to list of issues
    std::string url = "/";
    url += p.getUrlName() + "/issues?defaultView=1";
    sendHttpRedirect(req, url.c_str(), 0);
}


void httpGetNewIssueForm(const RequestContext *req, Project &p, const User &u)
{
    enum Role role = u.getRole(p.getName());
    if (role != ROLE_RW && role != ROLE_ADMIN) {
        sendHttpHeader403(req);
        return;
    }

    sendHttpHeader200(req);

    ContextParameters ctx = ContextParameters(req, u, p.getProjectParameters());

    // only HTML format is needed
    RHtml::printPageNewIssue(ctx);
}

void httpGetView(const RequestContext *req, Project &p, const std::string &view, const User &u)
{
    LOG_FUNC();
    sendHttpHeader200(req);

    if (view.empty()) {
        // print the list of all views
        ContextParameters ctx = ContextParameters(req, u, p.getProjectParameters());
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
        ContextParameters ctx = ContextParameters(req, u, p.getProjectParameters());
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
void httpGetObject(const RequestContext *req, const Project &p, std::string object)
{
    if (object.empty()) return;

    std::string id = popToken(object, '/');
    std::string filename = object;

    // TODO check if project needs read-only locking
    GitObject o(p.getPathEntries(), id);
    int size = o.getSize();
    if (size < 0) {
        sendHttpHeader404(req);
        return;
    }

    // send header
    int err = o.open();
    if (err) {
        sendHttpHeader500(req, "Error while opening subprocess");
        return;
    }

    const char *mimeType = mg_get_builtin_mime_type(filename.c_str());
    sendHttpHeader200(req);
    req->printf("Etag: %s\r\n", id.c_str());
    req->printf("Content-Type: %s\r\n", mimeType);
    req->printf("Content-Length: %ld\r\n", L(size));
    req->printf("\r\n");
    const int BUF_SIZ = 4096;
    char buffer[BUF_SIZ];
    while (1)
    {
        int n = o.read(buffer, BUF_SIZ);
        if (n < 0) {
            // error
            LOG_ERROR("Error while sending object %s (%s)", id.c_str(), p.getPath().c_str());
            // TODO how to deal with this server side error ?
            break;
        }
        if (n == 0) break; // end of file
        req->write(buffer, n);
    }
}


void httpGetStat(const RequestContext *req, Project &p, const User &u)
{
    sendHttpHeader200(req);
    ContextParameters ctx = ContextParameters(req, u, p.getProjectParameters());
    // only HTML format is supported
    RHtml::printPageStat(ctx, u);
}


/** Handle the POST of a view
  *
  * All users can post these as an advanced search (with no name).
  * But only admin users can post predefined views (with a name).
  */
void httpPostView(const RequestContext *req, Project &p, const std::string &name, const User &u)
{
    LOG_FUNC();

    std::string postData;
    ContentType ct = getContentType(req);
    if (ct != CT_WWW_FORM_URLENCODED) {
        // bad request
        sendHttpHeader400(req, "");
        return;
    }

    // application/x-www-form-urlencoded
    // post_data is "var1=val1&var2=val2...".
    int rc = readMgreq(req, postData, MAX_SIZE_UPLOAD);
    if (rc < 0) {
        sendHttpHeader413(req, "You tried to upload too much data. Max is 10 MB.");
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
            p.deletePredefinedView(name, u.username);
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
        int r = p.setPredefinedView(name, pv, u.username);
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
void httpPostTag(const RequestContext *req, Project &p, std::string &ref, const User &u)
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
void httpReloadProject(const RequestContext *req, Project &p, const User &u)
{
    if (!u.superadmin) {
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

int httpGetIssue(const RequestContext *req, Project &p, const std::string &issueId, const User &u)
{
    LOG_DEBUG("httpGetIssue: project=%s, issue=%s", p.getName().c_str(), issueId.c_str());
    enum RenderingFormat format = getFormat(req);

    IssueCopy issue;
    int r = p.get(issueId, issue);
    if (r < 0) {
        // issue not found or other error
        // for example because the issueId has also the entry id: id/entry
        return REQUEST_NOT_PROCESSED;

    }

    sendHttpHeader200(req);

    if (format == RENDERING_TEXT) RText::printIssue(req, issue);

    else if (format == RENDERING_ZIP) {
        ContextParameters ctx = ContextParameters(req, u, p.getProjectParameters());
        RZip::printIssue(ctx, issue);

    } else if (format == RENDERING_CSV) {
        ProjectConfig pconfig = p.getConfig();
        RCsv::printIssue(req, issue, pconfig);

    } else if (format == RENDERING_JSON) {
        RJson::printIssue(req, issue);

    } else {

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

        ContextParameters ctx = ContextParameters(req, u, p.getProjectParameters());
        std::string originView;
        int r = getFromCookie(req, COOKIE_ORIGIN_VIEW, originView);
        if (r == 0) ctx.originView = originView.c_str();

        // clear this cookie, so that getting any other issue
        // without coming from a view does not display get/next
        // (get/next use a redirection from a view, so the cookie will be set for these)
        req->printf("%s\r\n", getDeletedCookieString(req, COOKIE_ORIGIN_VIEW).c_str());
        if (ctx.originView) LOG_DEBUG("originView=%s", ctx.originView);
        RHtml::printPageIssue(ctx, issue, entryToBeAmended);
    }

    return REQUEST_COMPLETED;
}


void parseMultipartAndStoreUploadedFiles(const std::string &part, std::string boundary,
                                         std::map<std::string, std::list<std::string> > &vars,
                                         std::list<AttachedFileRef> &files,
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
            trim(value);
            vars[name].push_back(value);

        } else if (!filename.empty()) {
            // uploaded file
            LOG_DIAG("Multipart: filename=%s", filename.c_str());

            // case of a file
            // store to objects directory
            std::string fileId = project.storeFile(data, dataSize);
            if (fileId.empty()) {
                // error
                LOG_ERROR("cannot store file %s.", filename.c_str());
                // ignore this file and continue
            } else {
                AttachedFileRef file;
                file.size = dataSize;
                file.filename = getBasename(filename);
                file.id = fileId;
                files.push_back(file);
            }
        }

        offset += n;
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
void httpPostEntry(const RequestContext *req, Project &pro, const std::string & issueId, const User &u)
{
    std::string projectName = pro.getName();
    enum Role role = u.getRole(projectName);
    if (role != ROLE_RW && role != ROLE_ADMIN) {
        sendHttpHeader403(req);
        return;
    }

    std::map<std::string, std::list<std::string> > vars;
    std::string boundary;
    ContentType ct = getContentType(req, boundary);

    if (ct != CT_MULTIPART_FORM_DATA) {
        // other Content-Type
        LOG_ERROR("Content-Type '%s' not supported", getContentTypeString(req));
        sendHttpHeader400(req, "Bad Content-Type");
        return;
    }

    if (boundary.empty()) {
        LOG_ERROR("Missing boundary in multipart form data");
        sendHttpHeader400(req, "Missing boundary");
        return;
    }
    LOG_DEBUG("Boundary: %s", boundary.c_str());

    std::string postData;
    int rc = readMgreq(req, postData, MAX_SIZE_UPLOAD);
    if (rc < 0) {
        sendHttpHeader413(req, "You tried to upload too much data. Max is 10 MB.");
        return;
    }

    std::list<AttachedFileRef> files;
    parseMultipartAndStoreUploadedFiles(postData, boundary, vars, files, pro);

    std::string id = issueId;
    Entry *entry = 0;

    std::string amendedEntry = getProperty(vars, K_AMEND);

    int r = 0;
    IssueCopy oldIssue;

    if (!amendedEntry.empty()) {
        // this post is an amendment to an existing entry
        std::string newMessage = getProperty(vars, K_MESSAGE);
        r = pro.amendEntry(amendedEntry, newMessage, entry, u.username, oldIssue);
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
        }
        r = pro.addEntry(vars, files, id, entry, u.username, oldIssue);
        if (r < 0) {
            // error
            sendHttpHeader500(req, "Cannot add entry");
        }
    }

#if !defined(_WIN32)
    if (entry) {
        std::list<Recipient> recipients; // TODO populate this
        recipients = UserBase::getRecipients(projectName, entry, oldIssue);
        if (! UserBase::isLocalUserInterface()) Trigger::notifyEntry(pro, entry, oldIssue, recipients);
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
  * /_                      GET/POST   superadmin        new project
  * /public/...             GET        all               public pages, javascript, CSS, logo
  * /signin                 POST       all               sign-in
  * /users/                            superadmin        management of users for all projects
  * /users/<user>           GET/POST   user, superadmin  management of a single user
  * /users/_                GET/POST   superadmin        new user
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

    std::string userAgent = getUserAgent(req);
    if (0 == strncmp("git/", userAgent.c_str(), 4)) {
        // TODO check query string instead of user agent
        httpGitServeRequest(req);
        return REQUEST_COMPLETED; // do not let mongoose handle this request further
    }

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
    if    ( (resource == RESOURCE_PUBLIC) && (method == "GET")) return REQUEST_NOT_PROCESSED; // let mongoose handle it
    else if (resource == RESOURCE_PUBLIC) return sendHttpHeader400(req, "invalid method");

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

    if      ( (resource == "signout") && (method == "POST") ) httpPostSignout(req, sessionId);
    else if ( (resource == "") && (method == "GET") ) httpGetProjects(req, user);
    else if ( resource.empty() ) sendHttpHeader400(req, "void resource");
    else if ( resource[0] == '.' ) sendHttpHeader400(req, "invalid resource");
    else if ( (resource == "users") && (method == "GET") ) httpGetUser(req, user, uri);
    else if ( (resource == "users") && (method == "POST") ) httpPostUser(req, user, uri);
    else if ( (resource == "users") && (method == "DELETE") ) httpDeleteUser(req, user, uri);
    else if ( (resource == "_") && (method == "GET") ) httpGetNewProject(req, user);
    else if ( (resource == "_")&& (method == "POST") ) httpPostNewProject(req, user);
    else {
        // Get the projects given by the uri.
        // We need to concatenate back 'resource' and 'uri', as resource was
        // previously popped from the URI.
        uri = resource + "/" + uri;
        std::list<Project *> projects;

        // Prepare the list of the projects where the user has reading permission
        std::list<std::string> projectsNames;
        if (user.superadmin) projectsNames = Database::getProjects();
        else projectsNames = user.getProjectsNames();

        // Look for the projects that match the URI
        Database::lookupProjectsWildcard(uri, projectsNames, projects);

        if (projects.size() == 0) {
            // No project found. Bad request or permission denied.
            // Send same error as for existing project in order to prevent
            // an attacker from deducing existing projects after the http status code
            LOG_DIAG("No project for URI '%s' (for user %s)", uri.c_str(), user.username.c_str());
            return handleUnauthorizedAccess(req, true);
        }

        // At this point the user has read access to the resource inside the project(s)

        if (projects.size() > 1) {
            // multi projects
            // only page "issues" is supported
            resource = popToken(uri, '/');
            httpIssuesAccrossProjects(req, user, resource, projects);
            return REQUEST_COMPLETED;
        }

        // case of a single project
        Project *p = projects.front();

        resource = popToken(uri, '/');
        LOG_DEBUG("resource=%s", resource.c_str());
        if      ( resource.empty()       && (method == "GET") ) httpGetProject(req, *p, user);
        else if ( (resource == "issues") && (method == "GET") && uri.empty() ) httpGetListOfIssues(req, *p, user);
        else if ( (resource == "issues") && (method == "POST") ) {
            // /<p>/issues/<issue>/<entry> [/...]
            std::string issueId = popToken(uri, '/');
            std::string entryId = popToken(uri, '/');
            if (entryId.empty()) httpPostEntry(req, *p, issueId, user);
            else return sendHttpHeader400(req, "");

        } else if ( (resource == "issues") && (uri == "new") && (method == "GET") ) httpGetNewIssueForm(req, *p, user);
        else if ( (resource == "issues") && (method == "GET") ) return httpGetIssue(req, *p, uri, user);
        else if ( (resource == "entries") && (method == "GET") ) httpGetListOfEntries(req, *p, user);
        else if ( (resource == "config") && (method == "GET") ) httpGetProjectConfig(req, *p, user);
        else if ( (resource == "config") && (method == "POST") ) httpPostProjectConfig(req, *p, user);
        else if ( (resource == "views") && (method == "GET")) httpGetView(req, *p, uri, user);
        else if ( (resource == "views") && (method == "POST") ) httpPostView(req, *p, uri, user);
        else if ( (resource == "tags") && (method == "POST") ) httpPostTag(req, *p, uri, user);
        else if ( (resource == "reload") && (method == "POST") ) httpReloadProject(req, *p, user);
        else if ( (resource == RESOURCE_FILES) && (method == "GET") ) httpGetObject(req, *p, uri);
        else if ( (resource == "stat") && (method == "GET") ) httpGetStat(req, *p, user);
        else return REQUEST_NOT_PROCESSED; // other static file, let mongoose handle it

    }

    return REQUEST_COMPLETED; // do not let Mongoose handle the request
}

