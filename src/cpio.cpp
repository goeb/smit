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

#include <errno.h>
#include <string.h>
#include <stdint.h>
//#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>

#include "cpio.h"
#include "logging.h"
#include "mg_win32.h"

#include "embedcpio.h" // generated

// http://people.freebsd.org/~kientzle/libarchive/man/cpio.5.txt
struct header_old_cpio {
    unsigned short   c_magic;
    unsigned short   c_dev;
    unsigned short   c_ino;
    unsigned short   c_mode;
    unsigned short   c_uid;
    unsigned short   c_gid;
    unsigned short   c_nlink;
    unsigned short   c_rdev;
    unsigned short   c_mtime[2];
    unsigned short   c_namesize;
    unsigned short   c_filesize[2];
};

enum FileType { FTYPE_REGULAR, FTYPE_DIR, FTYPE_OTHER };


int cpioGetNextFileHeader(const char *&offset, const char *end, std::string &filepath, FileType &ftype, uint32_t &filesize)
{
    LOG_FUNC();
    struct header_old_cpio header;
    size_t availableSize = end-offset;
    LOG_DEBUG("offset=%p, end=%p, availableSize=%zd", offset, end, availableSize);
    if (sizeof(header) > availableSize) return -2;
    memcpy(&header, (void*)offset, sizeof(header));
    offset += sizeof(header);

    if (header.c_magic == 0x71c7) { // 0o070707 == 0x71c7
        // ok
    } else {
        LOG_ERROR("cpioExtract: Unsupported archive / endianess");
        return -5;
    }

    // 0100000  File type value for regular files.
    // 0040000  File type value for directories.
    if ( (header.c_mode & 0100000) == 0100000) {
        LOG_DEBUG("cpioExtract: File is a regular file");
        ftype = FTYPE_REGULAR;

    } else if ( (header.c_mode & 0040000) == 0040000) {
        LOG_DEBUG("cpioExtract: File is a directory");
        ftype = FTYPE_DIR;
        if (header.c_filesize[0] != 0 ||
            header.c_filesize[1] != 0) {
            LOG_ERROR("cpioExtract: directory with non-null size. Abort.");
            return -6;
        }

    } else if (header.c_mode == 0) {
        // should be the TRAILER.

    } else {
        LOG_ERROR("cpioExtract: File type %hu not supported. Abort.", header.c_mode);
        return -7;
    }

    // get size of filename
    char filepathBuf[1024];
    if (header.c_namesize > 1022) {
        LOG_ERROR("cpioExtract: Filename too long (%hu)", header.c_namesize);
        return -8;
    }
    if (header.c_namesize % 2 == 1) header.c_namesize += 1; // make the size even

    availableSize = end-offset;
    if (header.c_namesize > availableSize)  {
        LOG_ERROR("cpioExtract: short read %u/%u", availableSize, header.c_namesize);
        return -9;
    }
    memcpy(filepathBuf, (void*)offset, header.c_namesize);
    offset += header.c_namesize;
    LOG_DEBUG("cpioExtract: filepath=%s", filepathBuf);
    if (0 == strcmp(filepathBuf, "TRAILER!!!")) return -10; // end of archive

    filesize = (header.c_filesize[0] << 8) + header.c_filesize[1];
    filepath = filepathBuf;
    return 0;
}


/** Get the pointer of a regular file in the cpio archive
  */
int cpioGetFile(const char *file, const char *&cpioOffset)
{
    LOG_FUNC();
    std::string filepath;
    FileType ftype;
    uint32_t filesize;
    const char *cpioEnd = em_binary_data_embedcpio + em_binary_size_embedcpio;
    LOG_DEBUG("em_binary_size_embedcpio=%d", em_binary_size_embedcpio);
    cpioOffset = em_binary_data_embedcpio;
    int r;
    while ( (r = cpioGetNextFileHeader(cpioOffset, cpioEnd, filepath, ftype, filesize)) >= 0 ) {

        if (filepath != file) {
            // this is not the file we are looking for
            // skip the contents of this file
            if (filesize % 2 == 1) filesize++; // padding of 1 null byte
            cpioOffset += filesize;
            continue;
        }

        if (ftype != FTYPE_REGULAR) {
            LOG_DEBUG("File '%s' is a directory. Return -1.", file);
            return -1;
        }
        return filesize;
    }
    // error of file not found (r<0)
    LOG_DEBUG("File %s not found in archive. r=%d", file, r);
    return r;
}


/** Extract a cpio archive starting at the given address
  * @param cpioStart, cpioSize
  *     cpio archive in memory
  *
  * @param src
  *     The source file or directory to extract
  *
  * @param dst
  *     The destination directory where the archive shall be extracted.
  */
