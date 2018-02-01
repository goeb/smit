#include <string>
#include <sys/stat.h>

#include "gitconfig.h"
#include "repository/db.h"
#include "project/Project.h"
#include "utils/logging.h"
#include "utils/filesystem.h"

int setupGitHookDenyCurrentBranch(const char *dir)
{
    std::string subStdout, subStderr;
    Argv argv;
    argv.set("git", "config", "receive.denyCurrentBranch", "updateInstead", 0);
    int err = Subprocess::launchSync(argv.getv(), 0, dir, 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("Cannot setup receive.denyCurrentBranch in '%s': %s", dir, subStderr.c_str());
        return -1;
    }
    return 0;
}

const char *UPDATE_HOOK_SCRIPT = ""
"#!/bin/sh\n"
"refname=\"$1\"\n"
"case \"$refname\" in\n"
"    refs/notes/commit|refs/heads/issues/*)\n"
"        if [ \"$SMIT_ROLE\" != rw ]; then\n"
"            echo \"SMIT_ROLE $SMIT_ROLE cannot update $refname\" >&2\n"
"            exit 1\n"
"        fi\n"
"        ;;\n"
"    refs/heads/master)\n"
"        if [ \"$SMIT_ROLE\" != admin ]; then\n"
"            echo \"SMIT_ROLE $SMIT_ROLE cannot update $refname\" >&2\n"
"            exit 1\n"
"        fi\n"
"        ;;\n"
"    *)\n"
"        echo \"Invalid ref $refname\" >&2\n"
"        exit 1\n"
"        ;;\n"
"esac\n"
"exit 0\n";


int setupGitServerSideHooks(const char *repo)
{
    std::string dir;
    int err;

    // TODO git config receive.denyNonFastForwards true
    // TODO git config receive.denyDeletes true

    // git repo "public"
    dir = std::string(repo) + "/public";
    err = setupGitHookDenyCurrentBranch(dir.c_str());
    if (err) {
        return -1;
    }

    // git repo ".smit"
    dir = std::string(repo) + "/" PATH_REPO;
    err = setupGitHookDenyCurrentBranch(dir.c_str());
    if (err) {
        return -1;
    }

    // git repos of all projects
    Project *p = Database::Db.getNextProject(0);
    while (p) {
        err = setupGitHookDenyCurrentBranch(p->getPath().c_str());
        if (err) {
            return -1;
        }
        // setup 'update' hook
        std::string updateHookPath = p->getPath() + "/.git/hooks/update";
        err = writeToFile(updateHookPath, UPDATE_HOOK_SCRIPT);
        if (err) {
            return -1;

        }

        // make the script executable
        mode_t mode = S_IRUSR | S_IXUSR;
        err = chmod(updateHookPath.c_str(), mode);
        if (err) {
            LOG_ERROR("Cannot chmod %s: %s", updateHookPath.c_str(), STRERROR(errno));
            return -1;
        }

        p = Database::Db.getNextProject(p);
    }
    return 0;
}
