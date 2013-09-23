#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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
  */
int cpioExtract(FILE* f, long end)
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

        if (0 == strcmp(filepath, "TRAILER!!!")) return 0;

        // now begins the contents of the size

        // created intermediate directories if needed
        // TODO

        // TODO if it is a directory, do not proceed below with file contents

        // create the file
        mode_t mode = O_CREAT | O_TRUNC | O_WRONLY;
        int flags = S_IRUSR | S_IWUSR;
        int extractedFile = open(filepath, mode, flags);
        if (-1 == extractedFile) {
            LOG_ERROR("cpioExtract: Could not create file '%s', (%d) %s", filepath, errno, strerror(errno));
            return -1;
        }

        // read the data and dump it to the file
        uint32_t dataToRead = (header.c_filesize[0] << 8) + header.c_filesize[1];
        while (dataToRead > 0) {
            const int SIZ = 2048;
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

int cpioExtractFile(const char *file)
{
    LOG_INFO("Extracting data from %s...", file);
    FILE *f = fopen(file, "rb");
    if (!f) {
        LOG_ERROR("Cannot open file '%s': %s", file, strerror(errno));
        return -1;
    }

    int r = fseek(f, -4, SEEK_END); // go to the end
    if (r != 0) {
        LOG_ERROR("Cannot fseek -4 file '%s': %s", file, strerror(errno));
        return -1;
    }

    uint32_t size;
    size_t n = fread(&size, sizeof(size), 1, f);
    size = ntohl(size); // convert to host byte order
    LOG_DEBUG("cpioExtractFile: size=%u\n", size);

    // go to the beginning of the cpio archive
    r = fseek(f, -4-size, SEEK_END);
    if (r != 0) {
        LOG_ERROR("Cannot fseek %d file '%s': %s", -4-size, file, strerror(errno));
        return -1;
    }

    r = cpioExtract(f, size);
    fclose(f);
    return r;
}
