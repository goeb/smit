/*   Small Issue Tracker
 *   Copyright (C) 2015 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License v2 as published by
 *   the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
#include "config.h"

#include <iostream>
#include <string>
#include <list>
#include <algorithm>
#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <getopt.h>
#include <fstream>
#include <sstream>
#include <iostream>

#include "clone.h"
#include "global.h"
#include "utils/stringTools.h"
#include "utils/filesystem.h"
#include "utils/logging.h"
#include "utils/gitTools.h"
#include "user/session.h"
#include "project/gitdb.h"
#include "mg_win32.h"
#include "console.h"
#include "repository/db.h"
#include "httpClient.h"
#include "Args.h"
#include "restApi.h"
#include "project/Project.h"
#include "localClient.h"

#define LOG_CLI(...) { printf(__VA_ARGS__); printf("\n"); fflush(stdout);}

/** smit distributed functions
  *     smit clone .... : download a local copy of a smit repository
  *     smit pull ....  : download latest changes from a smit server
  *     smit push ....  : upload local changes to a smit server
  *
  * 1. Conflicts on existing issues
  * These conflicts occur typically when a user modified an issue on the server and
  * another user modified the same issue on his/her local clone.
  * These conflicts are rejected during a pushing. They can be resolved only
  * by a pulling.
  * These conflicts are resolved bu a git pull --rebase
  *
  *
  * 2. Conflicts on new issues
  * These conflicts occur typically when a user creates a new issue on the server and
  * another user creates a new issue on his/her local clone, and both new issues get
  * the same id.
  *
  * These conflicts are resolved as follows:
  * - the client detects the conflict (say on issues/123), and renames the local "issues/123.local"
  * - the client pulls the remote issue 123
  * - the clients pushes issues/123.local, and the procedure below applies (cf. 3.)
  *
  *
  * 3. Server-side renaming of issues identifiers
  * As the server is responsible for allocating issues identifiers (which may be shared
  * among several project if numbering policy is "global"), all local issues may be
  * renamed by the server after being pushed.
  *
  * This is solved by the client as follows:
  * - the client pushes local issues to the remote under a branch named issues_tmp/...
  *       git push issues/123:issues_tmp/123
  * - the server allocates a new identifier for issues_tmp/123 (say issues/127)
  * - the client pulls
  * - the client detects that issues/127 and the local 123 are the same (because same first entry),
  *   prints the information on the terminal, and deletes the old local name (123)
  */

// resolve strategies for pulling conflicts
enum MergeStrategy { MERGE_KEEP_LOCAL, MERGE_DROP_LOCAL, MERGE_INTERACTIVE};
struct PullContext {
    std::string rooturl; // eg: http://example.com:8090/
    std::string localRepo; // path to local repository
    HttpClientContext httpCtx; // session identifier

    inline std::string getTmpDir() const { return localRepo + "/.tmp"; }
};

static void terminateSession()
{
    curl_global_cleanup();
}

int testSessid(const std::string &url, const HttpClientContext &ctx)
{
    LOG_DEBUG("testSessid(%s, %s)", url.c_str(), ctx.cookieSessid.c_str());

    HttpRequest hr(ctx);
    hr.setUrl(url + "/?format=text");
    int status = hr.test();
    LOG_DEBUG("status = %d", status);

    if (status == 200) return 0;
    return -1;
}


/** Create local branches tracking remotes ones after a clone
 */
static int alignIssueBranches(const std::string &projectPath)
{
    int err;

    // create local branches to track remote ones (issues/*)
    GitIssueList ilist;
    err = ilist.open(projectPath, "origin");
    if (err) {
        LOG_ERROR("Cannot load issues of project (%s)", projectPath.c_str());
        return -1;
    }

    while (!err) {
        std::string issueId = ilist.getNext();
        if (issueId.empty()) break; // reached the end

        // create a local branch
        std::string localBranch = BRANCH_PREFIX_ISSUES +  issueId;
        std::string origin = "origin/" BRANCH_PREFIX_ISSUES +  issueId;
        err = gitBranchCreate(projectPath, localBranch, origin);
    }
    ilist.close();
    return err;
}

static void setupGitCloneConfig(const std::string &dir)
{
    Argv argv;
    std::string subStdout, subStderr;
    std::string credentialHelper = "store --file ../" PATH_GIT_CREDENTIAL;

    argv.set("git", "config", "credential.helper", credentialHelper.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, dir.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("setupGitCloneConfig: error: %d: stdout=%s, stderr=%s", err, subStdout.c_str(), subStderr.c_str());
    }
}

