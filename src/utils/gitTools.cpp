
#include "gitTools.h"
#include "logging.h"
#include "filesystem.h"
#include "subprocess.h"

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


