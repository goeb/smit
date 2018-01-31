/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
//#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <getopt.h>

#include "server/HttpContext.h"
#include "server/httpdHandlers.h"
#include "server/httpdUtils.h"
#include "utils/logging.h"
#include "utils/cpio.h"
#include "utils/identifiers.h"
#include "utils/filesystem.h"
#include "utils/gitTools.h"
#include "utils/subprocess.h"
#include "utils/argv.h"
#include "repository/db.h"
#include "user/session.h"
#include "global.h"
#include "local/localClient.h"

#ifdef CURL_ENABLED
  #include "local/clone.h"
#endif

#define DEFAULT_REPO_CONFIG "editDelay 600\n" \
                            "sessionDuration 129600\n"

void usage()
{
    printf("Usage: smit <command> [<args>]\n"
           "\n"
           "The smit commands are:\n"
           "\n"
#ifdef CURL_ENABLED
           "  clone       Clone a smit repository\n"
#endif
           "  init        Initialise a smit repository\n"
           "  issue       Print or modify an issue in a local project\n"
           "  project     List, create, or update a smit project\n"
#ifdef CURL_ENABLED
           "  pull        Fetch from and merge with a remote repository\n"
           "  push        Push local changes to a remote repository\n"
#endif
           "  serve       Start a smit web server\n"
           "  user        List, create, or update a smit user\n"
           "  ui          Browse a local smit repository (read-only)\n"
           "  version     Print the version\n"
           "  help\n"
           "\n"
           "See 'smit help <command>' for more information on a specific command.\n"
           );
    exit(1);
}

int helpInit()
{
    printf("Usage: smit init [<directory>]\n"
           "\n"
           "  Initialize a repository, where the smit projects are to be stored.\n"
           "\n"
           "  If the directory exists, it must be empty.\n"
           "  If the directory does not exist, it is created.\n"
           "  If the directory is not given, . is used by default.\n"
           );
    return 1;
}

