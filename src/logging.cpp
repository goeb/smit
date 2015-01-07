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

LogLevel LoggingLevel = LL_INFO;
int LoggingOptions = LO_NO_OPTION;

bool doPrint(enum LogLevel msgLevel)
{
        return (msgLevel <= LoggingLevel);
}

