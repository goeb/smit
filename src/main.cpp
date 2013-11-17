
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>

#include "mongoose.h"
#include "httpdHandlers.h"
#include "logging.h"
#include "db.h"
#include "cpio.h"
#include "session.h"


void usage()
{
    printf("Usage: smit <command> [<args>]\n"
           "\n"
           "Commands:\n"
           "\n"
           "    init [<directory>]\n"
           "        Initialize a repository in an existing empty directory. A repository\n"
           "        is a directory where the projects are stored.\n"
           "\n"
           "    addproject <project-name> [-d <repository>]\n"
           "        Add a new project, with a default structure. The structure\n"
           "        may be modified online by an admin user.\n"
           "\n"
           "    adduser <user-name> [--passwd <password>] [--project <project-name> <role>] [--superadmin] \\\n"
           "                        [-d <repository>]\n"
           "        Add a user on one or several projects. The role must be one of: admin, rw, ro, ref.\n"
           "\n"
           "    serve [<repository>] [--listen-port <port>]\n"
           "\n"
           "When a repository is not specified, the current working directory is assumed.\n"
           "\n"
           "Roles:\n"
           "    superadmin  able to create projects and manage users\n"
           "    admin       able to modify an existing project\n"
           "    rw          able to add and modify issues\n"
           "    ro          able to read issues\n"
           "    ref         may not access a project, but may be referenced\n"
           "\n");
    exit(1);
}

int initRepository(const char *exec, const char *directory)
{
    DIR *dirp;
    dirp = opendir(directory);
    if (!dirp) {
        LOG_ERROR("Cannot open directory '%s': %s", directory, strerror(errno));
        return 1;
    }

    // check that the directory is empty
    struct dirent *f;
    while ((f = readdir(dirp)) != NULL) {
        if (0 == strcmp(f->d_name, ".")) continue;
        if (0 == strcmp(f->d_name, "..")) continue;
        LOG_ERROR("Directory not empty. Aborting.");
        closedir(dirp);
        return 2;
    }
    closedir(dirp);

    // ok, extract the files for the new repository
    int r = cpioExtractFile(exec, "public", directory);
    if (r < 0) {
        LOG_ERROR("Error while extracting files. r=%d", r);
        return 3;
    }
    LOG_INFO("Done.");
    return 0;
}

int addProject(int argc, const char **args)
{
    int i = 0;
    const char *repo = ".";
    const char *projectName = 0;
    while (i<argc) {
        const char *arg = args[i]; i++;
        if (0 == strcmp(arg, "-d")) {
            if (i<argc) repo = args[i];
            else usage();

        } else if (!projectName) {
            projectName = arg;

        } else {
            usage();
        }
    }

    if (!projectName) usage();

    int r = Project::createProject(repo, projectName);
    if (r < 0) return 1;
    return 0;
}

int addUser(int argc, const char **args)
{
    int i = 0;
    const char *repo = ".";
    const char *userName = 0;
    while (i<argc) {
        const char *arg = args[i]; i++;
        if (0 == strcmp(arg, "-d")) {
            if (i<argc) repo = args[i];
            else usage();

        } else if (!userName) {
            userName = arg;

        } else {
            usage();
        }
    }

    if (!userName) usage();
    // TODO
    return 0;

}
int serveRepository(int argc, const char **args)
{
    int i = 0;
    const char *listenPort = "8080";
    const char *repo = ".";
    while (i<argc) {
        const char *arg = args[i]; i++;
        if (0 == strcmp(arg, "--listen-port")) {
            if (i<argc) listenPort = args[i];
            else usage();
        } else if (!repo) {
            repo = arg;
        } else {
            usage();
        }
    }

    // Load all projects
    dbLoad(repo);
    UserBase::load(repo);
    struct mg_context *ctx;
    const char *options[] = {"listening_ports", listenPort, "document_root", repo, NULL};
    struct mg_callbacks callbacks;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.begin_request = begin_request_handler;
    callbacks.upload = upload_handler;

    LOG_DEBUG("Starting httpd server on port %s", options[1]);
    ctx = mg_start(&callbacks, NULL, options);

    while (1) sleep(1); // block until ctrl-C
    getchar();  // Wait until user hits "enter"
    mg_stop(ctx);

    return 0;
}

int main(int argc, const char **argv)
{
    if (argc < 2) usage();

    int i = 1;
    const char *command = 0;
    while (i<argc) {

        command = argv[1]; i++;

        if (0 == strcmp(command, "init")) {
            const char *dir = ".";
            if (i < argc) dir = argv[i];
            return initRepository(argv[0], dir);

        } else if (0 == strcmp(command, "serve")) {
            return serveRepository(argc-2, argv+2);

        } else if (0 == strcmp(command, "addproject")) {
            return addProject(argc-2, argv+2);

        } else if (0 == strcmp(command, "adduser")) {
            return addUser(argc-2, argv+2);

        }

    }
    if (0 == strcmp(argv[1], "extract")) {
        cpioExtractFile(argv[0], argv[2], argv[3]);
        exit(0);
    }

    return 0;
}
