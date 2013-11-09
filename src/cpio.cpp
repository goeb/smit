#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>

#include "cpio.h"
#include "logging.h"


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

/** Extract a cpio archive starting at the given file pointer
  * @param basedir
  *     The destination directory where the archive shall be extracted.
  */
int cpioExtract(FILE* f, long end, const char *basedir)
{
    struct header_old_cpio header;
    while (1) {
        size_t n = fread(&header, sizeof(header), 1, f);
        if (n == 0) {
            if (feof(f)) return 0; // ok, normal termination.
            else {
                // error
                LOG_ERROR("Error while reading header");
                return -1;
            }
        }

        long offset = ftell(f);
        if (offset < 0) {
            LOG_ERROR("cpioExtract: Cannot ftell file: %s", strerror(errno));
            return -1;
        }
        if (header.c_magic == 0x71c7) { // 0o070707 == 0x71c7
            // ok
        } else {
            LOG_ERROR("cpioExtract: Unsupported archive / endianess");
            return -1;
        }

        bool isDir = false;
        // 0100000  File type value for regular files.
        // 0040000  File type value for directories.
        if ( (header.c_mode & 0100000) == 0100000) {
            LOG_DEBUG("cpioExtract: File is a regular file");

        } else if ( (header.c_mode & 0040000) == 0040000) {
            LOG_DEBUG("cpioExtract: File is a directory");
            isDir = true;
            if (header.c_filesize[0] != 0 ||
                header.c_filesize[1] != 0) {
                LOG_ERROR("cpioExtract: directory with non-null size. Abort.");
                return -1;
            }

        } else if (header.c_mode == 0) {
            // should be the TRAILER.

        } else {
            LOG_ERROR("cpioExtract: File type %hu not supported. Abort.", header.c_mode);
            return -1;
        }

        // get size of filename
        char filepath[1024];
        if (header.c_namesize > 1022) {
            LOG_ERROR("cpioExtract: Filename too long (%d)", header.c_namesize);
            return -1;
        }
        if (header.c_namesize % 2 == 1) header.c_namesize += 1; // make the size even
        n = fread(filepath, 1, header.c_namesize, f); // terminated null char included
        if (n != header.c_namesize) {
            LOG_ERROR("cpioExtract: short read %d/%d", n, header.c_namesize);
            return -1;
        }
        LOG_DEBUG("cpioExtract: filepath=%s", filepath);
        if (0 == strcmp(filepath, "TRAILER!!!")) return 0; // end of archive

        // now begins the contents of the size

        // create intermediate directories if needed
        std::string incrementalDir = basedir;
        std::string destinationFile = filepath;
        while (destinationFile.size() > 0) {
            size_t i = destinationFile.find_first_of('/');
            if ( (i == std::string::npos) && (!isDir) ) break; // done if regular file
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
            int r = mkdir(incrementalDir.c_str(), mode);
            if (r == 0) continue; // ok
            else if (errno == EEXIST) continue; // maybe ok
            else {
                LOG_ERROR("cpioExtract: Could not mkdir '%s': %s (%d)", incrementalDir.c_str(), strerror(errno), errno);
                return -1;
            }
        }

        if (isDir) {
            // we are done with this file.
            // it is a directory, do not proceed below with file contents
            // continue to next file in the archive
            continue;
        }

        // create the file
        // destinationFile contains the basename of the file
        mode_t mode = O_CREAT | O_TRUNC | O_WRONLY;
        int flags = S_IRUSR | S_IWUSR;
        destinationFile = incrementalDir + "/" + destinationFile;
        LOG_DEBUG("cpioExtract: creating file %s...", destinationFile.c_str());
        int extractedFile = open(destinationFile.c_str(), mode, flags);
        if (-1 == extractedFile) {
            LOG_ERROR("cpioExtract: Could not create file '%s', (%d) %s", filepath, errno, strerror(errno));
            return -1;
        }

        // read the data and dump it to the file
        uint32_t dataToRead = (header.c_filesize[0] << 8) + header.c_filesize[1];
        uint32_t contentsSize = dataToRead;
        LOG_DEBUG("cpioExtract: dataToRead=%u", dataToRead);
        while (dataToRead > 0) {
            const size_t SIZ = 2048;
            char buffer[SIZ];
            size_t nToRead;
            if (dataToRead > SIZ) nToRead = SIZ;
            else nToRead = dataToRead;

            n = fread(buffer, 1, nToRead, f); // terminated null char included
            if (n != nToRead) {
                if (feof(f)) {
                    close(extractedFile);
                    LOG_ERROR("cpioExtract: Error while reading contents (eof)");
                    return -1;
                } else {
                    // error
                    LOG_ERROR("cpioExtract: Error while reading contents");
                    close(extractedFile);
                    return -1;
                }
            }
            dataToRead -= n;
            // write to file
            size_t written = write(extractedFile, buffer, n);
            if (written != n) {
                if (written == -1) {
                    LOG_ERROR("cpioExtract: Cannot write to file '%s': %s", destinationFile.c_str(), strerror((errno)));

                } else {
                    LOG_ERROR("cpioExtract: short write to file '%s': n=%d, written=%d", destinationFile.c_str(), n, written);

                }
                close(extractedFile);
                return -1;
            }
        }
        if (contentsSize % 2 == 1) {
            // consuming the padding of 1 null byte
            char buffer[1];
            size_t n = fread(buffer, 1, 1, f);
            if (n != 1)  {
                LOG_ERROR("cpioExtract: cannot get the padding 1: n=%d, %s", n, strerror(errno));
                close(extractedFile);
                return -1;
            }
        }
        close(extractedFile);
    }
}

/** Extract a cpio archive located at the end of the given file.
  *
  * The memory is organized as follows:
  * [... exe ... | ... cpio archive ... | length-of-cpio-archive ]
  * length-of-cpio-archive is on 4 bytes. Thus the beginning of the cpio archive
  * is found starting from the end.
  */

int cpioExtractFile(const char *file, const char *basedir)
{
    LOG_INFO("Extracting archive from %s...", file);
    FILE *f = fopen(file, "rb");
    if (!f) {
        LOG_ERROR("Cannot open file '%s': %s", file, strerror(errno));
        return -1;
    }

    int r = fseek(f, -4, SEEK_END); // go to the end - 4
    if (r != 0) {
        LOG_ERROR("Cannot fseek -4 file '%s': %s", file, strerror(errno));
        return -1;
    }

    uint32_t size;
    size_t n = fread(&size, sizeof(size), 1, f);
    //size = ntohl(size); // convert to host byte order
    LOG_DEBUG("cpioExtractFile: size=%u\n", size);

    // go to the beginning of the cpio archive
    r = fseek(f, -4-size, SEEK_END);
    if (r != 0) {
        LOG_ERROR("Cannot fseek %d file '%s': %s", -4-size, file, strerror(errno));
        return -1;
    }

    r = cpioExtract(f, size, basedir);
    LOG_DEBUG("cpioExtract: result=%d", r);
    fclose(f);
    return r;
}
