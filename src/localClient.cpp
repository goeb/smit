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
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <getopt.h>

#include "localClient.h"
#include "global.h"
#include "stringTools.h"
#include "mg_win32.h"
#include "console.h"
#include "filesystem.h"
#include "db.h"
#include "logging.h"

bool Verbose_localClient = false;

#define LOGV(...) do { if (Verbose_localClient) { printf(__VA_ARGS__); fflush(stdout);} } while (0)

#define SMIT_DIR ".smit"
#define PATH_SESSID SMIT_DIR "/sessid"
#define PATH_URL SMIT_DIR "/remote"


// print mode mask
#define PRINT_SUMMARY      0x0000
#define PRINT_MESSAGES     0x0001
#define PRINT_PROPERTIES   0x0002
#define PRINT_FULL_HISTORY 0x0004

#define INDENT "  "


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

void printProperties(const Issue &i)
{
    PropertiesIt p;
    printHeader("Properties");
    FOREACH(p, i.properties) {
        printf(INDENT "%-25s : %s\n", p->first.c_str(), toString(p->second).c_str());
    }
}

void printMessages(const Issue &i, int printMode)
{
    const Entry *e = i.latest;
    while (e && e->prev) e = e->prev;
    while (e) {
        bool doPrint = false; // used to know ifn the header must be printed
        std::string msg = e->getMessage();
        if (msg.size() || (printMode & PRINT_FULL_HISTORY) ) doPrint = true;

        if (doPrint) {

            // print the header
            std::string header = "Date: " + epochToString(e->ctime) + ", Author: " + e->author + " (" + e->id +")";
            printHeader(header.c_str());

            // TODO print tags ?

            if (msg.size()) {
                // print the message
                printf("%s\n", msg.c_str());
            }

            // TODO print names of attached files

            if (printMode & PRINT_FULL_HISTORY) {
                printHeader("Modified Properties");
                // print the properties changes
                // process summary first as it is not part of orderedFields
                PropertiesIt p;
                FOREACH(p, e->properties) {
                    if (p->first == K_MESSAGE) continue;
                    std::string value = toString(p->second);
                    printf(INDENT "%s: %s\n", p->first.c_str(), value.c_str()); // TODO print label instead of logical name
                }
            }
        }
        e = e->next;
    }
}

void printIssue(const Issue &i, int printMode)
{
    printf("Issue %s: %s\n", i.id.c_str(), i.getSummary().c_str());

    if (printMode & PRINT_PROPERTIES) {
        printProperties(i);
    }

    if ( (printMode & PRINT_MESSAGES) || (printMode & PRINT_FULL_HISTORY) ) {
        printMessages(i, printMode);
    }
}


int helpIssue()
{
    printf("Usage: smit issue [options] <path-to-project> [<id>]\n"
           "\n"
           "  Print an issue.\n"
           "\n"
           "Options:\n"
           "  -m, --messages\n"
           "      Print messages.\n"
           "\n"
           "  -p, --properties\n"
           "      Print properties.\n"
           "\n"
           "  -h, --history\n"
           "      Print all history (including properties changes).\n"
           "      (implies -m)"
           "\n"
           "  -a, --add"
           "      Add an entry.\n"
           "      If <id> is specified, then the entry shall be added to this issue.\n"
           "      Otherwise, a new issue shall be created.\n"
           "\n"
           );
    return 1;
}

int cmdIssue(int argc, char * const *argv)
{
    const char *projectPath = 0;
    const char *issueId = 0;
    int printMode = PRINT_SUMMARY;
    bool add = false;

    int c;
    int optionIndex = 0;
    struct option longOptions[] = {
        {"add",         no_argument,    0,  'a' },
        {"history",     no_argument,    0,  'h' },
        {"properties",  no_argument,    0,  'p' },
        {"messages",    no_argument,    0,  'm' },
        {NULL, 0, NULL, 0}
    };
    while ((c = getopt_long(argc, argv, "ahpm", longOptions, &optionIndex)) != -1) {
        switch (c) {
        case 'v':
            Verbose_localClient = true;
            break;
        case 'p':
            printMode |= PRINT_PROPERTIES;
            break;
        case 'm':
            printMode |= PRINT_MESSAGES;
            break;
        case 'h':
            printMode |= PRINT_FULL_HISTORY;
            break;
        case 'a':
            add = true;
            break;
        case '?': // incorrect syntax, a message is printed by getopt_long
            return helpIssue();
            break;
        default:
            printf("?? getopt returned character code 0x%x ??\n", c);
            return helpIssue();
        }
    }
    // manage non-option ARGV elements
    if (optind < argc) {
        projectPath = argv[optind];
        optind++;
    }
    if (optind < argc) {
        issueId = argv[optind];
        optind++;
    }
    if (optind < argc) {
        printf("Too many arguments.\n\n");
        return helpIssue();
    }

    if (!projectPath) {
        printf("You must specify the path of a project.\n\n");
        return helpIssue();
    }

    if (!add && !issueId) {
        printf("You must specify an issue identifier.\n\n");
        return helpIssue();
    }

    setLoggingOption(LO_CLI);
    setLoggingLevel(LL_ERROR);


    // load the project
    Project *p = Project::init(projectPath);
    if (!p) {
        fprintf(stderr, "Cannot load project '%s'", projectPath);
        exit(1);
    }

    if (!add) {
        // get the issue
        Issue issue;
        int r = p->get(issueId, issue);
        if (r != 0) {
            fprintf(stderr, "Cannot get issue '%s'\n", issueId);
            exit(1);
        }

        printIssue(issue, printMode);
    } else {
        printf("--add not implemented\n");
    }
    return 0;
}
