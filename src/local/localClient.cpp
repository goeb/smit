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

#include <iostream>
#include <string>
#include <list>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <getopt.h>

#include "localClient.h"
#include "global.h"
#include "utils/stringTools.h"
#include "utils/filesystem.h"
#include "utils/logging.h"
#include "mg_win32.h"
#include "console.h"
#include "repository/db.h"
#include "clone.h"

bool Verbose_localClient = false;

#define LOG_CLI_ERR(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)

// print mode mask
#define PRINT_SUMMARY      0x0000
#define PRINT_MESSAGES     0x0001
#define PRINT_PROPERTIES   0x0002
#define PRINT_FULL_HISTORY 0x0004

#define INDENT "    "

void storeUsername(const std::string &dir, const std::string &username)
{
    LOG_DEBUG("storeUsername(%s, %s)...", dir.c_str(), username.c_str());
    std::string path = dir + "/" PATH_USERNAME;
    int r = writeToFile(path, username + "\n");
    if (r < 0) {
        LOG_CLI_ERR("Abort.");
        exit(1);
    }
}
std::string loadUsername(const std::string &clonedRepo)
{
    std::string path = clonedRepo + "/" PATH_USERNAME;
    std::string username;
    int r = loadFile(path.c_str(), username);
    if (r < 0) {
        username = "Anonymous";
        LOG_CLI_ERR("Cannot load username. Set '%s'", username.c_str());
    }
    trim(username); // remove trailing \n
    return username;
}

void printSeparation()
{
    printf("----------------------------------------"); // 40 hyphens
    printf("----------------------------------------\n"); // 40 hyphens
}

/** Print a header, and complete with hypens
  */
void printHeader(const char *header)
{
    printf("---- %s ", header);
    int i = 80 - strlen(header) - 6;
    while (i>0) {
        printf("-");
        i--;
    }
    printf("\n");
}

void printProperties(const IssueCopy &i)
{
    PropertiesIt p;
    printHeader("Properties");
    FOREACH(p, i.properties) {
        if (p->first == K_SUMMARY) continue;
        printf(INDENT "%-25s : %s\n", p->first.c_str(), toString(p->second).c_str());
    }
}

void printMessages(const IssueCopy &i, int printMode)
{
    const Entry *e = i.first;
    while (e) {
        bool doPrint = false; // used to know ifn the header must be printed
        std::string msg = e->getMessage();

        if (e->isAmending()) doPrint = false;
        else if (msg.size() || (printMode & PRINT_FULL_HISTORY) ) doPrint = true;

        if (doPrint) {

            printSeparation();
            // print the header
            std::string header = "Date: " + epochToString(e->ctime) + ", Author: " + e->author + " (" + e->id +")";
            printf("%s\n", header.c_str());

            // TODO print tags ?

            if (msg.size()) {
                // print the message
                printf("Message:\n");
                size_t size = msg.size();
                size_t i;
                for (i = 0; i < size; i++) {
                    // indent lines of message
                    if (msg[i] == '\r') continue; // skip this
                    if (i==0) printf(INDENT); // indent first line of message
                    printf("%c", msg[i]);
                    if (msg[i] == '\n') printf(INDENT); // indent each new line
                }
                printf("\n");
            }

            bool noModifiedPropertiesSoFar = true;
            if (printMode & PRINT_FULL_HISTORY) {
                // print the properties changes
                // process summary first as it is not part of orderedFields
                PropertiesIt p;
                FOREACH(p, e->properties) {
                    if (p->first == K_MESSAGE) continue;
                    if (noModifiedPropertiesSoFar) {
                        // do print this header only if there is at least one modified property
                        printf("Modified Properties:\n");
                        noModifiedPropertiesSoFar = false;
                    }
                    std::string value = toString(p->second);
                    printf(INDENT "%s: %s\n", p->first.c_str(), value.c_str()); // TODO print label instead of logical name
                }

                // print names of attached files
                std::list<AttachedFileRef>::const_iterator file;
                FOREACH(file, e->files) {
                    if (file == e->files.begin()) {
                        // print a header
                        printf("Attached files:\n");
                    }
                    printf(INDENT "%s %s\n", file->id.c_str(), file->filename.c_str());
                }

            }
        }
        e = e->getNext();
    }
}

void printIssue(const IssueCopy &i, int printMode)
{
    printf("Issue %s: %s\n", i.id.c_str(), i.getSummary().c_str());

    if (printMode & PRINT_PROPERTIES) {
        printProperties(i);
    }

    if ( (printMode & PRINT_MESSAGES) || (printMode & PRINT_FULL_HISTORY) ) {
        printMessages(i, printMode);
    }
}

void printAllIssues(const Project &p)
{
    const std::map<std::string, std::list<std::string> > filterIn;
    const std::map<std::string, std::list<std::string> > filterOut;

    std::vector<IssueCopy> issueList;
    p.search("", filterIn, filterOut, "id", issueList);
    std::vector<IssueCopy>::const_iterator i;
    FOREACH(i, issueList) {
        printIssue(*i, PRINT_SUMMARY);
    }
}


