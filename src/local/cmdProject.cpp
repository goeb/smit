#include <stdio.h>
#include <list>
#include <string>

#include "cmdProject.h"
#include "Args.h"
#include "utils/logging.h"
#include "utils/filesystem.h"
#include "repository/db.h"
#include "global.h"


Args *setupOptions()
{
    Args *args = new Args();
    args->setDescription("Manage a smit project locally.\n"
                         "\n"
                         "Args:\n"
                         "  <project-path>  The directory of the project.\n"
                         "                  (default: current directory)\n"
                         "\n"
                         "  <action>\n"
                         "          addProperty <name>\n"
                         "          numberIssues global|local\n"
                         "\n"
                         "Example:\n"
                         "  smit project -c example addProperty \"priority select high low\"\n"
                         "  smit project example addProperty \"friendly_name text\"\n"
                         );
    args->setUsage("smit project [options] [<project-path> [<action> ...]]");
    args->setOpt("verbose", 'v', "be verbose", 0);
    args->setOpt("all", 'a', "list all projects under the given path", 0);
    args->setOpt("create", 'c', "create a project", 0);
    args->setOpt(0, 'l', "print the project configuration", 0);

    return args;
}

int helpProject()
{
    Args *args = setupOptions();
    if (!args) {
        LOG_ERROR("Cannot setupOptions");
    }

    args->usage(true);
    return 1;
}

int cmdProject(int argc, char **argv)
{
    Args *args = setupOptions();

    bool create = false;
    bool printall = false;
    bool listconfig = false;

    args->parse(argc-1, argv+1);
    if (args->get("create")) create = true;
    if (args->get("l")) listconfig = true;
    if (args->get("all")) printall = true;

    // manage non-option ARGV elements
    const char *path = args->pop();
    if (!path) path = ".";

    setLoggingOption(LO_CLI);

    std::list<std::pair<std::string, std::string> > updateActions;
    const char* verb;
    while ( (verb = args->pop()) ) {
        // actions go by pair
        const char *arg = args->pop();
        if (!arg) {
            LOG_ERROR("missing argument for '%s'", arg);
            exit(1);
        }

        if ( 0 == strcmp(verb, "addProperty") ||
             0 == strcmp(verb, "numberIssues") ) {
            // ok

        } else {
            LOG_ERROR("Invalid action '%s' (allowed values: addProperty, numberIssues)", verb);
            exit(1);
        }

        updateActions.push_back(std::make_pair(verb, arg));
    }

    // set log level to hide INFO logs
    if (args->get("verbose")) {
        setLoggingLevel(LL_DIAG);
    } else {
        setLoggingLevel(LL_ERROR);
    }

    if (printall) {
        Database::loadProjects(path, true);

    } else {
        if (create) {
            if (fileExists(path)) {
                LOG_ERROR("Cannot create project: existing file or directory '%s'", path);
                exit(1);
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
                LOG_ERROR("Cannot update project '%s': no project found", path);
                exit(1);
            }

            ProjectConfig config = p->getConfig();

            std::list<std::pair<std::string, std::string> >::iterator action;
            FOREACH(action, updateActions) {
                if (action->first == "addProperty") {
                    std::list<std::string> tokens = split(action->second, " \t");
                    int r = config.addProperty(tokens);
                    if (r != 0) {
                        LOG_ERROR("Cannot addProperty: check the syntax\n");
                        exit(1);
                    }

                } else if (action->first == "numberIssues") {
                    if (action->second == "global") config.numberIssueAcrossProjects = true;
                    else config.numberIssueAcrossProjects = false;
                }
            }

            int r = p->modifyConfig(config, "local");
            if (r != 0) {
                LOG_ERROR("Cannot modify project config");
                exit(1);
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

    exit(0);
}
