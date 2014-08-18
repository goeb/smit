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

#include "HttpContext.h"
#include "httpdHandlers.h"
#include "logging.h"
#include "db.h"
#include "cpio.h"
#include "session.h"
#include "global.h"
#include "identifiers.h"
#include "filesystem.h"
#include "clone.h"


void usage()
{
    printf("Usage: smit <command> [<args>]\n"
           "\n"
           "The smit commands are:\n"
           "\n"
           "  clone       Clone a smit repository\n"
           "  init        Initialise a smit repository\n"
           "  project     List, create, or update a smit project\n"
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
    return 0;
}

int helpProject()
{
    printf("Usage: project [<project-name>] [options]\n"
           "\n"
           "  List, create, or update a smit project.\n"
           "\n"
           "Options:\n"
           "  -c         Create a project, with a default structure. The structure\n"
           "             may be modified online by an admin user.\n"
           "  -d <repo>  select a repository by its path (by default . is used)\n"
          );
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
           "  --project <project-name> <role>\n"
           "                    set a role (ref, ro, rw, admin) on a project\n"
           "  --superadmin      set the superadmin priviledge (ability to create\n"
           "                    projects and manage users via the web interface)\n"
           "  --no-superadmin   remove the superadmin priviledge\n"
           "  -d <repo>         select a repository by its path (by default . is used)\n"
           "\n"
           "Roles:\n"
           "    admin       able to modify an existing project\n"
           "    rw          able to create and modify issues\n"
           "    ro          able to read issues\n"
           "    ref         may not access the project, but may be referenced\n"
           "\n");
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
           );
    return 0;
}

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
    return 0;

}

