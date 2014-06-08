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

#include <string.h>

#include "logging.h"

bool doPrint(enum LogLevel msgLevel)
{
    const char *envLevel = getenv("SMIT_DEBUG");
    enum LogLevel policy = LL_INFO; // default value
    if (envLevel) {
        if (0 == strcmp(envLevel, "FATAL")) policy = LL_FATAL;
        else if (0 == strcmp(envLevel, "ERROR")) policy = LL_ERROR;
        else if (0 == strcmp(envLevel, "INFO")) policy = LL_INFO;
        else if (0 == strcmp(envLevel, "DEBUG")) policy = LL_DEBUG;
        else policy = LL_INFO;
    }
    return (policy >= msgLevel);
}