static int gitClone(const std::string &remote, const std::string &smitRepo, const std::string &subpath)
{
    Argv argv;
    std::string subStdout, subStderr;
    std::string into = smitRepo + "/" + subpath; // where to clone into
    std::string configOpt = "credential.helper=store --file " + smitRepo + "/" PATH_GIT_CREDENTIAL;

    argv.set("git", "clone", remote.c_str(), into.c_str(), "--config", configOpt.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, 0, 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitClone: error: %d: stdout=%s, stderr=%s", err, subStdout.c_str(), subStderr.c_str());
        return err;
    }

    LOG_DIAG("gitClone: stdout=%s, stderr=%s", subStdout.c_str(), subStderr.c_str());

    // create local branches to track remote ones
    err = alignIssueBranches(into);
    if (err) {
        LOG_ERROR("gitClone: alignIssueBranches error: %d: stdout=%s, stderr=%s", err, subStdout.c_str(), subStderr.c_str());
    }

    setupGitCloneConfig(into);

    return err;
}

static int copyNotes(const std::string &gitRepo, const std::string &fromCommit, const std::string &toCommit)
{
    // read the notes attached to <fromCommit> and copy to the current HEAD commit
    // git notes copy [-f] <from-object> <to-object> )

    Argv argv;
    std::string subStdout, subStderr;

    argv.set("git", "notes", "copy", "--force", fromCommit.c_str(), toCommit.c_str(), 0);
    int err = Subprocess::launchSync(argv.getv(), 0, gitRepo.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_DIAG("git notes add error: gitRepo=%s, fromCommit=%s, stderr=%s",
                 gitRepo.c_str(), fromCommit.c_str(), subStderr.c_str());
    }
    return err;
}

static int gitRebaseCommit(const std::string &gitRepo, const std::string &branch, const std::string &commit)
{
    LOG_DIAG("gitRebaseCommit %s", commit.c_str());

    // Create a new entry with same message, author, creation time, treeid, etc.
    // - parse the old commit
    // - create the new commit
    // Use:
    // - Entry::loadEntry()
    // - gitdb.cpp createCommitUpdateBranch()

    // read the commit message
    std::string entryString = GitIssue::getCommit(gitRepo, commit);

    // get the entry
    std::string tree;
    std::list<std::string> tags;
    Entry *e = Entry::loadEntry(entryString, tree, tags);
    if (!e) {
        LOG_ERROR("gitRebaseCommit: Cannot load entry '%s'", entryString.c_str());
        return -1;
    }

    // create a clone of this entry onto the branch
    std::string bareGitRepo = gitRepo + "/.git";
    const std::string body = e->serialize();

    std::string newCommit = GitIssue::addCommit(bareGitRepo, branch, tree, e->author, e->ctime, body);
    if (newCommit.empty()) {
        LOG_ERROR("gitRebaseCommit: Cannot create rebased commit");
        return -1;
    }

    return 0;
}

/** Rebase a branch onto the remote by cherry-picking
 *
 *  Notes are also copied.
 *
 */
static int rebaseBranchOntoRemoteFull(const std::string &gitRepo,const std::string &base, const std::string &branch)
{
    // git rev-list 2ba568abdcbf91f05d0e3539004505c0ea6a6b0e..refs/heads/issues/2
    // 2d040344eb57a953bb72f688f996c81a70e9d531
    // 9e9e7eae6f4db222da46f9838cfdf467fa41661c
    // git branch tmp origin/issues/2
    // git worktree add worktree_issue_x tmp
    // cd worktree_issue_x
    //     git cherry-pick --allow-empty -X theirs 9e9e7eae6f4db222da46f9838cfdf467fa41661c
    //     git cherry-pick --allow-empty -X theirs 2d040344eb57a953bb72f688f996c81a70e9d531
    //     TODO git notes copy
    // git update-ref refs/heads/issues/2 refs/heads/tmp


    std::string localBranch = "refs/heads/" + branch;
    std::string revList;
    int err = gitRevListReverse(gitRepo, base, localBranch, revList);
    if (err) return -1;

    const char *TMP_BRANCH = "smit_tmp_for_rebasing";
    std::string origin = "refs/remotes/origin/" + branch;
    err = gitBranchCreate(gitRepo, TMP_BRANCH, origin, GIT_OPT_FORCE);
    if (err) return -1;

    LOG_DIAG("rebaseBranchOntoRemoteFull: start rebasing on branch '%s'", TMP_BRANCH);

    while (!revList.empty()) {
        std::string commit = popToken(revList, '\n');
        if (commit.empty()) continue;

        err = gitRebaseCommit(gitRepo, TMP_BRANCH, commit);
        if (err) {
            LOG_ERROR("rebaseBranchOntoRemoteFull: abort.");
            // TMP_BRANCH not deleted, for further investigation.
            return -1;
        }

        // copy notes
        err = copyNotes(gitRepo, commit, TMP_BRANCH);
        // do not check error code as an error is raised if the commit has no note.
    }

    err = gitUpdateRef(gitRepo, localBranch, TMP_BRANCH);
    if (err) return -1;

    gitBranchRemove(gitRepo, TMP_BRANCH, GIT_OPT_FORCE);

    return 0;
}

/** Rebase a branch onto the remote
 *
 * 1. create a specific worktree
 * 2. do the rebasing in this worktree
 * 3. clean up the worktree
 *
 */
