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

#include <unistd.h>

#include "filesystem.h"

#if defined(_WIN32)

#include <windows.h>

#else

#define MAX_PATH 2048

#endif

std::string getExePath()
{
    char buffer[MAX_PATH];

#if defined(_WIN32)
    DWORD n = GetModuleFileName(NULL, buffer, MAX_PATH);

#else // Linux
    ssize_t n = readlink( "/proc/self/exe", buffer, sizeof(buffer));
#endif
    if (n < 0) return "";

    std::string exePath(buffer, n);
    return exePath;
}