int cpioExtractTree(const char *cpioStart, size_t cpioSize, const char *src, const char *dst)
{
    std::string filepath;
    FileType ftype;
    uint32_t filesize;
    const char *cpioEnd = cpioStart + cpioSize;
    const char *cpioOffset = cpioStart;
    int r;
    while ( (r = cpioGetNextFileHeader(cpioOffset, cpioEnd, filepath, ftype, filesize)) >= 0 ) {
        // if file does not match src, then skip this file
        if (src && strlen(src) && 0 != strncmp(src, filepath.c_str(), strlen(src))) {
            if (filesize % 2 == 1) filesize++; // padding of 1 null byte
            cpioOffset += filesize;
            if (cpioOffset > cpioEnd) {
                LOG_ERROR("past the cpio end (file=%s contentsSize=%u)", filepath.c_str(), filesize);
                return -1;
            }
            LOG_DEBUG("Skip: %s", filepath.c_str());

            continue;
        }

        // now begins the contents of the file

        // create intermediate directories if needed
        std::string incrementalDir = dst;
        std::string destinationFile = filepath;
        while (destinationFile.size() > 0) {
            size_t i = destinationFile.find_first_of('/');
            if ( (i == std::string::npos) && (ftype != FTYPE_DIR) ) break; // done if regular file
            // else, this is the basename of a directory. Proceed...

            std::string subdir = destinationFile.substr(0, i);

            if (i == std::string::npos) destinationFile = ""; // stop at next iteration
            else destinationFile = destinationFile.substr(i+1);

            LOG_DEBUG("cpioExtract: subdir=%s, destinationFile=%s", subdir.c_str(), destinationFile.c_str());

            if (subdir.size() == 0) continue; // case of 2 consecutives slashes in path

            incrementalDir += "/";
            incrementalDir += subdir;

            mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR;
            LOG_DEBUG("cpioExtract: mkdir %s...", incrementalDir.c_str());
            int r = mg_mkdir(incrementalDir.c_str(), mode);
            if (r == 0) continue; // ok
#if defined(_WIN32)
            else if (errno == ERROR_ALREADY_EXISTS) continue; // maybe ok
#else
            else if (errno == EEXIST) continue; // maybe ok
#endif
            else {
                LOG_ERROR("cpioExtract: Could not mkdir '%s': %s (errno=%d, r=%d)",
                          incrementalDir.c_str(), strerror(errno), errno, r);
                return -1;
            }
        }

        if (ftype == FTYPE_DIR) {
            // we are done with this file.
            // it is a directory, do not proceed below with file contents
            // continue to next file in the archive
            LOG_DEBUG("Directory done: %s", filepath.c_str());
            continue;
        }
        LOG_DEBUG("Creating file: %s", filepath.c_str());

        // create the file
        // destinationFile contains the basename of the file
        mode_t mode = O_CREAT | O_TRUNC | O_WRONLY;
#if defined(_WIN32)
        mode |= O_BINARY;
#endif
        int flags = S_IRUSR | S_IWUSR;
        destinationFile = incrementalDir + "/" + destinationFile;
        LOG_DEBUG("cpioExtract: creating file %s...", destinationFile.c_str());
        int extractedFile = open(destinationFile.c_str(), mode, flags);
        if (-1 == extractedFile) {
            LOG_ERROR("cpioExtract: Could not create file '%s', (%d) %s", filepath.c_str(), errno, strerror(errno));
            return -1;
        }

        // read the data and dump it to the file
        uint32_t dataToRead = filesize;
        LOG_DEBUG("cpioExtract: dataToRead=%u", dataToRead);
        while (dataToRead > 0) {
            const size_t SIZ = 2048;
            char buffer[SIZ];
            size_t nToRead;
            if (dataToRead > SIZ) nToRead = SIZ;
            else nToRead = dataToRead;

            if (cpioOffset + nToRead > cpioEnd) {
                LOG_ERROR("cpioExtract: Error while reading contents (eof)");
                return -1;
            }
            memcpy(buffer, cpioOffset, nToRead);
            cpioOffset += nToRead;
            dataToRead -= nToRead;
            // write to file
            ssize_t written = write(extractedFile, buffer, nToRead);

            if (written == -1) {
                LOG_ERROR("cpioExtract: Cannot write to file '%s': %s", destinationFile.c_str(), strerror((errno)));
                close(extractedFile);
                return -1;

            } else if ((size_t)written != nToRead) {
                LOG_ERROR("cpioExtract: short write to file '%s': n=%u, written=%d",
                          destinationFile.c_str(), nToRead, written);
                close(extractedFile);
                return -1;
            }
        }
        if (filesize % 2 == 1) {
            // consuming the padding of 1 null byte
            cpioOffset += 1;
        }
        close(extractedFile);
    }
    if (r == -10) r = 0; // end of archive is ok
    return r;
}




/** Extract a cpio archive located in memory
  */
int cpioExtractFile(const char *src, const char *dst)
{
    int r = cpioExtractTree(em_binary_data_embedcpio, em_binary_size_embedcpio, src, dst);
    LOG_DEBUG("cpioExtract: result=%d", r);
    return r;

}

