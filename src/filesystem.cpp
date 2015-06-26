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
#include <stdlib.h>
#include <dirent.h>
#include <string.h>

#include "filesystem.h"

#ifdef SM_NO_LOGGING

  #define LOG_DEBUG(...)
  #define LOG_INFO(...)
  #define LOG_ERROR(...)

#else
#include "logging.h"
#endif

#include "global.h"
#include "stringTools.h"

#if defined(_WIN32)

#include <windows.h>

#else

#define MAX_PATH 2048

#endif


int loadFile(const std::string &filepath, std::string &data)
{
    return loadFile(filepath.c_str(), data);
}

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
int writeToFile(const std::string &filepath, const std::string &data)
{
    return writeToFile(filepath.c_str(), data.data(), data.size());
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

    std::string tmp = getTmpPath(filepath);
    // TODO flags and mode inverted
    // TODO add O_CLOEXEC
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

bool fileExists(const std::string &path)
{
    struct stat fileStat;

    int r = stat(path.c_str(), &fileStat);
    return (r==0);
}

bool isDir(const std::string &path)
{
    struct stat fileStat;

    int r = stat(path.c_str(), &fileStat);
    if (r == 0 && S_ISDIR(fileStat.st_mode) ) return true;
    else return false;
}



#if defined(_WIN32)

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

int mkdir(const char *path, int mode) {
    wchar_t wbuf[PATH_MAX];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wbuf, ARRAY_SIZE(wbuf));

    BOOL r = CreateDirectoryW(wbuf, NULL);
    errno = GetLastError();
    return  r ? 0 : -1;
}
#endif

int mkdir(const std::string &path)
{
    return mkdir(path.c_str(), 0777);
}

/** Create the given directory (create all intermediate directories if needed)
  */
int mkdirs(const std::string &path)
{
    if (path.empty()) return -1;

    size_t offset = 0;
    size_t pos = 0;
    while (pos != std::string::npos) {
        pos = path.find_first_of("/", offset);
        std::string subpath;
        if (pos == std::string::npos) {
            subpath = path;

        } else {
            subpath = path.substr(0, pos);
            offset = pos+1;
        }

        if (!isDir(subpath)) {
            int r = mkdir(subpath.c_str(), 0777);
            if (r != 0) return -1;
        }
    }
    return 0;
}

std::string getFileSize(const std::string &path)
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


DIR *openDir(const std::string &path)
{
    return opendir(path.c_str());
}

std::string getNextFile(DIR *d)
{
    struct dirent *f;
    while ((f = readdir(d)) != NULL) {
        if (0 == strcmp(f->d_name, ".")) continue;
        if (0 == strcmp(f->d_name, "..")) continue;
        return f->d_name;
    }
    return "";
}
void closeDir(DIR *d)
{
    closedir(d);
}


/** Remove a directory and its contents
  *
  * Not fully recursive. Only the regular files of
  * the directory are removed before removing the directory.
  */
int removeDir(const std::string &path)
{
    DIR *d;
    if ((d = opendir(path.c_str())) == NULL) {
        if (errno == ENOENT) {
            // no such file or directory
            // not really an error
            LOG_DEBUG("Cannot opendir non-existing dir '%s' (%s)", path.c_str(), strerror(errno));
            return 0;
        } else {
            LOG_ERROR("Cannot opendir '%s': %s", path.c_str(), strerror(errno));
            return -1;
        }
    }

    struct dirent *f;
    while ((f = readdir(d)) != NULL) {
        if (0 == strcmp(f->d_name, ".")) continue;
        if (0 == strcmp(f->d_name, "..")) continue;
        std::string filepath = path + "/" + f->d_name;
        int r = unlink(filepath.c_str());
        if (r < 0) {
            if (errno == ENOENT) {
                // no such file or directory
                // not really an error
                LOG_DEBUG("Cannot unlink non-existing file '%s' (%s)", path.c_str(), strerror(errno));
            } else {
                LOG_ERROR("Cannot unlink '%s': %s", filepath.c_str(), strerror(errno));
                closedir(d);
                return -1;
            }
        }
    }
    closedir(d);

    // remove the directory, that should be empty now
    int r = rmdir(path.c_str());
    if (r < 0) {
        if (errno == ENOENT) {
            // no such file or directory
            // not really an error
            LOG_DEBUG("Cannot rmdir non-existing directory '%s' (%s)", path.c_str(), strerror(errno));
        } else {
            LOG_ERROR("Cannot rmdir '%s': %s", path.c_str(), strerror(errno));
            return -1;
        }
    }
    return 0;
}

int copyFile(const std::string &srcPath, const std::string &destPath)
{
    std::ifstream src(srcPath.c_str(), std::ios::binary);
    if (!src.is_open()) {
        LOG_ERROR("Cannot open source file '%s' for copying", srcPath.c_str());
        return -1;
    }

    std::ofstream dst(destPath.c_str(), std::ios::binary);
    if (!dst.is_open()) {
        LOG_ERROR("Cannot open destination file '%s' for copying", srcPath.c_str());
        return -1;
    }

    dst << src.rdbuf();

    src.close();
    dst.close();
    return 0;
}


std::string getTmpPath(const std::string &path)
{
    std::string dir = getDirname(path);
    std::string base = getBasename(path);
    return dir + "/." + base + ".tmp";
}

int cmpFiles(const std::string &srcPath, const std::string &destPath)
{
    LOG_INFO("cmpFiles not implemented");
    // TODO
    return 0;
}

/** Compare the contents of a file with some data
  *
  * @return
  *    0 equal
  *   -1 different
  */
int cmpContents(const char *contents, size_t size, const std::string &file)
{
    std::string data;
    int r = loadFile(file.c_str(), data);
    if (r != 0) return r;

    if (contents == data) return 0;
    else return -1;
}

int cmpContents(const std::string &contents, const std::string &file)
{
    return cmpContents(contents.data(), contents.size(), file);
}



