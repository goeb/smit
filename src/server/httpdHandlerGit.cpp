
#include <regex.h>

#include "httpdHandlerGit.h"
#include "httpdUtils.h"
#include "restApi.h"
#include "global.h"
#include "repository/db.h"
#include "utils/stringTools.h"
#include "utils/argv.h"
#include "utils/subprocess.h"
#include "utils/logging.h"
#include "utils/base64.h"
#include "user/session.h"
#include "cgi.h"

#define GIT_SERVICE_PUSH "git-receive-pack"
#define GIT_SERVICE_FETCH "git-upload-pack"
#define GIT_HTTP_BACKEND "/usr/lib/git-core/git-http-backend"

/*
    git-push
    GET /public/info/refs?service=git-receive-pack HTTP/1.1
    Host: localhost:8090
    User-Agent: git/2.11.0

    git-fetch
    GET /public/info/refs?service=git-upload-pack HTTP/1.1
    Host: localhost:8090
    User-Agent: git/2.11.0
*/

/* Serve a git fetch or push request
 *
 * Access restriction must have been done before calling this function.
 * (this function does not verify access rights)
 *
 * @param resourcePath
 *     Eg: /project/info/refs
 *         /project/HEAD
 *
 */
static void httpCgiGitBackend(const RequestContext *req, const std::string &resourcePath, const std::string &username="")
{
    // run a CGI git-http-backend

    std::string varRemoteUser;
    std::string varGitRoot = "GIT_PROJECT_ROOT=" + Database::getRootDirAbsolute();
    std::string varPathInfo = "PATH_INFO=" + resourcePath;
    std::string varQueryString = "QUERY_STRING=";
    std::string varMethod = "REQUEST_METHOD=" + std::string(req->getMethod());
    std::string varGitHttpExport = "GIT_HTTP_EXPORT_ALL=";

    const char *contentType = getContentTypeString(req);
    std::string varContentType = "CONTENT_TYPE=" + std::string(contentType);

    varQueryString += req->getQueryString();

    Argv envp;
    envp.set(varGitRoot.c_str(),
             varPathInfo.c_str(),
             varQueryString.c_str(),
             varMethod.c_str(),
             varGitHttpExport.c_str(),
             varContentType.c_str(),
             0);

    if (username.size()) {
        varRemoteUser = "REMOTE_USER=" + username;
        envp.append(varRemoteUser.c_str(), 0);
    }

    launchCgi(req, GIT_HTTP_BACKEND, envp);
    return;
}

// regular expression of valid git resources
static const char *GIT_RESOURCES[] = {
    "/HEAD$",
    "/info/refs$",
    "/objects/info/alternates$",
    "/objects/info/http-alternates$",
    "/objects/info/packs$",
    "/objects/[0-9a-f]{2}/[0-9a-f]{38}$",
    "/objects/pack/pack-[0-9a-f]{40}\\.pack$",
    "/objects/pack/pack-[0-9a-f]{40}\\.idx$",
    "/git-upload-pack$",
    "/git-receive-pack$",
    0
};

/** Extract the smit related resource by removing the git-related part
 *
 * @param uri
 *      The URI as received by the server
 *      Eg: /public/info/refs
 *
 * @param[out] gitPart
 *      Eg: /info/refs
 *
 * @return
 *      The smit resource
 *      Eg: /public
 */
static std::string extractResource(const std::string &uri, std::string &gitPart)
{
    const char **ptr = GIT_RESOURCES;
    while (*ptr) {
        regex_t re;
        regmatch_t out[1];
        int err;
        err = regcomp(&re, *ptr, REG_EXTENDED);
        if (err) {
            LOG_ERROR("Bogus regex in git resources table: %s", *ptr);
            return "";
        }
        err = regexec(&re, uri.c_str(), 1, out, 0);
        if (!err) {
            // match
            gitPart = uri.substr(out[0].rm_so);
            return uri.substr(0, out[0].rm_so);
        }
        ptr++;
    }
    return ""; // no match found
}

/* Serve a git fetch or push
 *
 * if /public + fetch => ok
 * if /public + push => require superadmin
 *
 * if / (.smit) + fetch|push => require superadmin
 * if /<p>/.git + fetch => require ro+
 * if /<p>/.git + push => require admin
 * if /<p>/.entries + fetch => require ro+
 * if /<p>/.entries + push => require rw+
 *
 */
void httpGitServeRequest(const RequestContext *req)
{
    std::string qs = req->getQueryString();
    std::string uri = req->getUri();
    std::string service = getFirstParamFromQueryString(qs, "service");

    std::string gitUriPart;
    std::string resource = extractResource(uri, gitUriPart);

    LOG_DIAG("httpGitServeRequest: uri=%s, resource=%s, q=%s", uri.c_str(), resource.c_str(), qs.c_str());

    if (resource.empty()) {
        // error
        LOG_ERROR("httpGitServeRequest: cannot extract resource (%s)", uri.c_str());
        sendHttpHeader400(req, "Cannot extract resource");
        return;
    }

    // Depending on the resource, proceed with the authentication

    std::string firstPart = popToken(resource, '/');

    if ( (firstPart == RESOURCE_PUBLIC && service == GIT_SERVICE_FETCH) ||
         (firstPart == RESOURCE_PUBLIC && gitUriPart == "/" GIT_SERVICE_FETCH) ) {
        // public access, no authentication required
        httpCgiGitBackend(req, uri);
        return;
    }

    // check authentication
    const char *authHeader = req->getHeader("Authorization");

    if (!authHeader) {
        // request authentication to the client
        std::string realm = "Access for: " + resource;
        sendBasicAuthenticationRequest(req, realm.c_str());
        return;
    }

    // check given authentication info
    std::string auth = authHeader;
    popToken(auth, ' '); // remove "Basic"

    // decode base64
    auth = base64decode(auth);
    std::string username = popToken(auth, ':');
    std::string &password = auth;

    LOG_DIAG("httpGitServeRequest: username=%s", username.c_str());

    const User *usr = SessionBase::authenticate(username, password);

    if (!usr) {
        return sendHttpHeader403(req);
    }

    // At this point we now have a valid user authenticated

    // Check if this user has sufficient privilege

    if (firstPart == RESOURCE_PUBLIC) {
        if (usr->superadmin) httpCgiGitBackend(req, uri, usr->username);
        else sendHttpHeader403(req);
        return;
    }

    if (firstPart == ".smit") {
        if (usr->superadmin) httpCgiGitBackend(req, uri, usr->username);
        else sendHttpHeader403(req);
        return;
    }

    // At this point we expect the uri to point to a project

    // TODO

    LOG_ERROR("httpGitServeRequest: not implemented");

}



