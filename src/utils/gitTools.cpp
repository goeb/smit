
#include "gitTools.h"
#include "logging.h"
#include "filesystem.h"
#include "subprocess.h"
#include "stringTools.h"
#include "cgi.h"

/** Modify a file
 *
 * This stores the file in the working directory, and commit the modification.
 *
 * @param gitRepoPath directory where the branch 'master' must be checked-out.
 *
 * Intermediate sub-directories must exist.
 *
 */
int gitdbCommitMaster(const std::string &gitRepoPath, const std::string &subpath,
                      const std::string &data, const std::string &author)
{
    int err;

    std::string path = gitRepoPath + "/" + subpath;
    err = writeToFile(path, data);
    if (err) {
        return -1;
    }

    err = gitAdd(gitRepoPath, subpath);
    if (err) return -1;

    // commit
    err = gitCommit(gitRepoPath, author);
    if (err) return -1;

    return 0;
}

int gitInit(const std::string &gitRepoPath)
{
    Argv argv;

    argv.set("git", "init", gitRepoPath.c_str(), 0);
    std::string subStdout, subStderr;
    int err = Subprocess::launchSync(argv.getv(), 0, 0, 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitInit error: %s (%s)", subStderr.c_str(), gitRepoPath.c_str());
       return -1;
    }
    return 0;
}

int gitAdd(const std::string &gitRepoPath, const std::string &subpath)
{
    // add
    Argv argv;
    argv.set("git", "add", subpath.c_str(), 0);

    std::string subStdout, subStderr;
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepoPath.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitAdd: error: %d: %s", err, subStderr.c_str());
    }
    return err;
}

int gitCommit(const std::string &gitRepoPath, const std::string &author)
{
    // commit
    std::string gitAuthor = author + " <>";
    Argv argv;
    std::string subStdout, subStderr;
    argv.set("git", "commit", "-m", "modified", "--author", gitAuthor.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepoPath.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitCommit: error: %d: %s, %s", err, subStdout.c_str(), subStderr.c_str());
    }
    return err;
}

int gitAddCommitDir(const std::string &gitRepoPath, const std::string &author)
{
    int err;

    err = gitInit(gitRepoPath);
    if (err) {
        LOG_ERROR("gitAddCommitDir: cannot init '%s': %d", gitRepoPath.c_str(), err);
        return -1;
    }

    err = gitAdd(gitRepoPath, ".");
    if (err) {
        LOG_ERROR("gitAddCommitDir: cannot add '%s': %d", gitRepoPath.c_str(), err);
        return -1;
    }

    err = gitCommit(gitRepoPath, "local");
    if (err) {
        LOG_ERROR("gitAddCommitDir: cannot commit '%s': %d", gitRepoPath.c_str(), err);
        return -1;
    }
    return 0;
}

/** Get a branch reference
 *
 * @param gitRepo
 * @param branchName
 * @param[out] gitRef
 *     This is the hash of the reference, if found.
 *     It is not modified (not cleared) on error.
 * @return
 *     0 on success
 *    -1 on error
 *
 * If the given branch does not exist, the gitRef is returned empty,
 * and the return code is 0.
 *
 */
int gitShowRef(const std::string &gitRepo, std::string branchName, std::string &gitRef)
{
    Argv argv;
    std::string subStdout, subStderr;

    // git show-ref --hash <branchName>

    argv.set("git", "show-ref", "--hash", branchName.c_str(), 0);

    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        if (subStderr.empty() && subStdout.empty()) {
            // ok, no error. The branch simply does not exist.
        } else {
            LOG_ERROR("gitGetBranchRef %d: stdout=%s, stderr=%s (gitRepo=%s)",
                      err, subStdout.c_str(), subStderr.c_str(), gitRepo.c_str());
            return -1;
        }
    }
    gitRef = subStdout;
    trim(gitRef);
    return 0;
}

std::string gitGetFirstCommit(const std::string &gitRepo, const std::string &ref)
{
    Argv argv;
    std::string subStdout, subStderr;

    // git rev-list --max-parents=0 <ref>
    argv.set("git", "rev-list", "--max-parents=0", ref.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitGetFirstCommit error: ref=%s, stdout=%s, stderr=%s (gitRepo=%s)",
                 ref.c_str(), subStdout.c_str(), subStderr.c_str(), gitRepo.c_str());
        return "";
    }
    trim(subStdout);
    return subStdout;
}

int gitRenameBranch(const std::string &gitRepo, const std::string &oldName, const std::string &newName)
{
    Argv argv;
    std::string subStdout, subStderr;

    // git branch -m <old> <new>
    argv.set("git", "branch", "-m", oldName.c_str(), newName.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitRenameBranch error: old=%s, new=%s, stdout=%s, stderr=%s (gitRepo=%s)",
                 oldName.c_str(), newName.c_str(), subStdout.c_str(), subStderr.c_str(), gitRepo.c_str());
    }
    return err;
}

std::string gitGetLocalBranchThatContains(const std::string &gitRepo, const std::string &gitRef)
{
    Argv argv;
    std::string subStdout, subStderr;

    argv.set("git", "branch", "--contains", gitRef.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitGetLocalBranchThatContains error: gitRef=%s, stdout=%s, stderr=%s (gitRepo=%s)",
                 gitRef.c_str(), subStdout.c_str(), subStderr.c_str(), gitRepo.c_str());
        return "";
    }
    // parse the output
    // eg:
    // $ git branch --contains 1a3f7f720081fa
    //   * branch_x
    //     branch_y

    // keep the first line only
    std::string line = popToken(subStdout, '\n', TOK_STRICT);
    // remove the first 2 characters
    if (line.size() > 2) line = line.substr(2);
    else line = "";

    return line;
}