int initRepository(int argc, char **argv)
{
    const char *directory = ".";

    int c;
    int err;
    int optionIndex = 0;
    struct option longOptions[] = { {NULL, 0, NULL, 0}  };
    while ((c = getopt_long(argc, argv, "", longOptions, &optionIndex)) != -1) {
        switch (c) {
        case '?': // incorrect syntax, a message is printed by getopt_long
            return helpInit();
            break;
        default:
            printf("?? getopt returned character code 0x%x ??\n", c);
            return helpInit();
        }
    }
    // manage non-option ARGV elements
    if (optind < argc) {
        directory = argv[optind];
        optind++;
    }
    if (optind < argc) {
        printf("Too many arguments.\n\n");
        return helpInit();
    }

    DIR *dirp;
    dirp = opendir(directory);
    if (!dirp) {
        // try create it
        int r = mkdir(directory);
        if (r != 0) {
            LOG_ERROR("Cannot create directory '%s': %s", directory, strerror(errno));
            return 1;
        }
        dirp = opendir(directory);
        if (!dirp) {
            LOG_ERROR("Cannot open just created directory '%s': %s", directory, strerror(errno));
            return 1;
        }
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

    // Extract the files 'public' to the new repository
    int r = cpioExtractFile("public", directory);
    if (r < 0) {
        LOG_ERROR("Error while extracting 'public/*': r=%d", r);
        return 1;
    }

    // init the git repo
    std::string gitrepoPublic = std::string(directory) + "/public";
    err = gitAddCommitDir(gitrepoPublic, "local-user");
    if (err) {
        LOG_ERROR("Error while initializing '%s': %d", gitrepoPublic.c_str(), err);
        return 1;
    }

    r = UserBase::initUsersFile(directory);
    if (r < 0) {
        LOG_ERROR("Abort.");
        return 1;
    }

    std::string gitrepoSmit = std::string(directory) + "/" PATH_REPO;

    // init config
    err = writeToFile(gitrepoSmit + "/config", DEFAULT_REPO_CONFIG);
    if (err) {
        LOG_ERROR("Error while initializing config in '%s': %d", gitrepoSmit.c_str(), err);
        return 1;
    }

    err = gitAddCommitDir(gitrepoSmit, "local-user");
    if (err) {
        LOG_ERROR("Error while initializing '%s': %d", gitrepoSmit.c_str(), err);
        return 1;
    }

    LOG_INFO("Done.");
    return 0;
}

int helpProject()
{
    printf("Usage: smit project [options] [<project-path> [<action> ...]]\n"
           "\n"
           "  List, create, or update a smit project.\n"
           "\n"
           "  <project-path>\n"
           "        Path to a project or a repository\n"
           "\n"
           "  <action>\n"
           "        Update the project configuration. Non compatible with\n"
           "        option '-a'.\n"
           "\n"
           "Options:\n"
           "  -a    List all projects under the given path.\n"
           "\n"
           "  -c    Create a project, with a default structure. The structure\n"
           "        may be modified online by an admin user.\n"
           "\n"
           "  -l    Print the project configuration.\n"
           "\n"
           "Example:\n"
           "  smit project -c example addProperty \"priority select high low\"\n"
           "  smit project example addProperty \"friendly_name text\"\n"
           "  smit project example delProperty friendly_name\n"
          );
    return 1;
}

int cmdProject(int argc, char **argv)
{
    const char *path = ".";
    bool create = false;
    bool printall = false;
    bool listconfig = false;

    int c;
    int optionIndex = 0;
    struct option longOptions[] = { {NULL, 0, NULL, 0}  };
    while ((c = getopt_long(argc, argv, "acl", longOptions, &optionIndex)) != -1) {
        switch (c) {
        case 0: // manage long options
            break;
        case 'a':
            printall = true;
            break;
        case 'c':
            create = true;
            break;
        case 'l':
            listconfig = true;
            break;
        case '?': // incorrect syntax, a message is printed by getopt_long
            return helpProject();
            break;
        default:
            printf("?? getopt returned character code 0x%x ??\n", c);
            return helpProject();
        }
    }
    // manage non-option ARGV elements
    if (optind < argc) {
        path = argv[optind];
        optind++;
    }
    std::list<std::pair<std::string, std::string> > updateActions;
    while (optind < argc) {
        // actions go by pair
        std::string verb = argv[optind];
        optind++;

        if (verb == "addProperty" || verb == "numberIssues") {
            // ok
        } else {
            printf("Invalid action '%s'. Allowed values are: \n"
                   "  addProperty\n"
                   "  numberIssues\n"
                   "\n", verb.c_str());
            return helpProject();
        }

        if (optind >= argc) {
            printf("Invalid action '%s': missing second argument\n\n", verb.c_str());
            return helpProject();
        }

        std::string arg = argv[optind];
        optind++;

        updateActions.push_back(std::make_pair(verb, arg));
    }

    // set log level to hide INFO logs
    setLoggingLevel(LL_ERROR);
    setLoggingOption(LO_CLI);

    if (printall) {
        Database::loadProjects(path, true);

    } else {
        if (create) {
            if (fileExists(path)) {
                printf("Cannot create project: existing file or directory '%s'\n", path);
                return 1;
            }
            std::string resultingPath;
            std::string projectName = getBasename(path);
            std::string repo = getDirname(path);
            int r = Project::createProjectFiles(repo, projectName, resultingPath, "anonymous");
            if (r < 0) return 1;
            printf("Project created: %s\n", resultingPath.c_str());
        }
        Database::loadProjects(path, false); // do not recurse in sub directories

        if (updateActions.size() > 0) {
            Project *p = Database::Db.getNextProject(0);
            if (!p) {
                printf("Cannot update project '%s': no project found\n", path);
                return 1;
            }

            ProjectConfig config = p->getConfig();

            std::list<std::pair<std::string, std::string> >::iterator action;
            FOREACH(action, updateActions) {
                if (action->first == "addProperty") {
                    std::list<std::string> tokens = split(action->second, " \t");
                    int r = config.addProperty(tokens);
                    if (r != 0) {
                        printf("Cannot addProperty: check the syntax\n");
                        return 1;
                    }
                } else if (action->first == "numberIssues") {
                    if (action->second == "global") config.numberIssueAcrossProjects = true;
                    else config.numberIssueAcrossProjects = false;
                }
            }

            int r = p->modifyConfig(config, "");
            if (r != 0) {
                printf("Cannot modify project config\n");
                return 1;
            }
        }
    }

    const Project *p = Database::Db.getNextProject(0);
    while (p) {
        if (listconfig) {
            std::string config = p->getConfig().serialize();
            printf("Configuration of project '%s':\n", p->getName().c_str());
            printf("%s\n", config.c_str());
        } else {
            // simply list projects
            printf("%s: %ld issues\n", p->getName().c_str(), L(p->getNumIssues()));
        }
        p = Database::Db.getNextProject(p);
    }
    printf("%lu project(s)\n", L(Database::Db.getNumProjects()));

    return 0;
}

int showUser(const User &u)
{
    printf("%s", u.username.c_str());
    if (u.superadmin) printf(" (superadmin)");
    if (!u.authHandler) printf(", no password");
    else printf(", %s", u.authHandler->serialize().c_str());
    printf("\n");

    std::map<std::string, enum Role>::const_iterator project;
    FOREACH(project, u.rolesOnProjects) {
        printf("    %s: %s\n", project->first.c_str(), roleToString(project->second).c_str());
    }
    printf("\n");
    return 0;
}

int helpUser()
{
    printf("Usage: 1. smit user\n"
           "       2. smit user <name> [options] [global-options]\n"
           "\n"
           "  1. List all users and their configuration.\n"
           "  2. With no option, print the configuration of a user.\n"
           "     With options, create or update a user.\n"
           "\n"
           "Options:\n"
           "  --passwd <pw>     set the password\n"
           "  --no-passwd       delete the password (leading to impossible login)\n"
           "  --project <project-name>:<role>\n"
           "                    set a role (ref, ro, rw, admin) on a project\n"
           "  --superadmin      set the superadmin priviledge (ability to create\n"
           "                    projects and manage users via the web interface)\n"
           "  --no-superadmin   remove the superadmin priviledge\n"
           "  -d <repo>         select a repository by its path (by default . is used)\n"
           "  -v                be verbose (debug)"
           "\n"
           "Roles:\n"
           "    admin       able to modify an existing project\n"
           "    rw          able to create and modify issues\n"
           "    ro          able to read issues\n"
           "    ref         may not access the project, but may be referenced\n"
           "\n");
    return 1;
}

int cmdUser(int argc, char **argv)
{
    const char *repo = ".";
    const char *username = 0;
    bool deletePasswd = false;
    std::string superadmin;
    enum UserConfigAction { GET_CONFIG, SET_CONFIG };
    UserConfigAction action = GET_CONFIG;
    User newUser;

    int c;
    int optionIndex = 0;
    struct option longOptions[] = {
        {"no-passwd", 0, 0, 0},
        {"passwd", 1, 0, 0},
        {"superadmin", 0, 0, 0},
        {"no-superadmin", 0, 0, 0},
        {"project", 1, 0, 0},
        {NULL, 0, NULL, 0}
    };
    while ((c = getopt_long(argc, argv, "vd:", longOptions, &optionIndex)) != -1) {
        switch (c) {
        case 0: // manage long options
            if (0 == strcmp(longOptions[optionIndex].name, "no-passwd")) {
                deletePasswd = true;
                action = SET_CONFIG;
            } else if (0 == strcmp(longOptions[optionIndex].name, "passwd")) {
                newUser.setPasswd(optarg);
                action = SET_CONFIG;
            } else if (0 == strcmp(longOptions[optionIndex].name, "superadmin")) {
                superadmin = "yes";
                action = SET_CONFIG;
            } else if (0 == strcmp(longOptions[optionIndex].name, "no-superadmin")) {
                superadmin = "no";
                action = SET_CONFIG;
            } else if (0 == strcmp(longOptions[optionIndex].name, "project")) {
                std::string arg = optarg;
                size_t i = arg.find_last_of(":");
                if (i == std::string::npos) {
                    printf("Malformed option --project <project>:<role>\n\n");
                    return helpUser();
                }
                std::string project = arg.substr(0, i);
                std::string role = arg.substr(i+1);
                newUser.permissions[project] = stringToRole(role);
                action = SET_CONFIG;
            }
            break;
        case 'v':
            setLoggingLevel(LL_DIAG);
            break;
        case 'd':
            repo = optarg;
            break;
        case '?': // incorrect syntax, a message is printed by getopt_long
            return helpUser();
            break;
        default:
            printf("?? getopt returned character code 0x%x ??\n", c);
            return helpUser();
        }
    }

    // manage non-option ARGV elements
    if (optind < argc) {
        username = argv[optind];
        optind++;
    }
    if (optind < argc) {
        printf("Too many arguments.\n\n");
        return helpUser();
    }

    int r = UserBase::init(repo);
    if (r < 0) {
        LOG_ERROR("Cannot loads users of repository '%s'. Aborting.", repo);
        exit(1);
    }

    if (!username) {
        // list all users
        if (action == SET_CONFIG) return helpUser();

        std::list<User> users = UserBase::getAllUsers();
        std::list<User>::const_iterator u;
        FOREACH(u, users) showUser(*u);
        printf("%ld user(s)\n", L(users.size()));

    } else {
        User *existingUser = UserBase::getUser(username);
        if (action == GET_CONFIG) {
            // list a specific user configuration
            if (!existingUser) {
                printf("No such user: %s\n", username);
                return 1;
            } else return showUser(*existingUser);
        }

        // create or update user

        newUser.username = username;
        if (superadmin == "no") newUser.superadmin = false;
        else if (superadmin == "yes") newUser.superadmin = true;

        if (existingUser) {
            // update the existing user
            if (!superadmin.empty()) existingUser->superadmin = newUser.superadmin;
            if (deletePasswd) {
                if (existingUser->authHandler) delete existingUser->authHandler;
                existingUser->authHandler = 0;

            } else if (newUser.authHandler) {
                // keep existing auth scheme
                existingUser->authHandler = newUser.authHandler;
            } else {
                // keep authentication and password unchanged
            }

            // update permissions (keep the others unchanged)
            std::map<std::string, Role>::iterator newPermission;
            FOREACH(newPermission, newUser.permissions) {
                if (newPermission->second == ROLE_NONE) {
                    // erase if same wildcard in existingUser
                    std::map<std::string, Role>::iterator existingPerm;
                    existingPerm = existingUser->permissions.find(newPermission->first);
                    if (existingPerm == existingUser->permissions.end()) {
                        // no such existing permission
                        // simlpy add the new wildcard and associated role
                        existingUser->permissions[newPermission->first] = newPermission->second;
                    } else {
                        // remove existing wildcard
                        existingUser->permissions.erase(newPermission->first);
                    }
                } else {
                    // add permission
                    existingUser->permissions[newPermission->first] = newPermission->second;
                }
            }
            // store the user (the existing user that has been updated)
            r = UserBase::updateUser(username, *existingUser, "local");

        } else {
            r = UserBase::addUser(newUser, "local");
        }
        if (r < 0) {
            LOG_ERROR("Abort.");
            return 1;
        }
    }

    return 0;
}

static int setupGitHookDenyCurrentBranch(const char *dir)
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


static int setupGitServerSideHooks(const char *repo)
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
        Argv argv;
        // TODO chmod on windows
        argv.set("chmod", "u+rx", updateHookPath.c_str(), 0);
        std::string subStdout, subStderr;
        err = Subprocess::launchSync(argv.getv(), 0, ".", 0, 0, subStdout, subStderr);
        if (err) {
            LOG_ERROR("Cannot chmod %s: %s", updateHookPath.c_str(), subStderr.c_str());
            return -1;
        }
        p = Database::Db.getNextProject(p);
    }
    return 0;
}

