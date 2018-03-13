/*   Small Issue Tracker
 *   Copyright (C) 2015 Frederic Hoerni
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

#include <iostream>
#include <string>
#include <list>
#include <algorithm>
#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <getopt.h>
#include <fstream>
#include <sstream>
#include <iostream>

#include "clone.h"
#include "global.h"
#include "utils/stringTools.h"
#include "utils/filesystem.h"
#include "utils/logging.h"
#include "utils/gitTools.h"
#include "user/session.h"
#include "project/gitdb.h"
#include "mg_win32.h"
#include "console.h"
#include "repository/db.h"
#include "httpClient.h"
#include "Args.h"
#include "restApi.h"
#include "project/Project.h"
#include "localClient.h"

#define LOG_CLI(...) { printf(__VA_ARGS__); printf("\n"); fflush(stdout);}

/** smit distributed functions
  *     smit clone .... : download a local copy of a smit repository
  *     smit pull ....  : download latest changes from a smit server
  *     smit push ....  : upload local changes to a smit server
  *
  * 1. Conflicts on existing issues
  *
  * These conflicts occur typically when a user modified an issue on the server and
  * another user modified the same issue on his/her local clone.
  * These conflicts are detected during a pushing, but they can be resolved only
  * by a pulling.
  * These conflicts are resolved either automatically as follows:
  *
  *   TODO
  *
  * 2. Conflicts on new issues
  * These conflicts occur typically when a user creates a new issue on the server and
  * another user creates a new issue on his/her local clone, and both new issues get
  * the same issue id.
  *
  * These conflicts within a same project are resolved by pulling: the local
  * issue of the clone is renamed. For example issue 123 will be renamed issue 124.
  *
  * Some conflicts are also resolved when pushing. Newly pushed issues may
  * be renamed by the server if another issue with the same id exists on a
  * different project (when both projects use global numbering).
  *
  */

// resolve strategies for pulling conflicts
enum MergeStrategy { MERGE_KEEP_LOCAL, MERGE_DROP_LOCAL, MERGE_INTERACTIVE};
struct PullContext {
    std::string rooturl; // eg: http://example.com:8090/
    std::string localRepo; // path to local repository
    HttpClientContext httpCtx; // session identifier
    MergeStrategy mergeStrategy; // for pulling only, not cloning

    inline std::string getTmpDir() const { return localRepo + "/.tmp"; }
};

static void terminateSession()
{
    curl_global_cleanup();
}

int testSessid(const std::string url, const HttpClientContext &ctx)
{
    LOG_DEBUG("testSessid(%s, %s)", url.c_str(), ctx.cookieSessid.c_str());

    HttpRequest hr(ctx);
    hr.setUrl(url + "/");
    int status = hr.test();
    LOG_DEBUG("status = %d", status);

    if (status == 200) return 0;
    return -1;
}

static Argv getGitEnv(const std::string &smitRepository)
{
    std::string varGitConfig = "GIT_CONFIG=" + smitRepository + "/" PATH_GIT_CONFIG;
    Argv envp;
    envp.set(varGitConfig.c_str(), 0);
    return envp;
}

static int alignIssueBranches(const std::string &projectPath)
{
    int err;

    // create local branches to track remote ones (issues/*)
    GitIssueList ilist;
    int ret = ilist.open(projectPath, "origin");
    if (ret) {
        LOG_ERROR("Cannot load issues of project (%s)", projectPath.c_str());
        return -1;
    }

    while (1) {
        std::string issueId = ilist.getNext();
        if (issueId.empty()) break; // reached the end

        // create a local branch
        Argv argv;
        std::string subStdout, subStderr;
        std::string localBranch = BRANCH_PREFIX_ISSUES +  issueId;
        std::string remoteBranch = "origin/" BRANCH_PREFIX_ISSUES + issueId;

        argv.set("git", "branch", localBranch.c_str(), remoteBranch.c_str(), 0);
        err = Subprocess::launchSync(argv.getv(), 0, projectPath.c_str(), 0, 0, subStdout, subStderr);
        if (err) {
            LOG_ERROR("alignIssueBranches: error: %d: stdout=%s, stderr=%s", err, subStdout.c_str(), subStderr.c_str());
            break;
        }
    }
    ilist.close();
    return err;
}