std::string gitMergeBase(const std::string &gitRepo, const std::string &branch1, const std::string &branch2)
{
    Argv argv;
    std::string subStdout, subStderr;

    // git merge-base <branch1> <branch2>
    argv.set("git", "merge-base", branch1.c_str(), branch2.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitMergeBase error: branch1=%s, branch2=%s, stdout=%s, stderr=%s (gitRepo=%s)",
                  branch1.c_str(), branch2.c_str(), subStdout.c_str(), subStderr.c_str(), gitRepo.c_str());
        return "";
    }

    trim(subStdout);

    return subStdout;
}

/** Git update-ref
 *
 * @param gitRepo
 * @param gitRef
 *     This must be the full path.
 *     Eg:
 *         refs/heads/<branch-name>
 *         refs/remotes/origin/<branch-name>
 * @param newValue
 *
 */
int gitUpdateRef(const std::string &gitRepo, const std::string &gitRef, const std::string &newValue)
{

    if (0 != strncmp(gitRef.c_str(), "refs/", 5)) {
        LOG_ERROR("gitUpdateRef error: wrong gitRef '%s'. Must start by 'refs/'.", gitRef.c_str());
        return -1;
    }

    Argv argv;
    std::string subStdout, subStderr;

    argv.set("git", "update-ref", gitRef.c_str(), newValue.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitUpdateRef error %d: ref=%s, new=%s, %s", err, gitRef.c_str(), newValue.c_str(), subStderr.c_str());
    }

    return err;
}

/** git rev-list
 *
 * @param[out] out
 *     List of commit objects as returned by git rev-list --reverse
 *     (in chronological order)
 * @return
 *     0 on sucess
 *    <0 on error
 */
int gitRevListReverse(const std::string &gitRepo, const std::string &base, const std::string &branch, std::string &out)
{
    Argv argv;
    std::string subStderr;

    std::string commitRange = base + ".." + branch;

    // git rev-list 2ba568abdcbf91f05d0e3539004505c0ea6a6b0e..refs/heads/issues/2
    argv.set("git", "rev-list", "--reverse", commitRange.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, out, subStderr);
    if (err) {
        LOG_ERROR("gitRevList error %d: commitRange=%s, stderr=%s", err, commitRange.c_str(), subStderr.c_str());
    }

    return err;
}


/** Create a branch
 *
 * @param options
 *    bit field
 *        GIT_OPT_FORCE: Reset <branchname> to <startpoint> if <branchname> exists already
 */
int gitBranchCreate(const std::string &gitRepo, const std::string &branch, const std::string &startPoint, int options)
{
    Argv argv;
    std::string subStdout, subStderr;

    argv.set("git", "branch", branch.c_str(), startPoint.c_str(), 0);

    if (options & GIT_OPT_FORCE) argv.append("--force", 0);

    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitBranchCreate error: %d: branch=%s, start=%s, stdout=%s, stderr=%s", err,
                  branch.c_str(), startPoint.c_str(), subStdout.c_str(), subStderr.c_str());
    }

    return err;
}

/** Remove a branch
 *
 * @param options
 *    bit field
 *        GIT_OPT_FORCE: allow deleting the branch irrespective of its merged status
 */
int gitBranchRemove(const std::string &gitRepo, const std::string &branch, int options)
{
    Argv argv;
    std::string subStdout, subStderr;

    argv.set("git", "branch", "--delete", branch.c_str(), 0);

    if (options & GIT_OPT_FORCE) argv.append("--force", 0);

    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitBranchRemove error: %d: branch=%s, stdout=%s, stderr=%s", err,
                  branch.c_str(), subStdout.c_str(), subStderr.c_str());
    }

    return err;
}


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

#define GIT_SERVICE_PUSH "git-receive-pack"
#define GIT_SERVICE_FETCH "git-upload-pack"
#define GIT_HTTP_BACKEND "/usr/lib/git-core/git-http-backend"

/** Tell if the request is a push request
 *
 * A request is a push request if (excerpt from Documentation/git-http-backend.txt):
 *     %{QUERY_STRING} service=git-receive-pack
 * OR  %{REQUEST_URI} /git-receive-pack$
 *
 */
bool gitIsPushRequest(const RequestContext *req)
{
    const std::string qs = req->getQueryString();
    std::string uri = req->getUri();
    const std::string service = getFirstParamFromQueryString(qs, "service");

    // take the last part of the uri
    std::string lastPart;
    while (!uri.empty()) lastPart = popToken(uri, '/');

    if (lastPart == "/" GIT_SERVICE_PUSH || service == GIT_SERVICE_PUSH) {
        return true;
    } else {
        return false;
    }
}

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
void gitCgiBackend(const RequestContext *req, const std::string &resourcePath, const std::string &gitRoot,
                   const std::string &username, const std::string &role)
{
    // run a CGI git-http-backend
    LOG_DIAG("gitCgiBackend: resource=%s, gitRoot=%s, username=%s, role=%s", resourcePath.c_str(),
             gitRoot.c_str(), username.c_str(), role.c_str());

    std::string varRemoteUser;
    std::string varGitRoot = "GIT_PROJECT_ROOT=" + gitRoot;
    std::string varPathInfo = "PATH_INFO=" + resourcePath;
    std::string varQueryString = "QUERY_STRING=";
    std::string varMethod = "REQUEST_METHOD=" + std::string(req->getMethod());
    std::string varGitHttpExport = "GIT_HTTP_EXPORT_ALL=";
    std::string varRole = "SMIT_ROLE=" + role;

    const char *contentType = req->getHeader("Content-Type");
    if (!contentType) contentType = "";
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
