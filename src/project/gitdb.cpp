#include <string>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "gitdb.h"
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

class Argv {
public:
    void set(const char *first, ...) {
        argv.clear();
        argv.push_back(first);
        char *p;
        va_list ap;
        va_start(ap, first);
        while((p = va_arg(ap, char *))) {
            argv.push_back(p);
        }

        va_end(ap);
        argv.push_back(0);
    }
    char *const* getVector() { return (char *const*) &argv[0]; }

private:
    std::vector<const char*> argv;
};

/**
 * @brief add an entry (ie: a commit) in the branch of the issue
 * @param gitRepoPath
 * @param issueId
 * @param author
 * @param body
 * @param files
 * @return
 *
 * The atomicity and thread safety is not garanteed at this level.
 * The caller must manage his own mutex.
 */
std::string GitIssue::addCommit(const std::string &gitRepoPath, const std::string &issueId,
                                const std::string &author, long ctime, const std::string &body, const std::list<std::string> &files)
{
    // git commit in a branch issues/<id> without checking out the branch
    // The git commands must be run in a bare git repo (the ".git/").

    // git read-tree --empty
    // attached files
    //     git hash-object -w
    //     git update-index --add --cacheinfo 100644 ...
    // git write-tree
    // git show-ref issues/<id>
    // git commit-tree
    // git update-ref refs/heads/issues/<id>

    std::string bareGitRepo = gitRepoPath + "/.git";
    Argv argv;
    Subprocess *subp = 0;

    argv.set("git", "read-tree", "--empty");
    subp = Subprocess::launch(argv.getVector(), 0, bareGitRepo.c_str());
    if (!subp) return "";
    std::string stderrString = subp->getStderr(); // must be called before wait()
    int err = subp->wait();
    delete subp;
    if (err) {
        LOG_ERROR("addCommit read-tree error %d: %s", err, stderrString.c_str());
        return "";
    }

    // attached files
    // TODO
    //     git hash-object -w
    //     git update-index --add --cacheinfo 100644 ...
    LOG_ERROR("addCommit attached files not implemented");

    argv.set("git", "write-tree");
    subp = Subprocess::launch(argv.getVector(), 0, bareGitRepo.c_str());
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
    argv.set("git", "show-ref", branchName.c_str());
    subp = Subprocess::launch(argv.getVector(), 0, bareGitRepo.c_str());
    if (!subp) return "";
    std::string branchRef = subp->getline();
    stderrString = subp->getStderr(); // must be called before wait()
    err = subp->wait();
    delete subp;
    if (err) {
        LOG_ERROR("addCommit show-ref error %d: %s", err, stderrString.c_str());
        return "";
    }

    trim(branchRef);
    if (branchRef.size() != SIZE_COMMIT_ID) {
        LOG_ERROR("addCommit show-ref error: branchRef=%s", branchRef.c_str());
        return "";
    }

    // git commit-tree

    argv.set("git", "commit-tree", treeId.c_str());
    std::string gitAuthorEnv = "GIT_AUTHOR_NAME=" + author;
    std::string gitCommiterEnv = "GIT_COMMITTER_NAME=" + author;
    std::stringstream gitAuthorDate;
    gitAuthorDate << "GIT_AUTHOR_DATE=" << ctime;
    std::stringstream gitCommiterDate;
    gitCommiterDate << "GIT_COMMITTER_DATE=" << ctime;

    Argv envp;
    envp.set(gitAuthorEnv.c_str(),
            "GIT_AUTHOR_EMAIL=<>",
            gitAuthorDate.str().c_str(),
            gitCommiterEnv.c_str(),
            "GIT_COMMITTER_EMAIL=<>",
            gitCommiterDate.str().c_str());

    subp = Subprocess::launch(argv.getVector(), envp.getVector(), bareGitRepo.c_str());
    if (!subp) return "";

    subp->write(body);
    subp->closeStdin();
    std::string commitId = subp->getline();
    stderrString = subp->getStderr(); // must be called before wait()
    err = subp->wait();
    delete subp;
    if (err) {
        LOG_ERROR("addCommit commit-tree error %d: %s", err, stderrString.c_str());
        return "";
    }

    LOG_DIAG("addCommit: %s", commitId.c_str());

    return commitId;
}

