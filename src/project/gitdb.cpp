#include <string>
#include <string.h>
#include <stdlib.h>

#include "gitdb.h"
#include "global.h"
#include "utils/logging.h"
#include "utils/stringTools.h"
#include "utils/subprocess.h"

#define BRANCH_PREFIX_ISSUES "issues/"
#define SIZE_COMMIT_ID 40

int GitIssueList::open(const std::string &gitRepoPath)
{
    // git branch --list issues/*
    std::string cmd = "git -C \"" + gitRepoPath + "\" branch --list \"" BRANCH_PREFIX_ISSUES "*\"";
    p = Pipe::open(cmd, "re"); // with FD_CLOEXEC

    if (p) return 0;
    LOG_ERROR("Cannot get issue list (%s)", gitRepoPath.c_str());
    return -1;
}

/** Read the next line
 *
 *  Expected line read from the pipe: "    issues/<id>\n"
 */
std::string GitIssueList::getNext()
{
    std::string line = p->getline();
    if (line.empty()) return line;

    // trim the "  issues/" prefix
    if (line.size() > strlen(BRANCH_PREFIX_ISSUES)+2) {
        line = line.substr(strlen(BRANCH_PREFIX_ISSUES)+2);
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
    Pipe::close(p);
}


int GitIssue::open(const std::string &gitRepoPath, const std::string &issueId)
{
    // git log --format=raw --notes issues/<id>"
    std::string cmd = "git -C \"" + gitRepoPath + "\"";
    cmd += " log --format=raw --notes";
    cmd += " " BRANCH_PREFIX_ISSUES + issueId;
    p = Pipe::open(cmd, "re"); // with FD_CLOEXEC

    if (p) return 0;
    LOG_ERROR("Cannot open log of issue %s (%s)", issueId.c_str(), gitRepoPath.c_str());
    return -1;
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
        std::string commitLine = p->getline();
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
        std::string line = p->getline();
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
    Pipe::close(p);
}

/** Store data in the git repo
 */
std::string gitdbStoreFile(const std::string &gitRepoPath, const char *data, size_t len)
{
    Argv argv;
    Subprocess *subp = 0;

    argv.set("git", "hash-object", "-w", "--stdin", 0);
    subp = Subprocess::launch(argv.getv(), 0, gitRepoPath.c_str());
    if (!subp) return "";
    subp->write(data, len);
    subp->closeStdin();
    std::string sha1Id = subp->getStdout();
    trim(sha1Id);
    std::string stderrString = subp->getStderr(); // must be called before wait()
    int err = subp->wait();
    delete subp;
    if (err) {
        LOG_ERROR("gitdbStoreFile error %d: %s", err, stderrString.c_str());
        return "";
    }

    if (sha1Id.size() != SIZE_COMMIT_ID) {
        LOG_ERROR("gitdbStoreFile error: sha1Id=%s", sha1Id.c_str());
        return "";
    }

    return sha1Id;
}

int gitdbLsTree(const std::string &gitRepoPath, const std::string &treeid, std::list<AttachedFileRef> &files)
{
    Argv argv;
    Subprocess *subp = 0;
    files.clear();

    argv.set("git", "ls-tree", "-l", "-z", treeid.c_str(), 0);
    subp = Subprocess::launch(argv.getv(), 0, gitRepoPath.c_str());
    if (!subp) return -1;
    std::string result = subp->getStdout();
    std::string stderrString = subp->getStderr(); // must be called before wait()
    int err = subp->wait();
    delete subp;
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
    Subprocess *subp = 0;

    argv.set("git", "notes", "add", "--force", "--file=-", entryId.c_str(), 0);
    subp = Subprocess::launch(argv.getv(), 0, gitRepoPath.c_str());
    if (!subp) return -1;
    subp->write(data);
    subp->closeStdin();
    std::string stderrString = subp->getStderr(); // must be called before wait()
    int err = subp->wait();
    delete subp;
    if (err) {
        LOG_ERROR("addCommit read-tree error %d: %s", err, stderrString.c_str());
    }
    return err;
}

/**
 * @brief add an entry (ie: a commit) in the branch of the issue
 * @param gitRepoPath
 * @param issueId
 * @param author
 * @param body
 * @param files
 *    List of files identifiers, in the form: <sha1-id>/<filename>
 *    The file itself must have been previously stored in the git
 *    repository with git hash-object -w (see related function)
 * @return
 *
 * The atomicity and thread safety is not garanteed at this level.
 * The caller must manage his own mutex.
 */
std::string GitIssue::addCommit(const std::string &gitRepoPath, const std::string &issueId,
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

    std::string bareGitRepo = gitRepoPath + "/.git";
    Argv argv;
    Subprocess *subp = 0;

    argv.set("git", "read-tree", "--empty", 0);
    subp = Subprocess::launch(argv.getv(), 0, bareGitRepo.c_str());
    if (!subp) return "";
    std::string stderrString = subp->getStderr(); // must be called before wait()
    int err = subp->wait();
    delete subp;
    if (err) {
        LOG_ERROR("addCommit read-tree error %d: %s", err, stderrString.c_str());
       return "";
    }

    // attached files
    std::list<AttachedFileRef>::const_iterator afr;
    FOREACH (afr, files) {
        // 100644 : Regular non-executable file
        argv.set("git", "update-index", "--add", "--cacheinfo", "100644",
                 afr->id.c_str(), afr->filename.c_str(), 0);
        subp = Subprocess::launch(argv.getv(), 0, bareGitRepo.c_str());
        if (!subp) return "";
        std::string stderrString = subp->getStderr(); // must be called before wait()
        int err = subp->wait();
        delete subp;
        if (err) {
            LOG_ERROR("addCommit update-index error %d: %s", err, stderrString.c_str());
            return "";
        }
    }

    argv.set("git", "write-tree", 0);
    subp = Subprocess::launch(argv.getv(), 0, bareGitRepo.c_str());
    if (!subp) return "";
    std::string treeId = subp->getline();
    stderrString = subp->getStderr(); // must be called before wait()
    err = subp->wait();
    delete subp;
    if (err) {
        LOG_ERROR("addCommit write-tree error %d: %s", err, stderrString.c_str());
        return "";
    }

    trim(treeId);
    if (treeId.size() != SIZE_COMMIT_ID) {
        LOG_ERROR("addCommit write-tree error: treeId=%s", treeId.c_str());
        return "";
    }

    // git show-ref issues/<id>
    std::string branchName = "issues/" + issueId;
    argv.set("git", "show-ref", "-s", branchName.c_str(), 0);
    subp = Subprocess::launch(argv.getv(), 0, bareGitRepo.c_str());
    if (!subp) return "";
    std::string branchRef = subp->getline();
    trim(branchRef);
    stderrString = subp->getStderr(); // must be called before wait()
    err = subp->wait();
    delete subp;
    if (err) {
        if (stderrString.empty() && branchRef.empty()) {
            // This is a new issue. The git branch will be created later.
            LOG_INFO("addCommit: new issue to be created: %s", issueId.c_str());

        } else {
            LOG_ERROR("addCommit show-ref error %d: %s (branchRef=%s)",
                      err, stderrString.c_str(), branchRef.c_str());
            return "";
        }
    }

    // check that branchRef is either empty or consistent with a commit id
    if (branchRef.size() && branchRef.size() != SIZE_COMMIT_ID) {
        LOG_ERROR("addCommit show-ref error: branchRef=%s", branchRef.c_str());
        return "";
    }

    // git commit-tree

    argv.set("git", "commit-tree", treeId.c_str(), 0);
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

    subp = Subprocess::launch(argv.getv(), envp.getv(), bareGitRepo.c_str());
    if (!subp) return "";

    subp->write(body);
    subp->closeStdin();
    std::string commitId = subp->getline();
    trim(commitId);
    stderrString = subp->getStderr(); // must be called before wait()
    err = subp->wait();
    delete subp;
    if (err) {
        LOG_ERROR("addCommit commit-tree error %d: %s", err, stderrString.c_str());
        return "";
    }

    // git update-ref refs/heads/$branch $commit_id
    std::string branchPath = "refs/heads/" + branchName;
    argv.set("git", "update-ref", branchPath.c_str(), commitId.c_str(), 0);
    subp = Subprocess::launch(argv.getv(), 0, bareGitRepo.c_str());
    if (!subp) return "";
    stderrString = subp->getStderr(); // must be called before wait()
    err = subp->wait();
    delete subp;
    if (err) {
        LOG_ERROR("addCommit update-ref error %d: %s", err, stderrString.c_str());
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
    Subprocess *subproc = 0;

    argv.set("git", "cat-file", "-s", id.c_str(), 0);
    subproc = Subprocess::launch(argv.getv(), 0, path.c_str());
    if (!subproc) return -1;
    std::string stderrString = subproc->getStderr(); // must be called before wait()
    std::string stdoutString = subproc->getStdout(); // must be called before wait()
    int err = subproc->wait();
    delete subproc;
    if (err) {
        LOG_ERROR("addCommit read-tree error %d: %s", err, stderrString.c_str());
       return -1;
    }
    return atoi(stdoutString.c_str());
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