Args *setupIssueOptions()
{
    Args *args = new Args();
    args->setUsage("smit issue [options] <path-to-project> [<id>]");

    args->setDescription(
                "Prints an issue.\n"
                "\n"
                "Examples:\n"
                "\n"
                "    smit issue /path/to/project -a - \"summary=some annoying bug\"\n"
                "\n"
                "        creates a new issue and sets its summary.\n"
                "\n"
                "\n"
                "    smit issue /path/to/project -a 138 \"status=open\"\n"
                "\n"
                "        sets the status at \"open\" for issue #138.\n"
                "\n"
                "\n"
                "    smit issue /path/to/project\n"
                "\n"
                "        prints the list of issues and their summaries\n"
                );
    args->setOpt("add", 'a',
                 "add an entry.\n"
                 "Argument <id> is mandatory. If a character '-' is used, then\n"
                 "a new issue shall be created. Otherwise, the entry shall be added\n"
                 "to this issue."
                 , 0);
    args->setOpt("file", 'f', "specify a file to attach. Must be used with '-a'.", 1);
    args->setOpt("history", 0,
                 "print all history (including properties changes).\n"
                 "(implies -m)"
                 , 0);
    args->setOpt("messages", 'm', "print messages", 0);
    args->setOpt("properties", 'p', "print properties", 0);
    args->setOpt("verbose", 'v', "be verbose", 0);
    return args;
}

int helpIssue(const Args *args)
{
    if (!args) args = setupIssueOptions();
    args->usage(true);
    return 1;
}


int cmdIssue(int argc, char **argv)
{
    std::string issueId = "";
    int printMode = PRINT_SUMMARY;
    bool add = false;
    const char *attachedFile;

    Args *args = setupIssueOptions();
    args->parse(argc-1, argv+1);
    if (args->get("verbose")) Verbose_localClient = true;
    if (args->get("properties")) printMode |= PRINT_PROPERTIES;
    if (args->get("messages")) printMode |= PRINT_MESSAGES;
    if (args->get("history")) printMode |= PRINT_FULL_HISTORY;
    if (args->get("add")) add = true;
    attachedFile = args->get("file");

    // manage non-option ARGV elements
    // manage non-option ARGV elements
    const char *projectPath = args->pop();

    if (!projectPath) {
        LOG_CLI_ERR("You must specify the path of a project.");
        exit(1);
    }

    const char *pIssueId = args->pop();
    if (pIssueId) issueId = pIssueId;

    setLoggingOption(LO_CLI);
    if (Verbose_localClient) setLoggingLevel(LL_INFO);
    else setLoggingLevel(LL_ERROR);

    // load the project
    Project *p = Project::init(projectPath, "");
    if (!p) {
        LOG_CLI_ERR("Cannot load project '%s'", projectPath);
        exit(1);
    }

    if (!add && !pIssueId) {
        printAllIssues(*p);

    } else if (add && !pIssueId) {
        LOG_CLI_ERR("Missing issue");
        exit(1);

    } else if (add) {
        // add a new issue, or an entry to an existing issue
        // parse the remaining argument
        // they must be of the form key=value
        PropertiesMap properties;
        while (optind < argc) {
            std::string arg = argv[optind];
            std::string key = popToken(arg, '=');
            std::string value = arg;
            trimBlanks(key);
            trimBlanks(value);
            properties[key].push_back(arg);
            optind++;
        }
        // get the username of the repository
        std::string repo = projectPath;
        repo += "/..";
        std::string username = loadUsername(repo);

        Entry *entry = 0;
        IssueCopy oldIssue;
        if (issueId == "-") issueId = "";

        std::list<AttachedFileRef> files;

        if (attachedFile) {
            std::string data;
            int err = loadFile(attachedFile, data);
            if (err) {
                LOG_CLI_ERR("Cannot load file %s.", attachedFile);
                exit(1);
            }
            std::string fileId = p->storeFile(data.data(), data.size());
            if (fileId.empty()) {
                // error
                LOG_CLI_ERR("Cannot store file %s.", attachedFile);
                exit(1);
            } else {
                AttachedFileRef file;
                file.size = data.size();
                file.filename = getBasename(attachedFile);
                file.id = fileId;
                files.push_back(file);
            }
        }
        int r = p->addEntry(properties, files, issueId, entry, username, oldIssue);
        if (r >= 0) {
            if (entry) {
                printf("%s/%s\n", entry->issue->id.c_str(), entry->id.c_str());
            } else {
                // no entry created, because no change
                printf("(no change)\n");
            }
        } else {
            printf("Error: cannot add entry\n");
            delete p;
            return 1;
        }

    } else {
        // get the issue
        IssueCopy issue;
        int r = p->get(issueId, issue);
        if (r != 0) {
            LOG_CLI_ERR("Cannot get issue '%s'", issueId.c_str());
            exit(1);
        }

        printIssue(issue, printMode);

    }
    delete p;
    return 0;
}
