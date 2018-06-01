
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
int gitGetBranchRef(const std::string &gitRepo, std::string branchName, GitRefType type, std::string &gitRef)
{
    Argv argv;
    std::string subStdout, subStderr;

    // git show-ref --hash <branchName>

    if (type == GIT_REF_LOCAL) branchName = "refs/heads/" + branchName;
    else branchName = "refs/remotes/origin/" + branchName;

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