static int gitClone(const std::string &remote, const std::string &smitRepo, const std::string &subpath)
{
    Argv argv;
    std::string subStdout, subStderr;
    std::string into = smitRepo + "/" + subpath; // where to clone into
    std::string configOpt = "credential.helper=store --file " + smitRepo + "/" PATH_GIT_CREDENTIAL;

    argv.set("git", "clone", remote.c_str(), into.c_str(), "--config", configOpt.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, 0, 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitClone: error: %d: stdout=%s, stderr=%s", err, subStdout.c_str(), subStderr.c_str());
        return err;
    }

    LOG_DIAG("gitClone: stdout=%s, stderr=%s", subStdout.c_str(), subStderr.c_str());

    // create local branches to track remote ones
    err = alignIssueBranches(into);

    return err;
}

struct Permissions {
    std::map<std::string, Role> byProject; // mapping project name / role
    bool superadmin;
    Permissions(): superadmin(false) {}
};

/** Read the permissions from the remote server and update locally
 */
static Permissions updatePermissions(const HttpClientContext &ctx, const std::string &rooturl, const std::string &localRepo,
                                     const std::string username)
{
    Permissions permissions;

    // get the list of remote projects
    HttpRequest hr(ctx);
    hr.setUrl(rooturl + "/?format=text");
    hr.getRequestLines();

    // Lines are in the format: <permission> <project name>
    // Except the "superadmin" line, if any.
    std::list<std::string>::iterator line;

    FOREACH(line, hr.lines) {
        if ( (*line) == "superadmin") {
            permissions.superadmin = true;

        } else {
            std::string roleStr = popToken(*line, ' ');
            std::string projectName = *line;

            Role role = stringToRole(roleStr);

            if (role != ROLE_NONE) permissions.byProject[projectName] = role;
        }
    }

    // Update the local file that stores the permissions (used by smit ui)
    std::string data = User::serializePermissions(username, permissions.superadmin, permissions.byProject);
    std::string filePermissions = localRepo + "/" PATH_LOCAL_PERM;
    int err = writeToFile(filePermissions, data);
    if (err) {
        LOG_ERROR("Cannot store local permissions: %s", filePermissions.c_str());
    }
    return permissions;
}

static int cloneAll(const HttpClientContext &ctx, const std::string &rooturl, const std::string &localdir, const Permissions &perms)
{
    int err;

    // clone /public
    LOG_CLI("Cloning 'public'...");
    err = gitClone(rooturl+"/public", localdir, "public");
    if (err) {
        LOG_ERROR("Abort.");
        exit(1);
    }

    // clone the projects if the permissions allow it
    std::map<std::string, Role>::const_iterator perm;
    FOREACH(perm, perms.byProject) {
        std::string projectName = perm->first;
        Role role = perm->second;
        if (role <= ROLE_RO) {
            // do clone
            LOG_CLI("Cloning '%s'...", projectName.c_str());
            LOG_DIAG("role=%s", roleToString(role).c_str());
            std::string resource = "/" + Project::urlNameEncode(projectName);
            std::string projectDir = resource;
            err = gitClone(rooturl+"/"+resource, localdir, projectDir);
            if (err) {
                LOG_ERROR("Abort.");
                exit(1);
            }
        }
    }

    //

    // clone /.smit if permission granted
    if (perms.superadmin) {
        LOG_CLI("Cloning '.smit'...");
        err = gitClone(rooturl+"/.smit", localdir, ".smit");
        if (err) {
            LOG_ERROR("Abort.");
            exit(1);
        }
    }

    return 0;
}

static int pullAll()
{
    // pull --rebase /public
    // pull --rebase /.smit if permission granted
    // get list of projects GET /?format=text
    // for each project, pull --rebase

    return 0;
}

static int pushAll()
{
    // push /public, if permission granted
    // push /.smit if permission granted
    // get list of projects GET /?format=text
    // for each project, push (if permission granted)

    return 0;
}

/** Get all the projects that we have read-access to
  */
int getProjects(const std::string &rooturl, const std::string &destdir, const HttpClientContext &ctx)
{
    LOG_DEBUG("getProjects(%s, %s, %s)", rooturl.c_str(), destdir.c_str(), ctx.cookieSessid.c_str());

    PullContext cloneCtx;
    cloneCtx.httpCtx = ctx;
    cloneCtx.rooturl = rooturl;
    cloneCtx.localRepo = destdir;
    cloneCtx.mergeStrategy = MERGE_KEEP_LOCAL; // not used here (brut cloning)
    LOG_ERROR("Cloning All Projects TODO...\n");

    return -1;
}


