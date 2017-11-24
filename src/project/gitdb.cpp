#include <string>
#include <string.h>

#include "gitdb.h"
#include "utils/logging.h"
#include "utils/stringTools.h"

#define BRANCH_PREFIX_ISSUES "issues/"

int GitIssueList::open(const std::string &gitRepoPath)
{
    // git branch --list issues/*
    std::string cmd = "git -C \"" + gitRepoPath + "\" branch --list \"" BRANCH_PREFIX_ISSUES "*\"";
    p = Pipe::open(cmd, "re"); // with FD_CLOEXEC

    if (p) return 0;
    LOG_ERROR("Cannot get issue list (%s)", path.c_str());
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
    LOG_ERROR("Cannot open log of issue %s (%s)", issueId.c_str(), path.c_str());
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