int initRepository(const char *directory)
{
    DIR *dirp;
    dirp = opendir(directory);
    if (!dirp) {
        // try create it
        mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
        int r = mg_mkdir(directory, mode);
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

    // ok, extract the files for the new repository
    int r = cpioExtractFile("public", directory);
    if (r < 0) {
        LOG_ERROR("Error while extracting 'public/*': r=%d", r);
        return 3;
    }

    r = UserBase::initUsersFile(directory);
    if (r < 0) {
        LOG_ERROR("Abort.");
        return 1;
    }
    LOG_INFO("Done.");
    return 0;
}

int cmdProject(int argc, char **args)
{
    int i = 0;
    const char *repo = ".";
    const char *projectName = 0;
    bool create = false;
    while (i<argc) {
        const char *arg = args[i]; i++;
        if (0 == strcmp(arg, "-d")) {
            if (i<argc) {
                repo = args[i];
                i++;
            } else usage();

        } else if (0 == strcmp(arg, "-c")) {
            create = true;

        } else if (!projectName) {
            projectName = arg;

        } else {
            usage();
        }
    }

    // set log level to hide INFO logs
    setLoggingLevel(LL_ERROR);

    // Load all projects
    int r = dbLoad(repo);
    if (r < 0) {
        LOG_ERROR("Cannot load projects of repository '%s'. Aborting.", repo);
        return 1;
    }

    if (!projectName) {
        if (create) {
            LOG_ERROR("Cannot create project with no name.\n");
            return 1;
        }
        // list projects
        std::map<std::string, Project*>::const_iterator p;
        FOREACH(p, Database::Db.projects) {
            printf("%s: %d issues\n", p->first.c_str(), p->second->getNumIssues());
        }
        printf("%lu project(s)\n", L(Database::Db.projects.size()));
        return 0;
    }

    if (create) {
        std::string resultingPath;
        r = Project::createProjectFiles(repo, projectName, resultingPath);
        if (r < 0) return 1;
        printf("Project created: %s\n", resultingPath.c_str());
    } else {
        // print project
        std::map<std::string, Project*>::const_iterator p;
        p = Database::Db.projects.find(projectName);
        if (p == Database::Db.projects.end()) {
            printf("No such project: %s", projectName);
            return 1;
        }
        printf("%s: %d issues\n", p->first.c_str(), p->second->getNumIssues());
    }
    return 0;
}


int showUser(const User &u)
{
    printf("%s", u.username.c_str());
    if (u.hashValue.empty()) printf(", no password");
    if (u.superadmin) printf(", superadmin");
    printf("\n");

    std::map<std::string, enum Role>::const_iterator project;
    FOREACH(project, u.rolesOnProjects) {
        printf("    %s: %s\n", project->first.c_str(), roleToString(project->second).c_str());
    }
    printf("\n");
    return 0;
}

int cmdUser(int argc, char **args)
{
    int i = 0;
    const char *repo = ".";
    const char *username = 0;
    bool deletePasswd = false;
    std::string superadmin;
    enum UserConfigAction { GET_CONFIG, SET_CONFIG };
    UserConfigAction action = GET_CONFIG;
    User u;

    while (i < argc) {
        const char *arg = args[i]; i++;
        if (0 == strcmp(arg, "-d")) {
            if (i<argc) {
                repo = args[i];
                i++;
            } else return helpUser();

        } else if (0 == strcmp(arg, "--no-passwd")) {
            deletePasswd = true;
            action = SET_CONFIG;

        } else if (0 == strcmp(arg, "--passwd")) {
            if (i<argc) {
                u.setPasswd(args[i]);
                i++;
                action = SET_CONFIG;
            } else return helpUser();

        } else if (0 == strcmp(arg, "--superadmin")) {
            superadmin = "yes";
            action = SET_CONFIG;

        } else if (0 == strcmp(arg, "--no-superadmin")) {
            superadmin = "no";
            action = SET_CONFIG;

        } else if (0 == strcmp(arg, "--project")) {
            if (i+1<argc) {
                std::string project = args[i];
                std::string role = args[i+1];
                u.rolesOnProjects[project] = stringToRole(role);
                // role "none" to be erase below
                i += 2;
                action = SET_CONFIG;
            } else return helpUser();

        } else if (!username) {
            username = arg;

        } else return helpUser();
    }

    int r = UserBase::init(repo, false);
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

    } else {
        if (action == GET_CONFIG) {
            const User *u = UserBase::getUser(username);
            if (!u) {
                printf("No such user: %s\n", username);
                return 1;
            } else return showUser(*u);
        }

        // create or update user
        u.username = username;
        if (superadmin == "no") u.superadmin = false;
        else if (superadmin == "yes") u.superadmin = true;
        User *old = UserBase::getUser(username);
        if (old) {
            if (!superadmin.empty()) old->superadmin = u.superadmin;
            if (deletePasswd) {
                old->hashType.clear();
                old->hashValue.clear();
            } else if (!u.hashType.empty()) {
                old->hashType = u.hashType;
                old->hashValue = u.hashValue;
            }
            // update modified roles (and keep the others unchanged)
            std::map<std::string, enum Role>::iterator newRole;
            FOREACH(newRole, u.rolesOnProjects) {
                if (newRole->second == ROLE_NONE) old->rolesOnProjects.erase(newRole->first);
                else old->rolesOnProjects[newRole->first] = newRole->second;
            }
            r = UserBase::updateUser(username, *old);

        } else {
            r = UserBase::addUser(u);
        }
        if (r < 0) {
            LOG_ERROR("Abort.");
            return 1;
        }
    }

    return 0;
}