static int rebaseBranchOntoRemote(const std::string &gitRepo, const std::string &branchName)
{
    int err;

    // git merge-base issues/2 origin/issues/2
    // 2ba568abdcbf91f05d0e3539004505c0ea6a6b0e
    // if this == origin/issues/2, then do nothing and return ok -- no rebase needed
    // if this == issues/2, then just do git update-ref refs/heads/issues/2 refs/remotes/origin/issues/2 (fast-forward)
    // else:
    // git rev-list 2ba568abdcbf91f05d0e3539004505c0ea6a6b0e..refs/heads/issues/2
    // 2d040344eb57a953bb72f688f996c81a70e9d531
    // 9e9e7eae6f4db222da46f9838cfdf467fa41661c
    // git branch tmp origin/issues/2
    // git worktree add worktree_issue_x tmp
    // cd worktree_issue_x
    //     git cherry-pick --allow-empty -X theirs 9e9e7eae6f4db222da46f9838cfdf467fa41661c
    //     git cherry-pick --allow-empty -X theirs 2d040344eb57a953bb72f688f996c81a70e9d531
    //     TODO git notes copy
    // git update-ref refs/heads/issues/2 refs/heads/tmp
    // done!

    // Do the rebasing manually as git-rebase --keep-empty does not keep all empty commits
    std::string localBranch = "refs/heads/" + branchName;
    std::string remoteBranch = "refs/remotes/origin/" + branchName;
    std::string base = gitMergeBase(gitRepo, localBranch, remoteBranch);
    if (base.empty()) {
        LOG_ERROR("gitMergeBase(%s, %s, %s) returned void", gitRepo.c_str(), localBranch.c_str(), remoteBranch.c_str());
        return -1;
    }

    std::string gitRef;
    err = gitShowRef(gitRepo, remoteBranch, gitRef);
    if (err) return -1;

    if (gitRef == base) {
        // The local branch is ahead of the remote, without diverging.
        // Do nothing and return ok -- no rebase needed
        LOG_DIAG("rebaseBranchOntoRemote: no rebase needed (1) for %s", branchName.c_str());
        return 0;
    }

    err = gitShowRef(gitRepo, localBranch, gitRef);
    if (err) return -1;
    if (gitRef == base) {
        // The local branch is past the remote, without diverging.
        // Simply do git update-ref refs/heads/issues/x -> refs/remote/origin/issues/x (fast-forward)
        LOG_DIAG("rebaseBranchOntoRemote: fast-forward for %s", branchName.c_str());

        err = gitUpdateRef(gitRepo, localBranch, remoteBranch);

        return err;
    }

    err = rebaseBranchOntoRemoteFull(gitRepo, base, branchName);

    return err;
}

/* Rename a local issue
 *
 * If oldId is of the form N.M, then increment M until an available identifier.
 * If oldId is of the form N, then start with M=0, and increment M until N.M is an available identifier.
 *
 * If the given local issue does not exist, then do nothing and return 0.
 */
static int renameIssue(const std::string &dir, const IssueId &oldId)
{
    int err;
    std::string gitRef;
    std::string oldBranch = BRANCH_PREFIX_ISSUES + oldId;
    std::string oldBranchFull = "refs/heads/" + oldBranch;

    err = gitShowRef(dir, oldBranchFull, gitRef);
    if (err) return err;
    if (gitRef.empty()) return 0; // no such branch. do nothing.

    size_t pos = oldId.find_last_of('.');
    int counter = 0;
    std::string root = oldId;

    if (pos == std::string::npos) {
        counter = 0;
    } else {
        counter = atoi(oldId.substr(pos).c_str()) + 1;
        if (pos > 0) root = oldId.substr(0, pos-1);
    }

    std::string newId;
    std::string newBranch;
    char buffer[32]; // size should be enough for representing an integer in decimal

    while (1) {
        snprintf(buffer, sizeof buffer, "%d", counter);
        newId = root + "." + buffer;
        newBranch = BRANCH_PREFIX_ISSUES + newId;
        std::string newBranchFull = "refs/heads/" + newBranch;

        err = gitShowRef(dir, newBranchFull, gitRef);
        if (err) return err;

        if (gitRef.empty()) {
            // no branch with this name, so it is available for renaming
            break;
        }
        counter++;
    }

    err = gitRenameBranch(dir, oldBranch, newBranch);
    if (!err) {
        // success
        // Indicate now the renaming (in case of a failure anytime further)
        LOG_CLI("  Local issue %s renamed %s", oldId.c_str(), newId.c_str());
    }

    return err;
}

static int renameIssue(const std::string &dir, const IssueId &oldIssue, const IssueId &newIssue)
{
    std::string oldBranch = BRANCH_PREFIX_ISSUES + oldIssue;
    std::string newBranch = BRANCH_PREFIX_ISSUES + newIssue;
    int err = gitRenameBranch(dir, oldBranch, newBranch);
    if (!err) {
        LOG_CLI("  Local issue %s renamed %s", oldIssue.c_str(), newIssue.c_str());
    }
    return err;
}