int helpServe()
{
    printf("Usage: smit serve [<repository>] [options]\n"
           "\n"
           "  Start a smit server.\n"
           "\n"
           "  <repository>      select a repository to serve (by default . is used)\n"
           "\n"
           "Options:\n"
           "  --listen-port <port>   set TCP listening port (default is 8090)\n"
           "  --ssl-cert <certificate>\n"
           "                         set HTTPS.\n"
           "                         <certificate> must be a PEM certificate,\n"
           "                         including public and private key.\n"
           "  --url-rewrite-root\n"
           "                         set URL-rewriting root, for usage behind a reverse proxy.\n"
           );
    return 1;
}

int cmdServe(int argc, char **argv)
{
    LOG_INFO("Starting Smit v" VERSION);

    std::string listenPort = "8090";
    const char *repo = 0;
    const char *certificatePemFile = 0;
    const char *urlRewritingRoot = 0;

    int c;
    int optionIndex = 0;
    struct option longOptions[] = {
        {"listen-port", 1, 0, 0},
        {"ssl-cert", 1, 0, 0},
        {"url-rewrite-root", 1, 0, 0},
        {NULL, 0, NULL, 0}
    };
    optind = 1; // reset this in case cmdUi has already parsed with getopt_long
    int loglevel = LL_INFO;
    while ((c = getopt_long(argc, argv, "d", longOptions, &optionIndex)) != -1) {
        switch (c) {
        case 0: // manage long options
            if (0 == strcmp(longOptions[optionIndex].name, "listen-port")) listenPort = optarg;
            else if (0 == strcmp(longOptions[optionIndex].name, "ssl-cert")) certificatePemFile = optarg;
            else if (0 == strcmp(longOptions[optionIndex].name, "url-rewrite-root")) urlRewritingRoot = optarg;
            break;
        case 'd':
            loglevel++;
            break;
        case '?': // incorrect syntax, a message is printed by getopt_long
            return helpServe();
            break;
        default:
            printf("?? getopt returned character code 0x%x ??\n", c);
            return helpServe();
        }
    }
    setLoggingLevel((LogLevel)loglevel);

    // manage non-option ARGV elements
    if (optind < argc) {
        repo = argv[optind];
        optind++;
    }
    if (optind < argc) {
        printf("Too many arguments.\n\n");
        return helpServe();
    }

    if (!repo) repo = ".";

    std::string dotLock = repo;
    dotLock += "/.lock";

    int lockFd = lockFile(dotLock);
    if (lockFd < 0) {
        LOG_ERROR("Cannot lock repository: %s", dotLock.c_str());
        exit(1);
    }

    std::string mongooseListeningPort = listenPort;
    if (certificatePemFile) mongooseListeningPort += 's'; // force HTTPS listening

    // Load all projects
    int r = dbLoad(repo);
    if (r < 0) {
        LOG_ERROR("Cannot serve repository '%s'. Aborting.", repo);
        exit(1);
    }
    r = UserBase::init(repo);
    if (r < 0) {
        LOG_ERROR("Cannot loads users of repository '%s'. Aborting.", repo);
        exit(1);
    }

    r = setupGitServerSideHooks(repo);
    if (r < 0) {
        LOG_ERROR("Cannot setup server side hooks. Aborting.");
        exit(1);
    }

    initHttpStats();

    MongooseServerContext *mc = new MongooseServerContext();
    mc->setRequestHandler(begin_request_handler);
    mc->setListeningPort(listenPort);

    if (urlRewritingRoot) mc->setUrlRewritingRoot(urlRewritingRoot);
    std::string serverMsg = "Starting http server on port " + mongooseListeningPort;
    if (urlRewritingRoot) serverMsg += std::string(" --url-rewrite-root ") + urlRewritingRoot;
    LOG_INFO("%s", serverMsg.c_str());

    mc->addParam("listening_ports");
    mc->addParam(mongooseListeningPort.c_str());
    mc->addParam("document_root");
    mc->addParam(repo);
    mc->addParam("enable_directory_listing");
    mc->addParam("no");

    if (certificatePemFile) {
        mc->addParam("ssl_certificate");
        mc->addParam(certificatePemFile);
    }

    if (UserBase::isLocalUserInterface()) {
        mc->addParam("num_threads");
        mc->addParam("1");
    }

    r = mc->start();

    if (r < 0) {
        LOG_ERROR("Cannot start http server. Aborting.");
        return -1;
    }

    if (!UserBase::isLocalUserInterface()) {
        while (1) sleep(1); // block until ctrl-C
    }
    // else, we return, and the cmdUi() will launch the web browser

    // TODO psosible memory leak as reference to 'mc' is lost
    return 0;
}
#ifndef _WIN32
void daemonize()
{
    int i;
    if (getppid() == 1) return; // already a daemon
    i = fork();
    if (i < 0) exit(1); // fork error
    if (i > 0) exit(0); // parent exits
    /* child (daemon) continues */
    setsid(); /* obtain a new process group */
    for (i = getdtablesize();i >= 0; i--) close(i); // close all descriptors
    i = open("/dev/null", O_RDWR);
    dup(i); dup(i); /* handle standart I/O */
    //int lfp=open(LOCK_FILE,O_RDWR|O_CREAT,0640);
    //if (lfp<0) exit(1); /* can not open */
    //if (lockf(lfp,F_TLOCK,0)<0) exit(0); /* can not lock */
    /* first instance continues */
    //char str[10];
    //sprintf(str,"%d\n",getpid());
    //write(lfp,str,strlen(str)); /* record pid to lockfile */
}
#endif

