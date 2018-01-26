
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
#include "repository/db.h"

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
static void httpCgiGitBackend(const RequestContext *req, const std::string &resourcePath,
                              const std::string &username, const std::string &role)
{
    // run a CGI git-http-backend

    std::string varRemoteUser;
    std::string varGitRoot = "GIT_PROJECT_ROOT=" + Database::getRootDirAbsolute();
    std::string varPathInfo = "PATH_INFO=" + resourcePath;
    std::string varQueryString = "QUERY_STRING=";
    std::string varMethod = "REQUEST_METHOD=" + std::string(req->getMethod());
    std::string varGitHttpExport = "GIT_HTTP_EXPORT_ALL=";
    std::string varRole = "SMIT_ROLE=" + role;

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
             varRole.c_str(), // the pre-receive hook shall take this into account
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

    LOG_DIAG("httpGitServeRequest: method=%s, uri=%s, resource=%s, q=%s",
             req->getMethod(), uri.c_str(), resource.c_str(), qs.c_str());

    if (resource.empty()) {
        // error
        LOG_ERROR("httpGitServeRequest: cannot extract resource (%s)", uri.c_str());
        sendHttpHeader400(req, "Cannot extract resource");
        return;
    }

    // Depending on the resource, proceed with the authentication
    std::string tmpResource = resource;
    std::string firstPart = popToken(tmpResource, '/');

    if ( (firstPart == RESOURCE_PUBLIC && service == GIT_SERVICE_FETCH) ||
         (firstPart == RESOURCE_PUBLIC && gitUriPart == "/" GIT_SERVICE_FETCH) ) {
        // public access, no authentication required
        httpCgiGitBackend(req, uri, "", "");
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
        return sendHttpHeader401(req);
    }

    // At this point we now have a valid user authenticated
    std::string rolename;

    // Check if this user has sufficient privilege

    if (firstPart == RESOURCE_PUBLIC) {
        // consider only the push, as the fetch has been handled earlier
        if (!usr->superadmin) return sendHttpHeader403(req);

        // no lock needed. Rely on the git http backend CGI
        httpCgiGitBackend(req, uri, usr->username, "superadmin");

    } else if (firstPart == ".smit") {
        if (!usr->superadmin) return sendHttpHeader403(req);

        // lock
        LOCK_SCOPE_I(UserBase::getLocker(), LOCK_READ_WRITE, 1);
        LOCK_SCOPE_I(Database::getLocker(), LOCK_READ_WRITE, 2);

        httpCgiGitBackend(req, uri, usr->username, "superadmin");

        if (gitUriPart == "/" GIT_SERVICE_PUSH) {
            // update users and repo config
            int err;
            err = Database::Db.reloadConfig();
            if (err) {
                LOG_ERROR("Cannot reload smit config after push");
            }
            err = UserBase::hotReload_NL();
            if (err) {
                LOG_ERROR("Cannot reload smit users config after push");
            }
        }

        // unlock, automacially done when leaving the scope

    } else {
        // At this point we expect the uri to point to a project

        // projet name
        std::string projectName = resource;
        trim(projectName, "/");

        Project *pro = Database::getProject(projectName);
        if (!pro) {
            LOG_ERROR("httpGitServeRequest: invalid project name: %s", projectName.c_str());
            return sendHttpHeader403(req);
        }

        // get user role on this project
        Role role = usr->getRole(projectName);

        // The access is granted:
        // - if the user has permission RO, RW or ADMIN and requests a git pull
        // - if the user has permission RW or ADMIN and requests a git push

        if (role > ROLE_RO) return sendHttpHeader403(req);
        if (role > ROLE_RW && service == GIT_SERVICE_PUSH) return sendHttpHeader403(req);

        rolename = roleToString(role);

        // lock TODO
        //pro->

        httpCgiGitBackend(req, uri, usr->username, rolename);

        if (service == GIT_SERVICE_PUSH) {
            // update project entries if pushed entries
            //TODO
        }

        // unlock
    }
}



