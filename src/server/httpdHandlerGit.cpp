
#include "httpdHandlerGit.h"
#include "httpdUtils.h"
#include "restApi.h"
#include "utils/stringTools.h"

#define GIT_SERVICE_PUSH "git-receive-pack"
#define GIT_SERVICE_FETCH "git-upload-pack"


static httpHandleGitFetch(const RequestContext *req, const std::string &ressource)
{
    // run a CGI git-http-backend
    // SetEnv GIT_PROJECT_ROOT /var/www/git
    // SetEnv GIT_HTTP_EXPORT_ALL


}

/*
 * if /public + fetch => ok
 * if /public + push => require superadmin
 *
 * if / (.smit) + fetch|push => require superadmin
 * if /<p>/.git + fetch => require ro+
 * if /<p>/.git + push => require admin
 * if /<p>/.entries + fetch => require ro+
 * if /<p>/.entries + push => require rw+
*/
void httpGitServeRequest(const RequestContext *req)
{
    std::string q = req->getQueryString();
    std::string uri = req->getUri();
    std::string service = getFirstParamFromQueryString(q, "service");
    std::string resource = popToken(uri, '/');

    // Do not check the method. Assume GET.

    if (resource == RESOURCE_PUBLIC && service == GIT_SERVICE_FETCH) {
        // publc access, no authentication required
        return httpHandleGitFetch(req, resource);
    }
}

/** Serve a git fetch or push
  *
  * Access restriction must have been done before calling this function.
  * (this function does not verify access rights)
  */
static int gitService(const RequestContext *req, const std::string &ressource)
{
    std::string q = req->getQueryString();
    std::string service = getFirstParamFromQueryString(q, "service");



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
    return 0; //TODO
}