static int gitPullIssue(const std::string &dir, const IssueId &remoteIssueId, const EntryId &firstEntry)
{
    int err;

    std::string branchX = gitGetLocalBranchThatContains(dir, firstEntry);
    // extract the issue id
    std::string otherIssueIdWithSameFirstEntry;
    if (branchX.size() > strlen(BRANCH_PREFIX_ISSUES)) {
        otherIssueIdWithSameFirstEntry = branchX.substr(strlen(BRANCH_PREFIX_ISSUES));
    }

    if (!otherIssueIdWithSameFirstEntry.empty()) {
        if (otherIssueIdWithSameFirstEntry != remoteIssueId) {

            renameIssue(dir, remoteIssueId); // does nothing if no such local issue

            err = renameIssue(dir, otherIssueIdWithSameFirstEntry, remoteIssueId);
            if (err) {
                LOG_ERROR("gitPullIssue %s error (dir=%s) (0)", remoteIssueId.c_str(), dir.c_str());
                return err;
            }
        }

        std::string branchName = BRANCH_PREFIX_ISSUES + remoteIssueId;
        err = rebaseBranchOntoRemote(dir, branchName);
        if (err) {
            LOG_ERROR("gitPullIssue %s error (dir=%s) (1)", remoteIssueId.c_str(), dir.c_str());
        }

    } else {

        // no local issue with same first entry

        renameIssue(dir, remoteIssueId); // does nothing if no such local issue

        std::string branchName = BRANCH_PREFIX_ISSUES + remoteIssueId;        
        std::string origin = "origin/" BRANCH_PREFIX_ISSUES +  remoteIssueId;
        err = gitBranchCreate(dir, branchName, origin);
        if (err) {
            LOG_ERROR("gitPullIssue %s error (dir=%s) (2)", remoteIssueId.c_str(), dir.c_str());
        }
    }
    return err;
}

/** alternative
 *
 * list all commits of all issues
 * => map<branch, list<EntryId> > (local&remote)
 * => map<EntryId, branch> first entries (local&remote)
 * then ...
 */

static int gitPullIssues(const std::string &dir)
{
    GitIssueList ilist;
    int err = ilist.open(dir, "origin");
    if (err) {
        LOG_ERROR("Cannot load remote issues of project (%s)", dir.c_str());
        return -1;
    }

    while (0 == err) {
        IssueId remoteIssueId = ilist.getNext();
        if (remoteIssueId.empty()) break; // reached the end

        std::string branchName = "refs/remotes/origin/" BRANCH_PREFIX_ISSUES + remoteIssueId;

        EntryId firstEntry = gitGetFirstCommit(dir, branchName);
        if (firstEntry.empty()) {
            break; // unexpected error
        }

        err = gitPullIssue(dir, remoteIssueId, firstEntry);
    }

    ilist.close();
    return err;
}



static void printModifiedBranches(std::string gitPullStderr)
{
    // Sample git pull output:
    // From http://localhost:8090//myproject
    //   98115e6..44bd148  issues/147 -> origin/issues/147

    // print only lines that start by a space.
    while (!gitPullStderr.empty()) {
        std::string line = popToken(gitPullStderr, '\n', TOK_STRICT);
        if (!line.empty() && line[0] == ' ') LOG_CLI("%s", line.c_str());
    }
}

static int gitPull(const std::string &dir, bool isProject)
{
    Argv argv;
    std::string subStdout, subStderr;

    // For branch master, conflicts shall be manually resolved
    argv.set("git", "pull", "--rebase", 0);
    int err = Subprocess::launchSync(argv.getv(), 0, dir.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitPull: error: %d: stdout=%s, stderr=%s", err, subStdout.c_str(), subStderr.c_str());
        return err;
    } else {
        // show information about modified branches
        printModifiedBranches(subStderr);
    }

    if (isProject) {
        // pull branches issues/*
        gitPullIssues(dir);

        // TODO pull notes
    }


    return err;
}

static int gitPush(const std::string &dir)
{
    Argv argv;
    std::string subStdout, subStderr;

    // push all branches
    argv.set("git", "push", "--all", 0);
    int err = Subprocess::launchSync(argv.getv(), 0, dir.c_str(), 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("gitPush: error: %d: stdout=%s, stderr=%s", err, subStdout.c_str(), subStderr.c_str());
    }
    return err;
}


struct Permissions {
    std::map<std::string, Role> byProject; // mapping project name / role
    bool superadmin;
    Permissions(): superadmin(false) {}
};

/** Read the permissions from the remote server and update locally
 */
