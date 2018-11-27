#include <string>
#include <sys/stat.h>

#include "gitconfig.h"
#include "repository/db.h"
#include "project/Project.h"
#include "utils/logging.h"
#include "utils/filesystem.h"
#include "utils/cpio.h"

static int setGitConfig(const char *dir, const char *key, const char *value)
{
    LOG_INFO("setGitConfig: dir=%s, key=%s, value=%s", dir, key, value);

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


const char *GIT_CONFIG_TABLE[][2] = {
    { "receive.denyCurrentBranch", "updateInstead" }, // automatically update the checked-out working copy after push
    { "receive.denyNonFastForwards", "true" },        // prevent push --force
    { "receive.denyDeletes", "true" },                // prevent removing a branch
    { NULL, NULL }
};


static int setupGitConfig(const char *dir)
{
    LOG_INFO("setupGitConfig on %s", dir);

    int i = 0;
    while (GIT_CONFIG_TABLE[i][0]) {

        int err = setGitConfig(dir, GIT_CONFIG_TABLE[i][0], GIT_CONFIG_TABLE[i][1]);
        if (err) return -1;

        i++;
    }

    return 0;
}


static int setupGitHook(const char *repo, const char *hookName)
{
    // setup 'update' hook
    int err;

    LOG_INFO("setupGitHook '%s' on %s", hookName, repo);

    std::string cpioSrc = std::string("githooks/") + hookName;
    std::string fileDest = std::string(repo) + "/.git/hooks/" + hookName;
    err = cpioExtractFile(cpioSrc.c_str(), fileDest.c_str());
    if (err) {
        return -1;
    }

    // make the script executable
    mode_t mode = S_IRUSR | S_IXUSR;
    err = chmod(fileDest.c_str(), mode);
    if (err) {
        LOG_ERROR("Cannot chmod %s: %s", fileDest.c_str(), STRERROR(errno));
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

        err = setupGitHook(p->getPath().c_str(), "update");
        if (err) return -1;

        err = setupGitHook(p->getPath().c_str(), "post-receive");
        if (err) return -1;

        p = Database::Db.getNextProject(p);
    }
    return 0;
}
