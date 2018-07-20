#include <string>
#include <string.h>
#include <stdlib.h>

#include "gitdb.h"
#include "global.h"
#include "utils/logging.h"
#include "utils/stringTools.h"
#include "utils/subprocess.h"
#include "utils/filesystem.h"
#include "utils/gitTools.h"

#define SIZE_COMMIT_ID 40

int GitIssueList::open(const std::string &gitRepoPath, const std::string &remote)
{
    remoteName = remote;

    // git branch --list issues/*
    // OR
    // git branch --list --remotes <remote>/issues/*

    Argv argv;
    argv.set("git", "branch", "--list", 0);

    // branch name (remote or not)
    std::string branchName = BRANCH_PREFIX_ISSUES "*";
    if (!remote.empty()) {
        // remote requested
        argv.append("--remotes", 0);
        branchName = remote + "/" + branchName;
    }

    argv.append(branchName.c_str(), 0);

    subp = Subprocess::launch(argv.getv(), 0, gitRepoPath.c_str());
    if (!subp) {
        LOG_ERROR("GitIssueList::open: failed to launch: %s", argv.toString().c_str());
        return -1;
    }

    subp->closeStdin();

    return 0;
}

/** Read the next line
 *
 *  Expected line read from the pipe: "    issues/<id>\n"
 */
std::string GitIssueList::getNext()
{
    std::string line = subp->getline();
    if (line.empty()) return line;

    // trim the "  issues/" prefix (or "  <remote>/issues/")
    int addLength = 0;
    if (!remoteName.empty()) addLength = remoteName.size() + 1; // +1 for the "/"
    if (line.size() > strlen(BRANCH_PREFIX_ISSUES)+2+addLength) {
        line = line.substr(strlen(BRANCH_PREFIX_ISSUES)+2+addLength);
        trimRight(line, "\n"); // remove possible trailing LF

    } else {
        // error, should not happen
        LOG_ERROR("GitIssueList::getNext recv invalid line: %s", line.c_str());
        line = "";
    }
    return line;
}

void GitIssueList::close()
{
    subp->shutdown();
    delete subp;
}

static Argv getCmdLog()
{
    // Return the command for reading the log
    Argv argv;
    argv.set("git", "log", "--format=raw", "--notes", 0);
    return argv;
}

std::string GitIssue::getCommit(const std::string &gitRepo, const std::string &commit)
{
    Argv argv = getCmdLog();
    argv.append(commit.c_str(), "-1", 0); // get just this commit
    std::string subStdout, subStderr;

    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_DIAG("git log single commit error: gitRepo=%s, commit=%s, stderr=%s",
                 gitRepo.c_str(), commit.c_str(), subStderr.c_str());
        return "";
    }
    return subStdout;
}

int GitIssue::open(const std::string &gitRepoPath, const std::string &issueId)
{
    // git log --format=raw --notes issues/<id>"

    std::string branchIssueId = BRANCH_PREFIX_ISSUES + issueId;
    Argv argv = getCmdLog();
    argv.append(branchIssueId.c_str(), 0);

    subp = Subprocess::launch(argv.getv(), 0, gitRepoPath.c_str());
    if (!subp) {
        LOG_ERROR("GitIssue::open: failed to launch: %s", argv.toString().c_str());
        return -1;
    }

    subp->closeStdin();

    return 0;
}

static bool isCommitLine(const std::string &line)
{
    if (line.size() < 7) return false;
    if (0 != strncmp("commit ", line.c_str(), 7)) return false;
    return true;
}

/** Read the next entry in the log.
 *
 *  An entry starts by a line "commit ...."
 *  The body is indented by 4 spaces.
 *
 *  Example:
 *  commit 746b18c8251b331f9819d066ff5bf05d67dcae4e
 *  tree 9dcca633b452ff20364918a1eb920d06ef73ea6e
 *  parent 13ad980a4627c811406611efe4ed28c81a7da8d4
 *  author homer <> 1511476601 +0100
 *  committer homer <> 1511476601 +0100
 *
 *      property status open
 *      ....
 *
 *  Notes:
 *      tag toto
 *      tag tutu
 *
 */
std::string GitIssue::getNextEntry()
{
    std::string entry;
    if (previousCommitLine.empty()) {
        std::string commitLine = subp->getline();
        if (commitLine.empty()) return ""; // the end

        if (!isCommitLine(commitLine)) {
            LOG_ERROR("GitIssue::getNextEntry: missing 'commit' line: %s", commitLine.c_str());
            return "";
        }
        entry = commitLine;
    } else {
        entry = previousCommitLine;
        previousCommitLine = "";
    }

    // read until next cmomit line or end of stream
    while (1) {
        std::string line = subp->getline();
        if (line.empty()) return entry; // end of stream
        if (isCommitLine(line)) {
            previousCommitLine = line; // store for next iteration
            return entry; // done
        }
        entry += line;
    }
}