static Permissions updatePermissions(const HttpClientContext &ctx, const std::string &rooturl, const std::string &localRepo,
                                     const std::string username)
{
    Permissions permissions;

    // get the list of remote projects
    HttpRequest hr(ctx);
    hr.setUrl(rooturl + "/?format=text");
    hr.getRequestLines();

    if (hr.httpStatusCode != 200) {
        LOG_ERROR("Cannot read permissions from remote");
        return permissions;
    }

    // Lines are in the format: <permission> <project name>
    // Except the "superadmin" line, if any.
    std::list<std::string>::iterator line;

    FOREACH(line, hr.lines) {
        if ( (*line) == "superadmin") {
            permissions.superadmin = true;

        } else {
            std::string roleStr = popToken(*line, ' ');
            std::string projectName = *line;

            Role role = stringToRole(roleStr);

            if (role != ROLE_NONE) permissions.byProject[projectName] = role;
        }
    }

    // Update the local file that stores the permissions (used by smit ui)
    std::string data = User::serializePermissions(username, permissions.superadmin, permissions.byProject);
    std::string filePermissions = localRepo + "/" PATH_LOCAL_PERM;
    int err = writeToFile(filePermissions, data);
    if (err) {
        LOG_ERROR("Cannot store local permissions: %s", filePermissions.c_str());
    }
    return permissions;
}

static int cloneAll(const HttpClientContext &ctx, const std::string &rooturl, const std::string &localdir, const Permissions &perms)
{
    int err;

    // clone /public
    LOG_CLI("Cloning 'public'...");
    err = gitClone(rooturl+"/public", localdir, "public");
    if (err) {
        LOG_ERROR("Abort.");
        exit(1);
    }

    // clone the projects if the permissions allow it
    std::map<std::string, Role>::const_iterator perm;
    FOREACH(perm, perms.byProject) {
        std::string projectName = perm->first;
        Role role = perm->second;
        if (role <= ROLE_RO) {
            // do clone
            LOG_CLI("Cloning '%s'...", projectName.c_str());
            LOG_DIAG("role=%s", roleToString(role).c_str());
            std::string resource = "/" + Project::urlNameEncode(projectName);
            std::string projectDir = resource;
            err = gitClone(rooturl+"/"+resource, localdir, projectDir);
            if (err) {
                LOG_ERROR("Abort.");
                exit(1);
            }
        }
    }

    //

    // clone /.smit if permission granted
    if (perms.superadmin) {
        LOG_CLI("Cloning '.smit'...");
        err = gitClone(rooturl+"/.smit", localdir, ".smit");
        if (err) {
            LOG_ERROR("Abort.");
            exit(1);
        }
    }

    return 0;
}

static int pushAll(const std::string &localRepo, Permissions perms)
{
    int err;
    // push the projects if the permission granted
    std::map<std::string, Role>::const_iterator perm;
    FOREACH(perm, perms.byProject) {
        std::string projectName = perm->first;
        Role role = perm->second;
        if (role < ROLE_RO) {
            // do push
            LOG_CLI("Pushing '%s'...", projectName.c_str());
            LOG_DIAG("role=%s", roleToString(role).c_str());
            std::string projectDir = Project::urlNameEncode(projectName);
            err = gitPush(localRepo + "/" + projectDir);
            if (err) {
                LOG_ERROR("Abort.");
                exit(1);
            }
        }
    }

    if (perms.superadmin) {
        // push /public
        LOG_CLI("Pulling 'public'...");
        err = gitPush(localRepo + "/public");
        if (err) {
            LOG_ERROR("Abort.");
            exit(1);
        }

        // push .smit
        LOG_CLI("Pulling '.smit'...");
        err = gitPush(localRepo + "/.smit");
        if (err) {
            LOG_ERROR("Abort.");
            exit(1);
        }
    }

    return 0; //ok
}

/** Pull issues of all local projects
  * - pull entries
  * - pull files
  *
  * @param all
  *     Also pull: tags, public, templates
  */
static int pullAll(const std::string &localRepo, Permissions perms)
{
    // pull /public
    LOG_CLI("Pulling 'public'...");
    int err = gitPull(localRepo + "/public", false);
    if (err) {
        LOG_ERROR("Abort.");
        exit(1);
    }

    // pull the projects if the permissions allow it
    std::map<std::string, Role>::const_iterator perm;
    FOREACH(perm, perms.byProject) {
        std::string projectName = perm->first;
        Role role = perm->second;
        if (role <= ROLE_RO) {
            // do clone
            LOG_CLI("Pulling '%s'...", projectName.c_str());
            LOG_DIAG("role=%s", roleToString(role).c_str());
            std::string projectDir = Project::urlNameEncode(projectName);
            err = gitPull(localRepo + "/" + projectDir, true);
            if (err) {
                LOG_ERROR("Abort.");
                exit(1);
            }
        }
    }

    //

    // clone /.smit if permission granted
    if (perms.superadmin) {
        LOG_CLI("Pulling '.smit'...");
        err = gitPull(localRepo + "/.smit", false);
        if (err) {
            LOG_ERROR("Abort.");
            exit(1);
        }
    }

    return 0; //ok
}

std::string getSmitDir(const std::string &dir)
{
    return dir + "/" SMIT_LOCAL;
}

