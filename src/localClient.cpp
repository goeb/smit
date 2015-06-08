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
#include "clone.h"

bool Verbose_localClient = false;

#define LOGV(...) do { if (Verbose_localClient) { printf(__VA_ARGS__); fflush(stdout);} } while (0)

// print mode mask
#define PRINT_SUMMARY      0x0000
#define PRINT_MESSAGES     0x0001
#define PRINT_PROPERTIES   0x0002
#define PRINT_FULL_HISTORY 0x0004

#define INDENT "    "

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
        if (p->first == K_SUMMARY) continue;
        printf(INDENT "%-25s : %s\n", p->first.c_str(), toString(p->second).c_str());
    }
}

void printMessages(const Issue &i, int printMode)
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

            // TODO print names of attached files

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
            }
        }
        e = e->getNext();
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

void printAllIssues(const Project &p)
{
    const std::map<std::string, std::list<std::string> > filterIn;
    const std::map<std::string, std::list<std::string> > filterOut;

    std::vector<Issue> issueList;
    p.search("", filterIn, filterOut, "id", issueList);
    std::vector<Issue>::const_iterator i;
    FOREACH(i, issueList) {
        printIssue(*i, PRINT_SUMMARY);
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
           "  -v, --verbose"
           "      Be verbose.\n"
           "\n"
           );
    return 1;
}

int cmdIssue(int argc, char * const *argv)
{
    const char *projectPath = 0;
    std::string issueId = "";
    int printMode = PRINT_SUMMARY;
    bool add = false;

    int c;
    int optionIndex = 0;
    struct option longOptions[] = {
        {"add",         no_argument,    0,  'a' },
        {"history",     no_argument,    0,  'h' },
        {"properties",  no_argument,    0,  'p' },
        {"messages",    no_argument,    0,  'm' },
        {"verbose",    no_argument,    0,   'v' },
        {NULL, 0, NULL, 0}
    };
    while ((c = getopt_long(argc, argv, "ahpmv", longOptions, &optionIndex)) != -1) {
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
    if (optind < argc && !add) {
        printf("Too many arguments.\n\n");
        return helpIssue();
    }

    if (!projectPath) {
        printf("You must specify the path of a project.\n\n");
        return helpIssue();
    }


    setLoggingOption(LO_CLI);
    if (Verbose_localClient) setLoggingLevel(LL_INFO);
    else setLoggingLevel(LL_ERROR);

    // load the project
    Project *p = Project::init(projectPath, "");
    if (!p) {
        fprintf(stderr, "Cannot load project '%s'\n", projectPath);
        exit(1);
    }

    if (!add && issueId.empty()) {
        printAllIssues(*p);

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
        if (issueId == "-") issueId = "";
        int r = p->addEntry(properties, issueId, entry, username);
        if (r == 0) {
            if (entry) {
                printf("%s/%s\n", entry->issue->id.c_str(), entry->id.c_str());
            } else {
                // no entry created, because no change
                printf("%s/-\n", issueId.c_str());
            }
        } else {
            printf("Error: cannot add entry\n");
            return 1;
        }

    } else {
        // get the issue
        Issue issue;
        int r = p->get(issueId, issue);
        if (r != 0) {
            fprintf(stderr, "Cannot get issue '%s'\n", issueId.c_str());
            exit(1);
        }

        printIssue(issue, printMode);

    }
    return 0;
}