static int pullPublic(const PullContext &ctx)
{
    // TODO
    LOG_ERROR("pullPublic: not implemented");
    return -1;
}


/** Pull issues of all local projects
  * - pull entries
  * - pull files
  *
  * @param all
  *     Also pull: tags, public, templates
  */
int pullProjects(const PullContext &pullCtx, bool all)
{
    // Load all local projects
    int r = dbLoad(pullCtx.localRepo.c_str());
    if (r < 0) {
        fprintf(stderr, "Cannot load repository '%s'. Aborting.", pullCtx.localRepo.c_str());
        exit(1);
    }

    // get the list of remote projects
    HttpRequest hr(pullCtx.httpCtx);
    hr.setUrl(pullCtx.rooturl + "/?format=text");
    hr.getRequestLines();

    std::list<std::string>::iterator projectName;
    FOREACH(projectName, hr.lines) {
        if ((*projectName) == "public") continue;
        if ((*projectName) == PATH_REPO) continue;
        if (projectName->empty()) continue;

        // project name is mangled
        Project *p = Database::Db.getProject(*projectName);
        if (!p) {
            // the remote project was not locally cloned
            // do cloning now
            std::string resource = "/" + Project::urlNameEncode(*projectName);
            std::string destLocal = pullCtx.localRepo + "/" + *projectName;
            LOG_ERROR("Pulling new project: TODO");
        } else {
            LOG_ERROR("Pulling new project: TODO");
        }
    }
    if (all) {

        LOG_CLI("Pulling 'public'\n");
        r = pullPublic(pullCtx);
        if (r < 0) return r;
    }
    return 0; //ok
}

std::string getSmitDir(const std::string &dir)
{
    return dir + "/" SMIT_LOCAL;
}