void GitIssue::close()
{
    subp->shutdown();
    delete subp;
}

/** Store data in the git repo
 */
std::string gitdbStoreFile(const std::string &gitRepoPath, const char *data, size_t len)
{
    Argv argv;

    argv.set("git", "hash-object", "-w", "--stdin", 0);
    std::string sha1Id, subStderr;
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepoPath.c_str(), data, len, sha1Id, subStderr);
    if (err) {
        LOG_ERROR("gitdbStoreFile error %d: %s", err, subStderr.c_str());
        return "";
    }

    trim(sha1Id);
    if (sha1Id.size() != SIZE_COMMIT_ID) {
        LOG_ERROR("gitdbStoreFile error: sha1Id=%s", sha1Id.c_str());
        return "";
    }

    return sha1Id;
}

/** Read the list of files of a tree id
 *
 * @param gitRepoPath
 * @param treeid
 * @param[out] files
 */
int gitdbLsTree(const std::string &gitRepoPath, const std::string &treeid, std::list<AttachedFileRef> &files)
{
    Argv argv;
    files.clear();

    argv.set("git", "ls-tree", "-l", "-z", treeid.c_str(), 0);
    std::string result, stderrString;
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepoPath.c_str(), 0, 0, result, stderrString);
    if (err) {
        LOG_ERROR("gitdbLsTree error %d: %s", err, stderrString.c_str());
        return -1;
    }

    // parse the result
    while (!result.empty()) {
        AttachedFileRef afr;
        std::string line = popToken(result, '\0', TOK_STRICT);
        std::string mode = popToken(line, ' ', TOK_STRICT); // unused
        std::string type = popToken(line, ' ', TOK_STRICT);
        afr.id = popToken(line, ' ', TOK_STRICT);
        std::string size = popToken(line, '\t', TOK_STRICT);
        afr.size = atoi(size.c_str());
        afr.filename = line;
        files.push_back(afr);
    }

    return 0;

}

/** Set git notes
 */
int gitdbSetNotes(const std::string &gitRepoPath, const ObjectId &entryId, const std::string &data)
{
    // git notes add --force --file=- <id>
    Argv argv;

    argv.set("git", "notes", "add", "--force", "--file=-", entryId.c_str(), 0);
    std::string subStdout, subStderr;
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepoPath.c_str(), data.data(), data.size(), subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitdbSetNotes error %d: %s", err, subStderr.c_str());
    }
    return err;
}

static void gitReset(const std::string &bareGitRepo)
{
    int err;
    std::string subStdout, subStderr;
    Argv argv;

    argv.set("git", "reset", 0);
    err = Subprocess::launchSync(argv.getv(), 0, bareGitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitReset error: %s", subStderr.c_str());
    }
}

/** Create a tree object in a bare git repository
 *
 * @param bareGitRepo
 * @param files
 * @return
 *     Identifier of the tree object, or an empty string on error.
 *
 */
static std::string createTree(const std::string &bareGitRepo, const std::list<AttachedFileRef> &files)
{
    int err;
    std::string subStdout, subStderr;
    Argv argv;

    argv.set("git", "read-tree", "--empty", 0);
    err = Subprocess::launchSync(argv.getv(), 0, bareGitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("createTree: read-tree error %d: %s", err, subStderr.c_str());
       return "";
    }

    // attached files
    std::list<AttachedFileRef>::const_iterator afr;
    FOREACH (afr, files) {
        // 100644 : Regular non-executable file
        argv.set("git", "update-index", "--add", "--cacheinfo", "100644",
                 afr->id.c_str(), afr->filename.c_str(), 0);
        err = Subprocess::launchSync(argv.getv(), 0, bareGitRepo.c_str(), 0, 0, subStdout, subStderr);
        if (err) {
            LOG_ERROR("createTree: update-index error %d: %s", err, subStderr.c_str());
            return "";
        }
    }

    argv.set("git", "write-tree", 0);
    err = Subprocess::launchSync(argv.getv(), 0, bareGitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("createTree: write-tree error %d: %s", err, subStderr.c_str());
        return "";
    }

    std::string treeId = subStdout;
    trim(treeId);
    if (treeId.size() != SIZE_COMMIT_ID) {
        LOG_ERROR("createTree: write-tree error: treeId=%s", treeId.c_str());
        return "";
    }

    // git-reset needed as the index has been modified by read-tree
    gitReset(bareGitRepo);

    return treeId;
}

/** Create a commit in a branch
 *
 * @return
 *     The newly created commit identifier, or an empty string on error.
 */
