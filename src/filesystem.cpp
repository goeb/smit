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

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <sstream>
#include <fstream>

#include "filesystem.h"
#include "logging.h"
#include "global.h"

#if defined(_WIN32)

#include <windows.h>

#else

#define MAX_PATH 2048

#endif



int loadFile(const char *filepath, std::string &data)
{
    std::ifstream f(filepath);
    if (!f.good()) return -1;
    std::stringstream buffer;
    buffer << f.rdbuf();
    data = buffer.str();
    return 0;
}

// Allocate a buffer (malloc) and load a file into this buffer.
// @return the number of bytes read (that is also the size of the buffer)
//         -1 in case of error
// If the file is empty, 0 is returned and the buffer is not allocated.
// It is up to the caller to free the buffer (if the return value is > 0).
int loadFile(const char *filepath, const char **data)
{
    //LOG_DEBUG("Loading file '%s'...", filepath);
    *data = 0;
    FILE *f = fopen(filepath, "rb");
    if (NULL == f) {
        LOG_DEBUG("Could not open file '%s', %s", filepath, strerror(errno));
        return -1;
    }
    // else continue and parse the file

    int r = fseek(f, 0, SEEK_END); // go to the end of the file
    if (r != 0) {
        LOG_ERROR("could not fseek(%s): %s", filepath, strerror(errno));
        fclose(f);
        return -1;
    }
    long filesize = ftell(f);
    if (filesize > 4*1024*1024) { // max 4 MByte
        LOG_ERROR("loadFile: file '%s'' over-sized (%ld bytes)", filepath, filesize);
        fclose(f);
        return -1;
    }

    if (0 == filesize) {
        // the file is empty
        fclose(f);
        return 0;
    }

    rewind(f);
    char *buffer = (char *)malloc(filesize+1); // allow +1 for terminating null char
    LOG_DEBUG("allocated buffer %p: %ld bytes", buffer, filesize+1);
    long n = fread(buffer, 1, filesize, f);
    if (n != filesize) {
        LOG_ERROR("fread(%s): short read. feof=%d, ferror=%d", filepath, feof(f), ferror(f));
        fclose(f);
        free(buffer);
        return -1;
    }
    buffer[filesize] = 0;
    *data = buffer;
    fclose(f);
    return n;
}

/** Write a string to a file
  *
  * @return
  *    0 if success
  *    <0 if error
  */
int writeToFile(const char *filepath, const std::string &data)
{
    return writeToFile(filepath, data.data(), data.size());
}

int writeToFile(const char *filepath, const char *data, size_t len)
{
    int result = 0;
    mode_t mode = O_CREAT | O_TRUNC | O_WRONLY;
    int flags = S_IRUSR;

#if defined(_WIN32)
    mode |= O_BINARY;
    flags |= S_IWUSR;
#else
    flags |= S_IRGRP | S_IROTH;
#endif

    std::string tmp = filepath;
    tmp += ".tmp";
    int f = open(tmp.c_str(), mode, flags);
    if (-1 == f) {
        LOG_ERROR("Could not create file '%s', (%d) %s", tmp.c_str(), errno, strerror(errno));
        return -1;
    }

    if (len > 0) {
        size_t n = write(f, data, len);
        if (n != len) {
            LOG_ERROR("Could not write all data, incomplete file '%s': (%d) %s",
                      filepath, errno, strerror(errno));
            return -1;
        }
    }

    close(f);

#if defined(_WIN32)
    _unlink(filepath);
#endif

    int r = rename(tmp.c_str(), filepath);
    if (r != 0) {
        LOG_ERROR("Cannot rename '%s' -> '%s': (%d) %s", tmp.c_str(), filepath, errno, strerror(errno));
        return -1;
    }

    return result;
}

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

bool fileExists(std::string &path)
{
    struct stat fileStat;

    int r = stat(path.c_str(), &fileStat);
    return (r==0);
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
        double s = (double)size/1024;
        result << s << "ko";
    } else {
        double s = (double)size/1024/1024;
        result << s << "Mo";
    }
    return result.str();
}
