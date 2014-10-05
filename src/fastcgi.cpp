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
#include <errno.h>
#include <getopt.h>
#include <fcgi_config.h>
//#include <fcgi_stdio.h> redefines printf

#include "fastcgi.h"
#include "global.h"
#include "stringTools.h"
#include "mg_win32.h"
#include "console.h"

int helpFcgi()
{
    printf("Usage: smit fcgi <repository>\n"
           "\n"
           "  Run a Fast Cgi instance on a smit repository.\n"
           "\n"
           "  <directory>  An existing smit repository.\n"
           "\n"
           "\n"
           "Example of configuration for lighttpd:\n"
           "fastcgi.server = \"/\" => (( ... \n"
           "    \"bin-path\" => \"/path/to/smit fcgi /path/to/repo\",\n"
           "    \"check-local\" => \"disable\"\n"
           "))\n"
           "\n"
           );
    return 1;
}

int serveFcgi(const char *repo)
{
    return 0;
}



int cmdFcgi(int argc, char *const*argv)
{
    const char *repo = 0;

    int c;
    int optionIndex = 0;
    struct option longOptions[] = {
        {NULL, 0, NULL, 0}
    };
    while ((c = getopt_long(argc, argv, "", longOptions, &optionIndex)) != -1) {
        switch (c) {
        case 0: // manage long options
            // no long option so far
            break;
        case '?': // incorrect syntax, a message is printed by getopt_long
            return helpFcgi();
            break;
        default:
            printf("?? getopt returned character code 0x%x ??\n", c);
            return helpFcgi();
        }
    }
    // manage non-option ARGV elements
    if (optind < argc) {
        repo = argv[optind];
        optind++;
    }
    if (optind < argc) {
        printf("Too many arguments.\n\n");
        return helpFcgi();
    }

    if (!repo) {
        printf("You must specify a repository.\n\n");
        return helpFcgi();
    }

    return serveFcgi(repo);
}
