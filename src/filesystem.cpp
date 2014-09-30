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
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <sstream>

#include "filesystem.h"
#include "logging.h"
#include "global.h"

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


std::string getFileSize(std::string &path)
{
    struct stat fileStat;

    int r = stat(path.c_str(), &fileStat);
    if (r != 0) {
        LOG_ERROR("stat(%s) error: %s", path.c_str(), strerror(errno));
        return _("N/A");
    }
    off_t size = fileStat.st_size;
    std::stringstream result;
    result.setf( std::ios::fixed, std:: ios::floatfield);
    result.precision(1);
    if (size < 1024) {
        result << size << "o";
    } else if (size < 1024*1024) {
        double s = size/1024;
        result << s << "ko";
    } else {
        double s = size/1024/1024;
        result << s << "Mo";
    }
    return result.str();
}
