
#include <regex.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#include "httpdHandlerGit.h"
#include "httpdUtils.h"
#include "restApi.h"
#include "global.h"
#include "repository/db.h"
#include "utils/stringTools.h"
#include "utils/argv.h"
#include "utils/subprocess.h"
#include "utils/logging.h"

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

struct HttpHeader {
    std::string name;
    std::string value;
};

/* Serve a git fetch request
 *
 * Access restriction must have been done before calling this function.
 * (this function does not verify access rights)
 *
 * @param resourcePath
 *     Eg: /project/info/refs
 *         /project/HEAD
 *
 * Examples of the smit resources:
 *
 * Resource as set in the git client      Directory on the server side
 * -------------------------------------------------------------------
 * http://server:port/project/         -> project/.git
 * http://server:port/project/.entries -> project/.entries
 * http://server:port/public/          -> public/.git
 * http://server:port/.smit            -> .smit
 *
 */
static void httpHandleGitFetch(const RequestContext *req, const std::string &resourcePath)
{
    // run a CGI git-http-backend

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

    Argv argv;
    argv.set(GIT_HTTP_BACKEND, 0);
    LOG_DIAG("httpHandleGitFetch: envp=%s", envp.toString().c_str());
    Subprocess *subp = Subprocess::launch(argv.getv(), envp.getv(), getDirname(GIT_HTTP_BACKEND).c_str());
    if (!subp) {
        LOG_ERROR("Cannot launch CGI: %s", GIT_HTTP_BACKEND);
        sendHttpHeader500(req, "cannot launch git backend");
        return;
    }

    LOG_DIAG("httpHandleGitFetch: CGI launched");

    // Send data from client to the CGI
    const int SIZ = 4096;
    char datachunk[SIZ];
    int n; // number of bytes read

    if (std::string("POST") == req->getMethod()) {
        while ( (n = req->read(datachunk, SIZ)) > 0) {
            subp->write(datachunk, n);
        }
    }

    LOG_DIAG("httpHandleGitFetch: closeStdin");
    subp->closeStdin();

    // read the header from the CGI
    std::list<HttpHeader> headers;
    std::string statusText = "OK";
    int status = 200;
    // and process some HTTP headers
    while (1) {
        std::string line = subp->getline();
        LOG_DIAG("httpHandleGitFetch: got line %s", line.c_str());

        if (line.empty() || line == "\r\n") break; // end of headers

        HttpHeader hh;
        hh.name = popToken(line, ':', TOK_TRIM_BOTH);
        trim(line); // remove surrounding blanks and ending CR or LF
        hh.value = line;

        if (hh.name == "Status") {
            // eg : Status:  404 Not Found
            // => we need to capture status=404 and statusText="Not Found"
            std::string statusStr = popToken(line, ' ', TOK_TRIM_BOTH);
            status = atoi(statusStr.c_str());
            statusText = line;
        }

        headers.push_back(hh);
    }

    LOG_DIAG("httpHandleGitFetch: send status line");

    // send the status line
    req->printf("HTTP/1.1 %d %s\r\n", status, statusText.c_str());

    // send the headers
    // process the headers from the CGI before sending to the client
    std::list<HttpHeader>::iterator header;
    FOREACH(header, headers) {
        req->printf("%s: %s\r\n", header->name.c_str(), header->value.c_str());
    }

    // end of headers
    req->write("\r\n", 2);

    // read the remaining bytes and send back to the client
    while ( (n = subp->read(datachunk, SIZ)) > 0) {
        req->write(datachunk, n);
    }

    std::string subpStderr = subp->getStderr();
    int err = subp->wait();
    if (err || !subpStderr.empty()) {
        LOG_ERROR("httpHandleGitFetch: err=%d, strerror=%s", err, subpStderr.c_str());
    }
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

static std::string extractResource(const std::string &uri)
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
            return uri.substr(0, out[0].rm_so);
        }
        ptr++;
    }
    return ""; // no match found
}

std::string base64decode(const std::string &b64string)
{
    BIO *bio, *b64;
    const int BUF_SIZE = 1024;
    char buffer[BUF_SIZE];

    int resultMaxLen = (b64string.size()/4+1)*3; // +1 to protect against malformed input (not 4-aligned)
    if (resultMaxLen > BUF_SIZE) {
        LOG_ERROR("base64decode: input size too big (%s)", b64string.c_str());
        return "";
    }

    bio = BIO_new_mem_buf(b64string.c_str(), -1);
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL); // do not use newlines to flush buffer
    int n = BIO_read(bio, buffer, b64string.size());

    BIO_free_all(bio);

    if (n <= 0) {
        LOG_ERROR("base64decode error (%s)", b64string.c_str());
        return "";
    }

    return std::string(buffer, n);
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
*/
void httpGitServeRequest(const RequestContext *req)
{
    std::string qs = req->getQueryString();
    std::string uri = req->getUri();
    std::string service = getFirstParamFromQueryString(qs, "service");

    LOG_DIAG("httpGitServeRequest: uri=%s, q=%s", uri.c_str(), qs.c_str());

    std::string resource = extractResource(uri);
    if (resource.empty()) {
        // error
        LOG_ERROR("httpGitServeRequest: cannot extract resource (%s)", uri.c_str());
        sendHttpHeader400(req, "Cannot extract resource");
        return;
    }

    std::string firstPart = popToken(resource, '/');

    if (firstPart == RESOURCE_PUBLIC && service == GIT_SERVICE_FETCH) {
        // public access, no authentication required
        return httpHandleGitFetch(req, uri);
    }

    // check authentication
    const char *authHeader = req->getHeader("Authorization");
    if (authHeader) {
        std::string auth = authHeader;
        std::string basic = popToken(auth, ' ');

        // decode base64
        auth = base64decode(auth);
        std::string username = popToken(auth, ':');
        std::string &password = auth;

        LOG_DIAG("httpGitServeRequest: username=%s, password=%s", username.c_str(), password.c_str()); //TODO remove password
    }

    // xxxxxxxxxxxx

    // authenticate
    std::string realm = "Access for: " + resource;
    sendBasicAuthenticationRequest(req, realm.c_str());

}