void createSmitDir(const std::string &dir)
{
    LOG_DEBUG("createSmitDir(%s)...", dir.c_str());

    std::string smitDir = getSmitDir(dir);

    if (isDir(smitDir)) return; // Already exists

    int r = mkdir(smitDir);
    if (r != 0) {
        fprintf(stderr, "Cannot create directory '%s': %s\n", getSmitDir(dir).c_str(), strerror(errno));
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

/** Sign-in and return the sessid cookie
  *
  * @return
  *    The format is the cookie format: key=value
  */
std::string signin(const std::string &rooturl, const std::string &user,
                   const std::string &passwd, const HttpClientContext &ctx)
{
    HttpRequest hr(ctx);

    hr.setUrl(rooturl + "/signin?format=text");

    // specify the POST data
    std::string params;
    params += "username=";
    params += urlEncode(user);
    params += "&password=";
    params += urlEncode(passwd);
    hr.post(params);

    // Get the sessiond id
    // Look for the cookie smit-sessid-*
    std::map<std::string, Cookie>::iterator c;
    FOREACH(c, hr.cookies) {
        std::string cookie = c->first; // name of the cookie
        if (0 == cookie.compare(0, strlen(COOKIE_SESSID_PREFIX), COOKIE_SESSID_PREFIX)) {
            // Got it
            cookie += "=" + c->second.value;
            return cookie;
        }
    }

    return "";
}

void parseUrl(std::string url, std::string &scheme, std::string &host, std::string &port, std::string &resource)
{
    scheme = popToken(url, ':');
    std::string hostAndPort = popToken(url, '/');
    host = popToken(hostAndPort, ':');
    port = hostAndPort;
    resource = url;
}

/** Store the cookie of the sessid in .smit/sessid
  */
void storeSessid(const std::string &dir, const std::string &sessid)
{
    LOG_DEBUG("storeSessid(%s, %s)...", dir.c_str(), sessid.c_str());
    std::string path = dir + "/" PATH_SESSID;
    int r = writeToFile(path, sessid + "\n");
    if (r < 0) {
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

/** Load the cookie of the sessid
  */
std::string loadSessid(const std::string &dir)
{
    std::string path = dir + "/" PATH_SESSID;
    std::string sessid;
    loadFile(path.c_str(), sessid);
    trim(sessid);

    return sessid;
}

static void storeRemoteUrl(const std::string &dir, const std::string &url)
{
    LOG_DEBUG("storeRemoteUrl(%s, %s)...", dir.c_str(), url.c_str());
    std::string path = dir + "/" PATH_REMOTE_URL;
    int r = writeToFile(path, url + "\n");
    if (r < 0) {
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

static std::string loadRemoteUrl(const std::string &dir)
{
    std::string path = dir + "/" PATH_REMOTE_URL;
    std::string url;
    loadFile(path.c_str(), url);
    trim(url);

    return url;
}


const char* SCHEMES[] = { "https://", "http://", 0 };

/** Store the session id in the git-credential-store format
 *
 *  Eg: https://user:pass@example.com
 */
static void storeGitCredential(const std::string &dir, const std::string &url, const std::string &username, const std::string &passwd)
{
    std::string credential;

    // insert the user:pass in the url
    const char **ptr = SCHEMES;
    while (*ptr) {
        if (0 == strncmp(url.c_str(), *ptr, strlen(*ptr))) {
            credential = *ptr;
            credential += username + ":" + passwd + "@" + url.substr(strlen(*ptr));
            break;
        }
        ptr++;
    }

    if (credential.empty()) {
        LOG_ERROR("Unsupported scheme (%s). Must be http:// or https://", url.c_str());
        exit(1);
    }

    std::string path = dir + "/" PATH_GIT_CREDENTIAL;
    int r = writeToFile(path, credential + "\n");
    if (r < 0) {
        fprintf(stderr, "Abort.\n");
        exit(1);
    }
}

/** Sign-in or reuse existing cookie
  *
  * Sign-in using the following means (in this order)
  * - if parameters username not null, use this username
  * - else use the username or session id stored locally
  * - else ask interactively
  *
  * If the session id is used and fails, then ask for username/password.
  *
  * @param dir
  *     Path to local smit repository (may be null).
  *     If not null and the sign-in is successful, the session cookie
  *     is stored.
  * @param username[in/out]
  *     Optional. If not given, try to use the cookie.
  *     Fulfilled only if username is obtained interactively, and sign-in successful.
  * @param password
  * @param[in/out] ctx
  *     This context gives the URL to contact (ctx.rooturl).
  *     The received session id is stored in ctx.httpCtx.cookieSessid.
  *
  */
static int establishSession(const char *dir, std::string &username, const char *password, PullContext &ctx)
{
    curl_global_init(CURL_GLOBAL_ALL);

    if (ctx.rooturl.empty()) {
        if (dir) {
            // get the remote root url from local configuration file
            ctx.rooturl = loadRemoteUrl(dir);
        }
        if (ctx.rooturl.empty()) {
            LOG_CLI("Cannot get remote url.\n");
            return -1;
        }
    }

    bool needSignin = true;

    std::string user;

    if (!username.empty()) {
        // do the signing-in with the given username / password
        user = username;

    } else if (dir) {
        // try with the sessid
        ctx.httpCtx.cookieSessid = loadSessid(dir);
        int r = testSessid(ctx.rooturl, ctx.httpCtx);

        if (r < 0) {
            // failed (session may have expired), then prompt for user name/password
            LOG_CLI("Session not valid or expired. Please sign-in.\n");
            user = getString("Username: ", false);

        } else {
            needSignin = false;
        }

    } else {
        user = getString("Username: ", false);
    }

    if (needSignin) {

        std::string pass;
        if (!password) pass = getString("Password: ", true);
        else pass = password;

        ctx.httpCtx.cookieSessid = signin(ctx.rooturl, user, pass, ctx.httpCtx);
        if (!ctx.httpCtx.cookieSessid.empty()) {
            // signin successful
            username = user;
            if (dir) {
                // store
                storeSessid(dir, ctx.httpCtx.cookieSessid);
                storeUsername(dir, username);
                storeRemoteUrl(dir, ctx.rooturl);
                std::string sessid = ctx.httpCtx.cookieSessid;
                popToken(sessid, '='); // remove the <key>= part
                storeGitCredential(dir, ctx.rooturl, BASIC_AUTH_SESSID, sessid);
            }
        }
    }

    LOG_DEBUG("cookieSessid: %s", ctx.httpCtx.cookieSessid.c_str());

    if (ctx.httpCtx.cookieSessid.empty()) {
        fprintf(stderr, "Authentication failed\n");
        return -1; // authentication failed
    }
    return 0;
}

Args *setupCloneOptions()
{
    Args *args = new Args();
    args->setDescription("Clones a repository into a newly created directory.\n"
                         "\n"
                         "Args:\n"
                         "  <url>        The repository to clone from.\n"
                         "  <directory>  The name of a new directory to clone into.\n"
                         "\n"
                         "Example:\n"
                         "  smit clone http://example.com:8090 localDir\n"
                         );
    args->setUsage("smit clone [options] <url> <directory>");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setNonOptionLimit(2);
    return args;
}

int helpClone(const Args *args)
{
    if (!args) args = setupCloneOptions();
    args->usage(true);
    return 1;
}

int cmdClone(int argc, char **argv)
{
    PullContext pullCtx;
    Args *args = setupCloneOptions();
    args->parse(argc-1, argv+1);
    const char *pUsername = args->get("user");
    const char *pPasswd = args->get("passwd");
    if (args->get("insecure")) pullCtx.httpCtx.tlsInsecure = true;
    pullCtx.httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DIAG);
    } else {
        setLoggingLevel(LL_ERROR);
    }

    // manage non-option ARGV elements
    const char *url = args->pop();
    const char *localdir = args->pop();
    if (!url || !localdir) {
        LOG_CLI("Missing argument.\n");
        exit(1);
    }

    setLoggingOption(LO_CLI);

    if (fileExists(localdir)) {
        LOG_CLI("Cannot clone: '%s' already exists\n", localdir);
        exit(1);
    }

    // Do not create the local clone until the signin succeeds

    pullCtx.rooturl = url;
    std::string username;
    if (pUsername) username = pUsername;
    int err = establishSession(0, username, pPasswd, pullCtx);
    if (err) return 1;

    int r = mkdir(localdir);
    if (r != 0) {
        LOG_CLI("Cannot create directory '%s': %s\n", localdir, strerror(errno));
        exit(1);
    }

    // create persistent configuration of the local clone
    createSmitDir(localdir);
    storeSessid(localdir, pullCtx.httpCtx.cookieSessid);
    storeUsername(localdir, username);
    storeRemoteUrl(localdir, url);
    std::string sessid = pullCtx.httpCtx.cookieSessid;
    popToken(sessid, '='); // remove the <key>= part
    storeGitCredential(localdir, url, BASIC_AUTH_SESSID, sessid);

    //
    Permissions perms = updatePermissions(pullCtx.httpCtx, url, localdir, username);

    r = cloneAll(pullCtx.httpCtx, url, localdir, perms);
    if (r < 0) {
        fprintf(stderr, "Clone failed. Abort.\n");
        exit(1);
    }

    terminateSession();

    return 0;
}

Args *setupGetOptions()
{
    Args *args = new Args();
    args->setDescription("Get with format x-smit.\n");
    args->setUsage("smit get [options] <root-url> <resource>");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("repo", 0, "use session of existing repository", 1);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setOpt("use-signin-cookie", 0, "Use the cookie of the cloned repository for signin in", 0);
    args->setNonOptionLimit(2);
    return args;
}

int helpGet(const Args *args)
{
    if (!args) args = setupGetOptions();
    args->usage(true);
    return 1;
}

int cmdGet(int argc, char **argv)
{
    PullContext pullCtx;
    Args *args = setupGetOptions();
    args->parse(argc-1, argv+1);
    const char *pusername = args->get("user");
    const char *password = args->get("passwd");
    const char *repo = args->get("repo");
    if (args->get("insecure")) pullCtx.httpCtx.tlsInsecure = true;
    pullCtx.httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DEBUG);
    } else {
        setLoggingLevel(LL_ERROR);
    }

    // manage non-option ARGV elements
    const char *rooturl = args->pop();
    const char *resource = args->pop();
    if (!rooturl || !resource) {
        LOG_CLI("Missing argument.\n");
        exit(1);
    }

    setLoggingOption(LO_CLI);

    pullCtx.rooturl = rooturl;
    std::string username;
    if (pusername) username = pusername;
    int r = establishSession(repo, username, password, pullCtx);
    if (r != 0) return 1;

    // do the GET
    HttpRequest hr(pullCtx.httpCtx);
    hr.setUrl(std::string(rooturl) + std::string(resource) + "?format=text");
    hr.getFileStdout();

    terminateSession();

    return 0;
}

Args *setupPullOptions()
{
    Args *args = new Args();
    args->setDescription("Incorporates changes from a remote repository into a local copy.");
    args->setUsage("smit pull [options] [<local-repository>]");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setNonOptionLimit(1);
    return args;
}

int helpPull(const Args *args)
{
    if (!args) args = setupPullOptions();
    args->usage(true);
    return 1;
}

int cmdPull(int argc, char **argv)
{
    PullContext pullCtx;
    Args *args = setupPullOptions();
    args->parse(argc-1, argv+1);
    const char* pusername = args->get("user");
    const char *password = args->get("passwd");
    if (args->get("insecure")) pullCtx.httpCtx.tlsInsecure = true;
    pullCtx.httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DEBUG);
    } else {
        setLoggingLevel(LL_ERROR);
    }

    // manage non-option ARGV elements
    const char *dir = args->pop();
    if (!dir) dir = "."; // default value is current directory

    setLoggingOption(LO_CLI);

    // get the remote url from local configuration file
    pullCtx.rooturl = loadRemoteUrl(dir);
    if (pullCtx.rooturl.empty()) {
        LOG_CLI("Cannot get remote url.\n");
        exit(1);
    }

    std::string username;
    if (pusername) username = pusername;
    int r = establishSession(dir, username, password, pullCtx);
    if (r != 0) return 1;

    if (username.empty()) {
        username = loadUsername(dir);
    }

    Permissions perms = updatePermissions(pullCtx.httpCtx, pullCtx.rooturl, dir, username);

    // TODO if some remote git repo has not been cloned yet, do a clone

    // pull new entries and new attached files of all projects
    r = pullAll(dir, perms);

    terminateSession();

    if (r < 0) return 1;
    else return 0;
}


