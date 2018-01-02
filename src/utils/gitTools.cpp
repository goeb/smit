
#include "gitTools.h"
#include "logging.h"
#include "filesystem.h"
#include "subprocess.h"

/** Modify a file
 *
 * This stores the file in the working directory, and commit the modification.
 * The working directory is supposed to be the checkout of the branch 'master'.
 *
 * Intermediate sub-directories must exist.
 * The file must already be tracked by git.
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

    // add
    Argv argv;
    argv.set("git", "add", subpath.c_str(), 0);

    std::string subStdout, subStderr;
    err = Subprocess::launchSync(argv.getv(), 0, gitRepoPath.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitdbCommitMaster: cannot add: %d: %s", err, subStderr.c_str());
        return -1;
    }

    // commit
    std::string gitAuthor = author + " <>";
    argv.set("git", "commit", "-m", "modified", "--author", gitAuthor.c_str(), 0);
    err = Subprocess::launchSync(argv.getv(), 0, gitRepoPath.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitdbCommitMaster: cannot commit: %d: %s", err, subStderr.c_str());
       return -1;
    }
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

