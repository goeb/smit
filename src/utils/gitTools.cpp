
#include "gitTools.h"
#include "logging.h"
#include "filesystem.h"
#include "subprocess.h"
#include "stringTools.h"

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

    // git branch -m <old> <new>
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

/** Return the tips of all local branches
 *
 *  The tip of a branch is the commit id to where the branch refers.
 *
 * @param[out] tips
 *     Map "branch name" -> "commit id"
 */
int gitGetTipsOfBranches(const std::string &gitRepo, std::map<std::string, std::string> &tips)
{
    Argv argv;
    std::string subStdout, subStderr;

    tips.clear(); // clear the result beforehand

    // git show-ref --heads
    argv.set("git", "show-ref", "--heads", 0);
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitGetTipsOfBranches error listing branches: %s (in %s)", subStderr.c_str(), gitRepo.c_str());
        return err;
    }

    // parse the output
    // Expected output:
    // 894f3d3f06c56bee22911054335bac9abac4bb71 refs/heads/issues/80
    // d2c6a6d8b3f81e70b2924cf5d1d6eec5375e4978 refs/heads/issues/81
    // 49324eb270b146682e87559b7de8cfdf8df3a4ad refs/heads/issues/82
    // 26ca472cfdbe1789b39ce33a2ba360351a4670df refs/heads/issues/92
    // af2a206c686eb45f358bc120e782cd23c9eb1b28 refs/heads/master

    const char refs_heads[] = "refs/heads/";
    const size_t refs_heads_len = strlen(refs_heads);

    while (!subStdout.empty()) {
        std::string line = popToken(subStdout, '\n');
        std::string hash = popToken(line, ' ');
        // now line contains "refs/heads/...".
        // remove the refs/heads part to keep only the branch name.
        if (line.size() < refs_heads_len || 0 != strncmp(line.c_str(), refs_heads, refs_heads_len) ) {
            // unexpected format
            LOG_ERROR("gitGetTipsOfBranches: git show-ref returned unexpected format: hash=%s, line=%s (in %s)",
                      hash.c_str(), line.c_str(), gitRepo.c_str());

            // ignore this line, and do not report the error for the return status
            continue;
        }
        std::string branchName = line.substr(refs_heads_len);
        tips[branchName] = hash;
    }

    return 0; //success
}