int helpUi()
{
    printf("Usage: smit ui [<repository>] [options]\n"
           "\n"
           "  Start a smit local server (bound to 127.0.0.1), and a web browser.\n"
           "\n"
           "  <repository>      select a repository to serve (by default . is used)\n"
           "\n"
           "Options:\n"
           "  --listen-port <port>   set TCP listening port (default is 8090)\n"
           );
    return 1;

}

int cmdUi(int argc, char **argv)
{
    std::string listenPort = "8199";
    char *repo = 0;
    setLoggingOption(LO_CLI);

    int c;
    int optionIndex = 0;
    struct option longOptions[] = {
        {"listen-port", 1, 0, 0},
        {NULL, 0, NULL, 0}
    };

    while ((c = getopt_long(argc, argv, "d", longOptions, &optionIndex)) != -1) {
        switch (c) {
        case 0: // manage long options
            if (0 == strcmp(longOptions[optionIndex].name, "listen-port")) listenPort = optarg;
            break;
        case 'd':
            setLoggingLevel(LL_DEBUG);
            break;
        case '?': // incorrect syntax, a message is printed by getopt_long
            return helpUi();
            break;
        default:
            printf("?? getopt returned character code 0x%x ??\n", c);
            return helpUi();
        }
    }

    // manage non-option ARGV elements
    if (optind < argc) {
        repo = argv[optind];
        optind++;
    }
    if (optind < argc) {
        printf("Too many arguments.\n\n");
        return helpUi();
    }

    if (!repo) repo = (char*)".";


    std::string username = loadUsername(repo);
    UserBase::setLocalInterfaceUser(username);

    listenPort = "127.0.0.1:" + listenPort;

    // start the local server
    char *serverArguments[4] = { 0, 0, 0, 0 };
    serverArguments[0] = (char*)"ui";
    serverArguments[1] = (char*)"--listen-port";
    serverArguments[2] = (char*)listenPort.c_str();
    serverArguments[3] = repo;

    // start a web browser
    std::string url = "http://" + listenPort + "/";
    std::string cmd;
    int r = 0;
#ifndef _WIN32 // linux
    // try xdg-open
    r = system("xdg-open --version");
    if (r == 0) cmd = "xdg-open"; // use xdg-open
    else {
        // try gnome-open
        int r = system("gnome-open --version");
        if (r == 0) cmd = "gnome-open"; // use gnome-open
        else {
            // try firefox
            int r = system("firefox --version");
            if (r == 0) cmd = "firefox"; // use firefox
            else cmd = "chromium"; // use chromium
        }
    }
    int pipefd[2]; // used to synchronize web client with local server
    r = pipe(pipefd);
    if (r < 0) {
        fprintf(stderr, "Cannot pipe: %s\n", strerror(errno));
        exit(1);
    }
    pid_t p = fork();
    if (p < 0) {
        fprintf(stderr, "Cannot fork: %s\n", strerror(errno));
        exit(1);
    }

    if (p) {
        // in parent, start local server
        close(pipefd[0]);
        int r = cmdServe(4, serverArguments);
        if (r < 0) {
            fprintf(stderr, "Cannot start local server\n");
            exit(1);
        }
        // indicate that the serve is ready
        ssize_t n = write(pipefd[1], "R", 1);
        if (n != 1) {
            fprintf(stderr, "Cannot write to pipe: %s\n", strerror(errno));
            exit(1);
        }
        close(pipefd[1]);

    } else {
        // in child
        close(pipefd[1]);
        // wait for local server to be ready
        // in case the local server has to load a huge local smit repository,
        // (tens of thousands of entries) then 20 seconds may not be enough. TODO.
        int i = 40; // wait 20 seconds max
        while (i > 0) {
            char buf[1];
            ssize_t n = read(pipefd[0], buf, 1);
            if (n == 1) break; // got it
            i--;
            if (i <= 0) {
                fprintf(stderr, "Child failed to get the ready indication\n");
                exit(1);
            }
            usleep(500*1000); // sleep 0.5 s
        }

        //printf("Running: %s %s\n", cmd.c_str(), url.c_str());
        daemonize();
        int r = execlp(cmd.c_str(), cmd.c_str(), url.c_str(), (char *)NULL);
        printf("execl: r=%d, %s\n", r, strerror(errno));
    }
    printf("Hit Ctrl-C to stop the local server\n");

    while (1) sleep(1); // block until ctrl-C

#else // _WIN32
    // start local server
    r = serveRepository(3, serverArguments);

    cmd = "start \"\" \"" + url + "\"";
    LOG_INFO("Running: %s...", cmd.c_str());
    system(cmd.c_str());

    printf("Enter 'exit' (or 'e') to stop the local server\n");

    while (1) {
        std::string userInput;
        std::cin >> userInput;
        if (userInput[0] == 'e' || userInput[0] == 'E') break;
        sleep(1); // block until ctrl-C
    }

#endif


    return r;
}

