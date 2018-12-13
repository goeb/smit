
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
#include "utils/filesystem.h"
#include "utils/cgi.h"
#include "user/session.h"
#include "repository/db.h"



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
    std::string method = req->getMethod();

    LOG_INFO("httpGitServeRequest: method=%s, uri=%s, qs=%s", method.c_str(), uri.c_str(), qs.c_str());

    std::string resource = popTokenConst(uri, '/');

    if (resource.empty()) {
        // error
        LOG_ERROR("httpGitServeRequest: cannot extract resource");
        sendHttpHeader400(req, "Cannot extract resource");
        return;
    }

    bool isPushRequest = gitIsPushRequest(req); // indicate if the request is a push or a pull

    // Depending on the resource, proceed with the authentication

    if (resource == RESOURCE_PUBLIC && !isPushRequest) {
        // pull request, public access, no authentication required
        gitCgiBackend(req, uri, Database::getRootDirAbsolute(), "", "");
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

    if (resource == RESOURCE_PUBLIC) {
        // consider only the push, as the fetch has been handled earlier
        if (!usr.superadmin) return sendHttpHeader403(req);

        // no lock needed:
        // - rely on the git http backend CGI
        // - the 'public' resource contains only static files
        gitCgiBackend(req, uri, Database::getRootDirAbsolute(), usr.username, "superadmin");

    } else if (resource == ".smit") {

        if (!usr.superadmin) return sendHttpHeader403(req);

        LockMode lockmode = LOCK_READ_ONLY;
        if (isPushRequest && method == "POST") {
            lockmode = LOCK_READ_WRITE;
        }

        // lock
        // TODO refactor so that we, external entity, does not lake these locks
        LOCK_SCOPE_I(UserBase::getLocker(), lockmode, 1);
        LOCK_SCOPE_I(Database::getLocker(), lockmode, 2);

        gitCgiBackend(req, uri, Database::getRootDirAbsolute(), usr.username, "superadmin");

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
        // (assume no nested project as 'resource' is the first dirname of the URI)

        // projet name
        std::string projectName = Project::urlNameDecode(resource);
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

        pro->runGitHttpBackend(req, usr.username, rolename);

    }
}