void createSmitDir(const std::string &dir)
{
    LOG_DEBUG("createSmitDir(%s)...", dir.c_str());

    std::string smitDir = getSmitDir(dir);

    if (isDir(smitDir)) return; // Already exists

    int r = mkdir(smitDir);
    if (r != 0) {
        fprintf(stderr, "Cannot create directory '%s': %s\n", getSmitDir(dir).c_str(), strerror(errno));
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

/** Sign-in and return the sessid cookie
  *
  * @return
  *    The format is the cookie format: key=value
  */
std::string signin(const std::string &rooturl, const std::string &user,
                   const std::string &passwd, const HttpClientContext &ctx)
{
    HttpRequest hr(ctx);

    hr.setUrl(rooturl + "/signin?format=text");

    // specify the POST data
    std::string params;
    params += "username=";
    params += urlEncode(user);
    params += "&password=";
    params += urlEncode(passwd);
    hr.post(params);

    // Get the sessiond id
    // Look for the cookie smit-sessid-*
    std::map<std::string, Cookie>::iterator c;
    FOREACH(c, hr.cookies) {
        std::string cookie = c->first; // name of the cookie
        if (0 == cookie.compare(0, strlen(COOKIE_SESSID_PREFIX), COOKIE_SESSID_PREFIX)) {
            // Got it
            cookie += "=" + c->second.value;
            return cookie;
        }
    }

    return "";
}

void parseUrl(std::string url, std::string &scheme, std::string &host, std::string &port, std::string &resource)
{
    scheme = popToken(url, ':');
    std::string hostAndPort = popToken(url, '/');
    host = popToken(hostAndPort, ':');
    port = hostAndPort;
    resource = url;
}

/** Store the cookie of the sessid in .smit/sessid
  */
void storeSessid(const std::string &dir, const std::string &sessid)
{
    LOG_DEBUG("storeSessid(%s, %s)...", dir.c_str(), sessid.c_str());
    std::string path = dir + "/" PATH_SESSID;
    int r = writeToFile(path, sessid + "\n");
    if (r < 0) {
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

/** Load the cookie of the sessid
  */
std::string loadSessid(const std::string &dir)
{
    std::string path = dir + "/" PATH_SESSID;
    std::string sessid;
    loadFile(path.c_str(), sessid);
    trim(sessid);

    return sessid;
}

const char* SCHEMES[] = { "https://", "http://", 0 };

/** Store the session id in the git-credential-store format
 *
 *  Eg: https://user:pass@example.com
 */
static void storeGitCredential(const std::string &dir, const std::string &url, const std::string &username, const std::string &passwd)
{
    std::string credential;

    // insert the user:pass in the url
    const char **ptr = SCHEMES;
    while (*ptr) {
        if (0 == strncmp(url.c_str(), *ptr, strlen(*ptr))) {
            credential = *ptr;
            credential += username + ":" + passwd + "@" + url.substr(strlen(*ptr));
            break;
        }
        ptr++;
    }

    if (credential.empty()) {
        LOG_ERROR("Unsupported scheme (%s). Must be http:// or https://", url.c_str());
        exit(1);
    }

    std::string path = dir + "/" PATH_GIT_CREDENTIAL;
    int r = writeToFile(path, credential + "\n");
    if (r < 0) {
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

/** Load the root url from the git-credential-store
 *
 *  Format in the file is for instance: https://user:pass@example.com
 *
 * @return
 *     https://example.com
 */
static std::string loadUrl(const std::string &dir)
{
    std::string path = dir + "/" PATH_GIT_CREDENTIAL;
    std::string data;
    std::string url;

    int err = loadFile(path, data);
    if (err) {
        fprintf(stderr, "Abort.\n");
        exit(1);
    }

    // take the first line
    std::string line = popToken(data, '\n');

    const char **ptr = SCHEMES;
    while (*ptr) {
        if (0 == strncmp(line.c_str(), *ptr, strlen(*ptr))) {
            url = *ptr;
            popToken(line, '@');
            url += line;
            break;
        }
        ptr++;
    }
    if (url.empty()) {
        LOG_ERROR("Cannot load url from '%s': malformed", path.c_str());
        exit(1);
    }

    return url;
}

Args *setupCloneOptions()
{
    Args *args = new Args();
    args->setDescription("Clones a repository into a newly created directory.\n"
                         "\n"
                         "Args:\n"
                         "  <url>        The repository to clone from.\n"
                         "  <directory>  The name of a new directory to clone into.\n"
                         "\n"
                         "Example:\n"
                         "  smit clone http://example.com:8090 localDir\n"
                         );
    args->setUsage("smit clone [options] <url> <directory>");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setNonOptionLimit(2);
    return args;
}

int helpClone(const Args *args)
{
    if (!args) args = setupCloneOptions();
    args->usage(true);
    return 1;
}

int cmdClone(int argc, char **argv)
{
    HttpClientContext httpCtx;
    Args *args = setupCloneOptions();
    args->parse(argc-1, argv+1);
    const char *pUsername = args->get("user");
    const char *pPasswd = args->get("passwd");
    if (args->get("insecure")) httpCtx.tlsInsecure = true;
    httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DIAG);
    } else {
        setLoggingLevel(LL_ERROR);
    }

    // manage non-option ARGV elements
    const char *url = args->pop();
    const char *localdir = args->pop();
    if (!url || !localdir) {
        LOG_CLI("Missing argument.\n");
        exit(1);
    }

    setLoggingOption(LO_CLI);

    if (fileExists(localdir)) {
        LOG_CLI("Cannot clone: '%s' already exists\n", localdir);
        exit(1);
    }

    std::string username;
    std::string passwd;

    if (!pUsername) username = getString("Username: ", false);
    else username = pUsername;
    if (!pPasswd) passwd = getString("Password: ", true);
    else passwd = pPasswd;

    curl_global_init(CURL_GLOBAL_ALL);

    httpCtx.cookieSessid = signin(url, username, passwd, httpCtx);

    if (httpCtx.cookieSessid.empty()) {
        fprintf(stderr, "Authentication failed\n");
        exit(1);
    }

    int r = mkdir(localdir);
    if (r != 0) {
        LOG_CLI("Cannot create directory '%s': %s\n", localdir, strerror(errno));
        exit(1);
    }

    // create persistent configuration of the local clone
    createSmitDir(localdir);
    storeSessid(localdir, httpCtx.cookieSessid);
    storeUsername(localdir, username);
    std::string sessid = httpCtx.cookieSessid;
    popToken(sessid, '='); // remove the <key>= part
    storeGitCredential(localdir, url, BASIC_AUTH_SESSID, sessid);

    //
    Permissions perms = updatePermissions(httpCtx, url, localdir, username);

    r = cloneAll(httpCtx, url, localdir, perms);
    if (r < 0) {
        fprintf(stderr, "Clone failed. Abort.\n");
        exit(1);
    }

    terminateSession();

    return 0;
}

/** Sign-in or reuse existing cookie
  *
  * @param username
  *    Optional. If not given, try to use the cookie.
  *
  *
  * @param ctx IN/OUT
  */
static int establishSession(const char *dir, const char *username, const char *password, PullContext &ctx, bool store)
{
    curl_global_init(CURL_GLOBAL_ALL);

    if (ctx.rooturl.empty()) {
        // get the remote root url from local configuration file
        ctx.rooturl = loadUrl(dir);
        if (ctx.rooturl.empty()) {
            LOG_CLI("Cannot get remote url.\n");
            return -1;
        }
    }

    bool redoSigin = false;

    std::string user;
    if (!username) {
        // check if the sessid is valid
        ctx.httpCtx.cookieSessid = loadSessid(dir);
        int r = testSessid(ctx.rooturl, ctx.httpCtx);

        if (r < 0) {
            // failed (session may have expired), then prompt for user name/password
            LOG_CLI("Session not valid. You must sign-in again.\n");
            user = getString("Username: ", false);

            // and redo the signing-in
            redoSigin = true;
        }

    } else {
        // do the signing-in with the given username / password
        redoSigin = true;
        user = username;
    }

    if (redoSigin) {

        std::string pass;
        if (!password) pass = getString("Password: ", true);
        else pass = password;

        ctx.httpCtx.cookieSessid = signin(ctx.rooturl, user, pass, ctx.httpCtx);
        if (!ctx.httpCtx.cookieSessid.empty() && store) {
            storeSessid(dir, ctx.httpCtx.cookieSessid);
            storeGitCredential(dir, ctx.rooturl, user, pass);
        }
    }

    LOG_DEBUG("cookieSessid: %s", ctx.httpCtx.cookieSessid.c_str());

    if (ctx.httpCtx.cookieSessid.empty()) {
        fprintf(stderr, "Authentication failed\n");
        return -1; // authentication failed
    }
    return 0;
}

Args *setupGetOptions()
{
    Args *args = new Args();
    args->setDescription("Get with format x-smit.\n");
    args->setUsage("smit get [options] <root-url> <resource>");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setOpt("use-signin-cookie", 0, "Use the cookie of the cloned repository for signin in", 0);
    args->setNonOptionLimit(2);
    return args;
}

int helpGet(const Args *args)
{
    if (!args) args = setupGetOptions();
    args->usage(true);
    return 1;
}

int cmdGet(int argc, char **argv)
{
    PullContext pullCtx;
    Args *args = setupGetOptions();
    args->parse(argc-1, argv+1);
    const char *username = args->get("user");
    const char *password = args->get("passwd");
    if (args->get("insecure")) pullCtx.httpCtx.tlsInsecure = true;
    pullCtx.httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DEBUG);
    } else {
        setLoggingLevel(LL_ERROR);
    }

    // manage non-option ARGV elements
    const char *rooturl = args->pop();
    const char *resource = args->pop();
    if (!rooturl || !resource) {
        LOG_CLI("Missing argument.\n");
        exit(1);
    }

    setLoggingOption(LO_CLI);

    pullCtx.rooturl = rooturl;
    int r = establishSession(".", username, password, pullCtx, false);
    if (r != 0) return 1;

    // do the GET
    HttpRequest hr(pullCtx.httpCtx);
    hr.setUrl(std::string(rooturl) + std::string(resource) + "?format=text");
    hr.getFileStdout();

    terminateSession();

    return 0;
}

Args *setupPullOptions()
{
    Args *args = new Args();
    args->setDescription("Incorporates changes from a remote repository into a local copy.");
    args->setUsage("smit pull [options] [<local-repository>]");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("resolve-conflict", 0,
                 "Resolve conflictual pulling automatically (no interactive choice).\n"
                 "Possible values for <policy>:\n"
                 "  keep-local : local modifications shall be kept as-is\n"
                 "  drop-local : local modifications shall be deleted"
                 ,
                 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setOpt("all", 'a', "also pull 'public' and templates", 0);
    args->setNonOptionLimit(1);
    return args;
}

int helpPull(const Args *args)
{
    if (!args) args = setupPullOptions();
    args->usage(true);
    return 1;
}

int cmdPull(int argc, char **argv)
{
    PullContext pullCtx;
    Args *args = setupPullOptions();
    args->parse(argc-1, argv+1);
    const char *username = args->get("user");
    const char *password = args->get("passwd");
    bool pullAll = false;
    if (args->get("all")) pullAll = true;
    const char *resolve = args->get("resolve-conflict");
    if (!resolve) pullCtx.mergeStrategy = MERGE_INTERACTIVE;
    else if (0 == strcmp(resolve, "keep-local")) pullCtx.mergeStrategy = MERGE_KEEP_LOCAL;
    else if (0 == strcmp(resolve, "drop-local")) pullCtx.mergeStrategy = MERGE_DROP_LOCAL;
    else {
        LOG_CLI("invalid value for --resolve-conflict\n");
        return helpPull(args);
    }
    if (args->get("insecure")) pullCtx.httpCtx.tlsInsecure = true;
    pullCtx.httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DEBUG);
    } else {
        setLoggingLevel(LL_ERROR);
    }

    // manage non-option ARGV elements
    const char *dir = args->pop();
    if (!dir) dir = "."; // default value is current directory

    setLoggingOption(LO_CLI);

    // get the remote url from local configuration file
    std::string url = loadUrl(dir);
    if (url.empty()) {
        LOG_CLI("Cannot get remote url.\n");
        exit(1);
    }

    int r = establishSession(dir, username, password, pullCtx, true);
    if (r != 0) return 1;

    // pull new entries and new attached files of all projects
    pullCtx.rooturl = url;
    pullCtx.localRepo = dir;
    r = pullProjects(pullCtx, pullAll);

    terminateSession();

    if (r < 0) return 1;
    else return 0;
}

int getHead(const PullContext &pushCtx, const std::string &url)
{
    HttpRequest hr(pushCtx.httpCtx);
    int r = hr.head(url);
    if (r !=0 ) {
        LOG_ERROR("Could not get HEAD for: %s", url.c_str());
        exit(1);
    } else if (hr.httpStatusCode >= 200 && hr.httpStatusCode < 300) {
        return 0;
    } else {
        return -1;
    }
}

int pushFile(const PullContext &pushCtx, const std::string &localFile, const std::string &url,
             int &httpStatusCode, std::string &response)
{
    HttpRequest hr(pushCtx.httpCtx);
    int r = hr.postFile(localFile, url);
    if (r == 0 && hr.lines.size()) response = hr.lines.front();
    httpStatusCode = hr.httpStatusCode;
    return r;
}



int pushProject(const PullContext &pushCtx, Project &project, bool dryRun)
{
    LOG_ERROR("Pushing project TODO\n");

    return -1;
}

int pushProjects(const PullContext &pushCtx, bool dryRun)
{
    // Load all local projects
    int r = dbLoad(pushCtx.localRepo.c_str());
    if (r < 0) {
        fprintf(stderr, "Cannot load repository '%s'. Aborting.", pushCtx.localRepo.c_str());
        exit(1);
    }

    // for each local project, push it

    Project *p = Database::Db.getNextProject(0);
    while (p) {
        pushProject(pushCtx, *p, dryRun);
        p = Database::Db.getNextProject(p);
    }
    return 0;
}

Args *setupPushOptions()
{
    Args *args = new Args();
    args->setDescription("Push local changes to a remote repository.");
    args->setUsage("smit push [options] [<local-repository>]");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("dry-run", 'n', "do not push, but show what would have been pushed", 0);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setNonOptionLimit(1);
    return args;
}

int helpPush(const Args *args)
{
    if (!args) args = setupPushOptions();
    args->usage(true);
    return 1;
}

int cmdPush(int argc, char **argv)
{
    PullContext pushCtx;

    Args *args = setupPushOptions();
    args->parse(argc-1, argv+1);

    const char *username = args->get("user");
    const char *password = args->get("passwd");
    if (args->get("insecure")) pushCtx.httpCtx.tlsInsecure = true;
    pushCtx.httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DEBUG);
    } else {
        setLoggingLevel(LL_ERROR);
    }
    bool dryRun = false;
    if (args->get("dry-run")) dryRun = true;

    // manage non-option ARGV elements
    const char *dir = args->pop();
    if (!dir) dir = "."; // default value is current directory

    const char *unexpected = args->pop();
    if (unexpected) {
        // normally already managed by the Args class
        LOG_CLI("Too many arguments.\n\n");
        return helpPush(args);
    }

    setLoggingOption(LO_CLI);
    // set log level to hide INFO logs

    int r = establishSession(dir, username, password, pushCtx, true);
    if (r != 0) return 1;

    // push new entries and new attached files of all projects
    pushCtx.localRepo = dir;
    r = pushProjects(pushCtx, dryRun);

    terminateSession();

    if (r < 0) return 1;
    else return 0;


}
