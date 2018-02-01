#include <string>
#include <sys/stat.h>

#include "gitconfig.h"
#include "repository/db.h"
#include "project/Project.h"
#include "utils/logging.h"
#include "utils/filesystem.h"

static int setGitConfig(const char *dir, const char *key, const char *value)
{
    std::string subStdout, subStderr;
    Argv argv;
    argv.set("git", "config", key, value, 0);
    int err = Subprocess::launchSync(argv.getv(), 0, dir, 0, 0, subStdout, subStderr);
    if (err) {
        LOG_ERROR("Cannot set git config %s %s (%s): %s", key, value, dir, subStderr.c_str());
        return -1;
    }
    return 0;
}

static int setupGitConfig(const char *dir)
{
    int err;

    // automatically update the checked-out working copy after push
    err = setGitConfig(dir, "receive.denyCurrentBranch", "updateInstead");
    if (err) return -1;

    // prevent push --force
    err = setGitConfig(dir, "receive.denyNonFastForwards", "true");
    if (err) return -1;

    // prevent removing a branch
    err = setGitConfig(dir, "receive.denyDeletes", "true");
    if (err) return -1;

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

static int setupGitHookUpdate(const char *repo)
{
    // setup 'update' hook
    int err;

    std::string updateHookPath = std::string(repo) + "/.git/hooks/update";
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

    return 0;
}


int setupGitServerConfig(const char *repo)
{
    std::string dir;
    int err;

    // git repo "public"
    dir = std::string(repo) + "/public";
    err = setupGitConfig(dir.c_str());
    if (err) {
        return -1;
    }

    // git repo ".smit"
    dir = std::string(repo) + "/" PATH_REPO;
    err = setupGitConfig(dir.c_str());
    if (err) {
        return -1;
    }

    // git repos of all projects
    Project *p = Database::Db.getNextProject(0);
    while (p) {

        err = setupGitConfig(p->getPath().c_str());
        if (err) return -1;

        err = setupGitHookUpdate(p->getPath().c_str());
        if (err) return -1;

        p = Database::Db.getNextProject(p);
    }
    return 0;
}