Args *setupPushOptions()
{
    Args *args = new Args();
    args->setDescription("Push local changes to a remote repository.");
    args->setUsage("smit push [options] [<local-repository>]");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("dry-run", 'n', "do not push, but show what would have been pushed", 0);
    args->setOpt("user", 0, "specify user name", 1);
    args->setOpt("passwd", 0, "specify password", 1);
    args->setOpt("insecure", 0, "do not verify the server certificate", 0);
    args->setOpt("cacert", 0,
                 "specify the CA certificate, in PEM format, for authenticating the server\n"
                 "(HTTPS only)", 1);
    args->setNonOptionLimit(1);
    return args;
}

int helpPush(const Args *args)
{
    if (!args) args = setupPushOptions();
    args->usage(true);
    return 1;
}

int cmdPush(int argc, char **argv)
{
    PullContext pushCtx;

    Args *args = setupPushOptions();
    args->parse(argc-1, argv+1);

    const char *pusername = args->get("user");
    const char *password = args->get("passwd");
    if (args->get("insecure")) pushCtx.httpCtx.tlsInsecure = true;
    pushCtx.httpCtx.tlsCacert = args->get("cacert");
    if (args->get("verbose")) {
        setLoggingLevel(LL_DEBUG);
    } else {
        setLoggingLevel(LL_ERROR);
    }
    bool dryRun = false;
    if (args->get("dry-run")) dryRun = true;

    // manage non-option ARGV elements
    const char *dir = args->pop();
    if (!dir) dir = "."; // default value is current directory

    const char *unexpected = args->pop();
    if (unexpected) {
        // normally already managed by the Args class
        LOG_CLI("Too many arguments.\n\n");
        return helpPush(args);
    }

    setLoggingOption(LO_CLI);
    // set log level to hide INFO logs

    std::string username;
    if (pusername) username = pusername;
    int r = establishSession(dir, username, password, pushCtx);
    if (r != 0) return 1;

    if (username.empty()) {
        username = loadUsername(dir);
    }

    Permissions perms = updatePermissions(pushCtx.httpCtx, pushCtx.rooturl, dir, username);

    pushAll(dir, perms);

    terminateSession();

    if (r < 0) return 1;
    else return 0;


}