int serveRepository(int argc, char **args)
{
    LOG_INFO("Starting Smit v" VERSION);

    int i = 0;
    std::string listenPort = "8090";
    //const char *lang = "en";
    const char *repo = 0;
    const char *certificatePemFile = 0;
    while (i<argc) {
        const char *arg = args[i]; i++;
        if (0 == strcmp(arg, "--listen-port")) {
            if (i<argc) {
                listenPort = args[i];
                i++;
            } else usage();
#if 0
        } else if (0 == strcmp(arg, "--lang")) {
            if (i<argc) {
                lang = args[i];
                i++;
            } else usage();
#endif
        } else if (0 == strcmp(arg, "-d")) {
            setLoggingLevel(LL_DEBUG);
        } else if (0 == strcmp(arg, "--ssl-cert")) {
            if (i<argc) {
                certificatePemFile = args[i];
                i++;
            } else usage();

        } else if (!repo) {
            repo = arg;
        } else {
            usage();
        }
    }

    if (!repo) repo = ".";
    if (certificatePemFile) listenPort += 's'; // force HTTPS listening

    // Load all projects
    int r = dbLoad(repo);
    if (r < 0) {
        LOG_ERROR("Cannot serve repository '%s'. Aborting.", repo);
        exit(1);
    }
    if (!UserBase::isLocalUserInterface()) {
        r = UserBase::init(repo);
        if (r < 0) {
            LOG_ERROR("Cannot loads users of repository '%s'. Aborting.", repo);
            exit(1);
        }
    }

    MongooseServerContext mc;
    mc.setRequestHandler(begin_request_handler);

    LOG_INFO("Starting http server on port %s", listenPort.c_str());

    mc.addParam("listening_ports");
    mc.addParam(listenPort.c_str());
    mc.addParam("document_root");
    mc.addParam(repo);

    if (certificatePemFile) {
        mc.addParam("ssl_certificate");
        mc.addParam(certificatePemFile);
    }

    if (UserBase::isLocalUserInterface()) {
        mc.addParam("num_threads");
        mc.addParam("1");
    }

    r = mc.start();

    if (r < 0) {
        LOG_ERROR("Cannot start http server. Aborting.");
        return -1;
    }

    if (!UserBase::isLocalUserInterface()) {
        while (1) sleep(1); // block until ctrl-C
        getchar();  // Wait until user hits "enter"
        mc.stop();
    }
    // else, we return, and the cmdUi() will launch the web browser
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
int cmdUi(int argc, char **args)
{
    int i = 0;
    std::string listenPort = "127.0.0.1:8090";
    //const char *lang = "en";
    const char *repo = 0;

    while (i<argc) {
        const char *arg = args[i]; i++;
        if (0 == strcmp(arg, "--listen-port")) {
            if (i<argc) {
                listenPort = "127.0.0.1:";
                listenPort += args[i];
                i++;
            } else usage();

        } else if (!repo) repo = arg;
        else usage();
    }
    if (!repo) repo = ".";

    UserBase::setLocalUserInterface();

    // start the local server

    const char *serverArguments[3];
    serverArguments[0] = "--listen-port";
    serverArguments[1] = listenPort.c_str();
    serverArguments[2] = repo;

    // start a web browser
    std::string url = "http://" + listenPort + "/";
    std::string cmd;
    int r = 0;
#ifndef _WIN32
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
        int r = serveRepository(3, (char**)serverArguments);
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
    int r = serveRepository(3, serverArguments);

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
            const char *dir = ".";
            if (i < argc) dir = argv[i];
            return initRepository(dir);

        } else if (0 == strcmp(command, "version")) {
            return showVersion();

        } else if (0 == strcmp(command, "serve")) {
            serveRepository(argc-2, argv+2);
            return 1;

        } else if (0 == strcmp(command, "project")) {
            return cmdProject(argc-2, argv+2);

        } else if (0 == strcmp(command, "user")) {
            return cmdUser(argc-2, argv+2);

        } else if (0 == strcmp(command, "clone")) {
            return cmdClone(argc-1, argv+1);

        } else if (0 == strcmp(command, "ui")) {
            return cmdUi(argc-2, argv+2);

        } else if (0 == strcmp(command, "help")) {
            if (i < argc) {
                const char *help = argv[i];
                if      (0 == strcmp(help, "init")) return helpInit();
                else if (0 == strcmp(help, "project")) return helpProject();
                else if (0 == strcmp(help, "user")) return helpUser();
                else if (0 == strcmp(help, "serve")) return helpServe();
                else if (0 == strcmp(help, "ui")) return helpUi();
                else if (0 == strcmp(help, "clone")) return helpClone();
                else {
                    printf("No help for '%s'\n", help);
                    exit(1);
                }
            } else usage();
        } else usage();

    }

    return 0;
}