std::string GitIssue::addCommit(const std::string &bareGitRepo, const std::string &branch, const std::string &tree,
                                const std::string &author, long ctime, const std::string &body)
{
    int err;
    std::string subStdout, subStderr;
    Argv argv;

    std::string branchPath = "refs/heads/" + branch; // a commit is always created in a local branch

    std::string branchRef;
    err = gitShowRef(bareGitRepo, branchPath, branchRef);
    if (err) return "";

    // check that branchRef is either empty or consistent with a commit id
    if (branchRef.size() && branchRef.size() != SIZE_COMMIT_ID) {
        LOG_ERROR("addCommit: show-ref error: branchRef=%s", branchRef.c_str());
        return "";
    }
    // If the branch ref does not exist, the branch will be created later.

    // git commit-tree

    argv.set("git", "commit-tree", tree.c_str(), 0);
    if (!branchRef.empty()) {
        argv.append("-p", branchRef.c_str(), 0);
    }
    std::string gitAuthorEnv = "GIT_AUTHOR_NAME=" + author;
    std::string gitCommitterEnv = "GIT_COMMITTER_NAME=" + author;
    std::stringstream ss;
    ss << "GIT_AUTHOR_DATE=" << ctime;
    std::string gitAuthorDate = ss.str();
    ss.str("");
    ss << "GIT_COMMITTER_DATE=" << ctime;
    std::string gitCommitterDate = ss.str();
    Argv envp;
    envp.set(gitAuthorEnv.c_str(),
            "GIT_AUTHOR_EMAIL=<>",
            gitAuthorDate.c_str(),
            gitCommitterEnv.c_str(),
            "GIT_COMMITTER_EMAIL=<>",
            gitCommitterDate.c_str(),
            0);

    err = Subprocess::launchSync(argv.getv(), envp.getv(), bareGitRepo.c_str(), body.data(), body.size(), subStdout, subStderr);
    if (err) {
        LOG_ERROR("addCommit commit-tree error %d: %s", err, subStderr.c_str());
        return "";
    }

    std::string commitId = subStdout;
    trim(commitId);

    // git update-ref refs/heads/$branch $commit_id
    err = gitUpdateRef(bareGitRepo, branchPath, commitId);
    if (err) return "";

    return commitId;
}

/**
 * @brief add an entry (ie: a commit) in the branch of the issue
 * @param bareGitRepo Must be the path to a bare git repository
 * @param issueId
 * @param author
 * @param body
 * @param files
 *    List of files identifiers, in the form: <sha1-id>/<filename>
 *    The file itself must have been previously stored in the git
 *    repository with git hash-object -w (see related function)
 * @return
 *    Identifier of the commit object, or an empty string on error.
 *
 * The atomicity and thread safety is not garanteed at this level.
 * The caller must manage his own mutex.
 */
std::string GitIssue::addCommit(const std::string &bareGitRepo, const std::string &issueId,
                                const std::string &author, long ctime, const std::string &body, const std::list<AttachedFileRef> &files)
{
    // git commit in a branch issues/<id> without checking out the branch
    // The git commands must be run in a bare git repo (the ".git/").

    // git read-tree --empty
    // attached files: git update-index --add --cacheinfo 100644 ...
    // git write-tree
    // git show-ref issues/<id>
    // git commit-tree
    // git update-ref refs/heads/issues/<id>

    std::string treeId = createTree(bareGitRepo, files);
    if (treeId.empty()) {
        LOG_ERROR("addCommit: createTree error");
        return "";
    }

    // Get the reference of the branch, if is exists
    std::string branch = BRANCH_PREFIX_ISSUES + issueId;

    std::string commitId = addCommit(bareGitRepo, branch, treeId, author, ctime, body);
    if (commitId.empty()) {
        LOG_ERROR("addCommit: createCommitUpdateBranch error");
        return "";
    }

    LOG_DIAG("addCommit: %s", commitId.c_str());

    return commitId;
}


GitObject::GitObject(const std::string &gitRepoPath, const std::string &objectid) :
    path(gitRepoPath), id(objectid), subp(0)
{
}

int GitObject::getSize()
{
    Argv argv;
    std::string subStdout, subStderr;

    argv.set("git", "cat-file", "-s", id.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, path.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("addCommit cat-file error %d: %s", err, subStderr.c_str());
       return -1;
    }
    return atoi(subStdout.c_str());
}

/** Open an object for reading
  */
int GitObject::open()
{
    if (subp) {
        LOG_ERROR("GitObject::open subp already open");
        return -1;
    }
    Argv argv;
    argv.set("git", "cat-file", "-p", id.c_str(), 0);
    subp = Subprocess::launch(argv.getv(), 0, path.c_str());
    if (!subp) return -1;

    return 0;
}

int GitObject::read(char *buffer, size_t size)
{
    return subp->read(buffer, size);
}

void GitObject::close()
{
    subp->wait();
    delete subp;
    subp = 0;
}