int showVersion()
{
    printf("Small Issue Tracker v%s\n"
           "Copyright (C) 2014 Frederic Hoerni\n"
           "\n"
           "This program is free software; you can redistribute it and/or modify\n"
           "it under the terms of the GNU General Public License v2 as published by\n"
           "the Free Software Foundation.\n"
           "\n"
           "This program is distributed in the hope that it will be useful,\n"
           "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
           "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
           "GNU General Public License for more details.\n"
           , VERSION);
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc < 2) usage();

    int i = 1;
    const char *command = 0;
    while (i < argc) {

        command = argv[1]; i++;

        if (0 == strcmp(command, "init")) {
            return initRepository(argc-1, argv+1);

        } else if (0 == strcmp(command, "version")) {
            return showVersion();

        } else if (0 == strcmp(command, "serve")) {
            cmdServe(argc-1, argv+1);
            return 1;

        } else if (0 == strcmp(command, "project")) {
            return cmdProject(argc-1, argv+1);

        } else if (0 == strcmp(command, "user")) {
            return cmdUser(argc-1, argv+1);

        } else if (0 == strcmp(command, "issue")) {
            return cmdIssue(argc-1, argv+1);
#ifdef CURL_ENABLED
        } else if (0 == strcmp(command, "clone")) {
            return cmdClone(argc-1, argv+1);

        } else if (0 == strcmp(command, "pull")) {
            return cmdPull(argc-1, argv+1);

        } else if (0 == strcmp(command, "push")) {
            return cmdPush(argc-1, argv+1);

        } else if (0 == strcmp(command, "get")) {
            return cmdGet(argc-1, argv+1);
#endif
        } else if (0 == strcmp(command, "ui")) {
            return cmdUi(argc-1, argv+1);

        } else if (0 == strcmp(command, "help")) {
            if (i < argc) {
                const char *help = argv[i];
                if      (0 == strcmp(help, "init")) return helpInit();
                else if (0 == strcmp(help, "issue")) return helpIssue();
                else if (0 == strcmp(help, "project")) return helpProject();
                else if (0 == strcmp(help, "user")) return helpUser();
                else if (0 == strcmp(help, "serve")) return helpServe();
                else if (0 == strcmp(help, "ui")) return helpUi();
#ifdef CURL_ENABLED
                else if (0 == strcmp(help, "clone")) return helpClone(0);
                else if (0 == strcmp(help, "pull")) return helpPull(0);
                else if (0 == strcmp(help, "push")) return helpPush(0);
                else if (0 == strcmp(help, "get")) return helpGet(0);
#endif
                else {
                    printf("No help for '%s'\n", help);
                    exit(1);
                }
            } else usage();
        } else usage();

    }

    return 0;
}
