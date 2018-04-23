
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


/** Store data in the git repo
 */
std::string gitStoreFile(const std::string &gitRepoPath, const char *data, size_t len)
{
    Argv argv;

    argv.set("git", "hash-object", "-w", "--stdin", 0);
    std::string sha1Id, subStderr;
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepoPath.c_str(), data, len, sha1Id, subStderr);
    if (err) {
        LOG_ERROR("gitStoreFile error %d: %s", err, subStderr.c_str());
        return "";
    }

    trim(sha1Id);
    if (sha1Id.size() != SIZE_COMMIT_ID) {
        LOG_ERROR("gitStoreFile error: sha1Id=%s", sha1Id.c_str());
        return "";
    }

    return sha1Id;
}


/** Load the contents of a file referenced by a git ref and a filename
 *
 * @return
 *     O on success
 *    -1 if gitRef does not exist
 *    -2 on error
 */
int gitLoadFile(const std::string &gitRepoPath, const std::string &gitRef, const std::string &filepath, std::string &data)
{
    Argv argv;
    int err;
    std::string subStdout, subStderr;

    // retrieve the commit object of gitRef

    argv.set("git", "cat-file", "-p", gitRef.c_str(), 0);
    err = Subprocess::launchSync(argv.getv(), 0, gitRepoPath.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_INFO("gitLoadFile error in cat-file: %s (the ref does not exist?)", subStderr.c_str());
        return -1;
    }

    // parse the commit object and look for the "tree" part
    std::string treeId;
    while (!subStdout.empty()) {
        std::string line = popToken(subStdout, '\n', TOK_STRICT);
        std::string token = popToken(line, ' ', TOK_STRICT);
        if (token == "tree") {
            treeId = line;
            break;
        }
    }

    if (treeId.empty()) {
        LOG_ERROR("gitLoadFile: missing tree in ref: %s", gitRef.c_str());
        return -2;
    }

    // git ls-tree <hash> <filepath>
    argv.set("git", "ls-tree", treeId.c_str(), filepath.c_str(), 0);
    err = Subprocess::launchSync(argv.getv(), 0, gitRepoPath.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitLoadFile error in ls-tree: %s (treeId=%s, filepath=%s)", subStderr.c_str(),
                  treeId.c_str(), filepath.c_str());
        return -2;
    }

    if (subStdout.empty()) {
        LOG_ERROR("gitLoadFile error: no such file (treeId=%s, filepath=%s)", treeId.c_str(), filepath.c_str());
        return -2;
    }

    // parse the ls-tree output
    // take 3rd field
    // The format of the line is: <mode> SP <type> SP <object> TAB <file>

    std::string mode = popToken(subStdout, ' ', TOK_STRICT);
    std::string type = popToken(subStdout, ' ', TOK_STRICT); // should be 'blob'
    std::string fileId = popToken(subStdout, '\t', TOK_STRICT);
    if (fileId.empty()) {
        LOG_ERROR("gitLoadFile error: cannot find fileId in ls-tree output (treeId=%s, filepath=%s)",
                  treeId.c_str(), filepath.c_str());
        return -2;
    }

    // git cat-file
    argv.set("git", "cat-file", "-p", fileId.c_str(), 0);
    err = Subprocess::launchSync(argv.getv(), 0, gitRepoPath.c_str(), 0, 0, data, subStderr);
    if (err) {
        LOG_ERROR("gitLoadFile error in cat-file (2): %s (fileId=%s)", subStderr.c_str(), fileId.c_str());
        return -2;
    }

    return 0;
}



