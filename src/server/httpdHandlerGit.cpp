
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
#include "utils/gitTools.h"
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

void diff(const std::map<std::string, EntryId> &oldTipsOfBRanches, std::map<std::string, EntryId> &newTipsOfBRanches)
{
    std::map<std::string, EntryId>::const_iterator oldit;
    std::map<std::string, EntryId>::iterator newit;
    FOREACH(oldit, oldTipsOfBRanches) {
        std::string oldbranch = oldit->first;
        newit = newTipsOfBRanches.find(oldbranch);
        if (newit == newTipsOfBRanches.end()) {
            LOG_ERROR("branch deleted after push: %s", oldbranch.c_str());

        } else if (oldit->second == newit->second) {
            // same content
            // remove if from new
            newTipsOfBRanches.erase(newit);

        } else {
            // values differ
            LOG_INFO("branch tip modified: [%s] %s -> %s", oldbranch.c_str(),
                     oldit->second.c_str(), newit->second.c_str());
            // remove if from new
            newTipsOfBRanches.erase(newit);
        }
    }
    // print what is remaining in new
    FOREACH(newit, newTipsOfBRanches) {
        LOG_INFO("new branches created: %s", newit->first.c_str());
    }
}


/* Serve a git fetch or push
 *
 * if /public + fetch => ok
 * if /public + push => require superadmin
 *
 * if / (.smit) + fetch|push => require superadmin
 * if /<p> + fetch => require ro+
 * if /<p> + push => require admin or rw+ (check done by update hook)
 *
 * When git-pushing, the client sends 2 requests:
 * 1. GET .../info/refs?service=git-receive-pack
 * 2. POST .../git-receive-pack
 * We need to update the in-memory tables only after the second request.
 *
 */
void httpGitServeRequest(const RequestContext *req)
{
    std::string qs = req->getQueryString();
    std::string uri = req->getUri();
    std::string service = getFirstParamFromQueryString(qs, "service");
    std::string method = req->getMethod();

    std::string gitUriPart;
    std::string resource = extractResource(uri, gitUriPart);

    bool isPushRequest = false; // indicate if the request is a push or a pull
    if (gitUriPart == "/" GIT_SERVICE_PUSH || service == GIT_SERVICE_PUSH) {
        isPushRequest = true;
    }

    LOG_DIAG("httpGitServeRequest: method=%s, uri=%s, resource=%s, q=%s",
             method.c_str(), uri.c_str(), resource.c_str(), qs.c_str());

    if (resource.empty()) {
        // error
        LOG_ERROR("httpGitServeRequest: cannot extract resource (%s)", uri.c_str());
        sendHttpHeader400(req, "Cannot extract resource");
        return;
    }

    // Depending on the resource, proceed with the authentication
    std::string tmpResource = resource;
    std::string firstPart = popToken(tmpResource, '/');

    if (firstPart == RESOURCE_PUBLIC && !isPushRequest) {
        // pull request, public access, no authentication required
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
    std::string username = popToken(auth, ':', TOK_STRICT);
    std::string &password = auth;

    LOG_DIAG("httpGitServeRequest: username=%s", username.c_str());

    bool authSuccess = false;
    User usr;

    if (username == BASIC_AUTH_SESSID) {
        // expect password to be the session id of an already opened session
        // (obtained via /signin)
        usr = SessionBase::getLoggedInUser(password);
        if (usr.username.empty()) authSuccess = false;
        else authSuccess = true;

    } else {
        // expect a regular user/password
        authSuccess = SessionBase::authenticate(username, password, usr);
    }

    if (!authSuccess) {
        return sendHttpHeader401(req);
    }

    // At this point we now have a valid user authenticated
    std::string rolename;

    // Check if this user has sufficient privilege

    if (firstPart == RESOURCE_PUBLIC) {
        // consider only the push, as the fetch has been handled earlier
        if (!usr.superadmin) return sendHttpHeader403(req);

        // no lock needed:
        // - rely on the git http backend CGI
        // - the 'public' resource contains only static files
        httpCgiGitBackend(req, uri, usr.username, "superadmin");

    } else if (firstPart == ".smit") {

        if (!usr.superadmin) return sendHttpHeader403(req);

        LockMode lockmode = LOCK_READ_ONLY;
        if (isPushRequest && method == "POST") {
            lockmode = LOCK_READ_WRITE;
        }

        // lock
        LOCK_SCOPE_I(UserBase::getLocker(), lockmode, 1);
        LOCK_SCOPE_I(Database::getLocker(), lockmode, 2);

        httpCgiGitBackend(req, uri, usr.username, "superadmin");

        if (isPushRequest && method == "POST") {
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
        Role role = usr.getRole(projectName);

        // The access is granted:
        // - if the user has permission RO, RW or ADMIN and requests a git pull
        // - if the user has permission RW or ADMIN and requests a git push

        if (role > ROLE_RO) return sendHttpHeader403(req);
        if (role > ROLE_RW && isPushRequest) return sendHttpHeader403(req);

        rolename = roleToString(role);

        LockMode lockmode = LOCK_READ_ONLY;
        if (isPushRequest && method == "POST") {
            lockmode = LOCK_READ_WRITE;
        }

        LOCK_SCOPE(pro->getLocker(), lockmode);

        std::map<std::string, EntryId> oldTipsOfBRanches;

        if (service == GIT_SERVICE_PUSH) {
            int err = gitGetTipsOfBranches(pro->getPath(), oldTipsOfBRanches);
            if (err) {
                LOG_ERROR("Cannot retrieve branches of %s. Abort git request", pro->getPath().c_str());
                sendHttpHeader500(req, "Cannot retrieve branches");
                return;
            }
        }

        httpCgiGitBackend(req, uri, usr.username, rolename);

        if (service == GIT_SERVICE_PUSH) {

            std::map<std::string, EntryId> newTipsOfBRanches;
            int err = gitGetTipsOfBranches(pro->getPath(), oldTipsOfBRanches);
            if (err) {
                // this is very annoying, as we are not be able to synchonize
                // our data in RAM with what has been pushed.
                LOG_ERROR("Cannot retrieve branches of %s. Cannot evaluate what has been pushed.", pro->getPath().c_str());

                // a full reload of the project is necessary
                // TODO
            }

            // Look at which branches have changed
            diff(oldTipsOfBRanches, newTipsOfBRanches);

            // TODO

            // update project entries if pushed entries

            // 3 cases :
            // - pushed entries to existing issue (branch issues/<id>)
            // - pushed entries to new issue (branch issues/new)
            // - pushed onto branch master (project config, etc.)

            // if branch issues/new exists, then:
            //    - allocate an issue id
            //    - rename the branch

            // update
            // easy way: reload full project (config and all issues)
            // problem: if a pusher client pushes 4 issues, then we would reload the full project 4 times.
            // optimized way: reload only the part that has been pushed. But how to know which branch got updated?
            //     1. keep track of tips of all branches before the httpCgiGitBackend
            //     2. run httpCgiGitBackend
            //     3. check the tips. normally at most 1 has changed. Update the info of this one only.
        }

        // unlock
    }
}



