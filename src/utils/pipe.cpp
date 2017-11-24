
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "pipe.h"

#define BUF_SIZ 4096

/** popen
 * @param mode "r" or "w"
 */
Pipe *Pipe::open(const std::string &command, const char *mode)
{
    FILE *fp = popen(command.c_str(), mode);
    if (!fp) return 0;

    Pipe *p = new Pipe;
    p->fp = fp;
    return p;
}

/** Close the pipe and destroy the Pipe object
 */
void Pipe::close(Pipe *&p)
{
    pclose(p->fp);
    delete p;
    p = 0;
}

/** Get the next line (including the LF if not at end of file)
 *
 *  Return an empty string if end of file is reached or an error occurred.
 */
std::string Pipe::getline()
{
    std::string result;

    while (1) {
        // look if a whole line is available in the buffer
        size_t pos = buffer.find_first_of('\n');
        if (pos != std::string::npos) {

            result = buffer.substr(0, pos+1);

            // pop the line from the buffer
            buffer = buffer.substr(pos+1);
            break;
        }

        // so far no whole line has been received

        // check end of file
        if (feof(fp) || ferror(fp)) {
            result = buffer;
            buffer = ""; // clear the buffer
            break;
        }

        // read from the pipe
        uint8_t localbuf[BUF_SIZ];
        size_t n = fread(localbuf, 1, BUF_SIZ, fp);

        if (ferror(fp)) {
            LOG_ERROR("Error in fread: %s", STRERROR(errno));
            // error is not raised immediately. First process
            // data previously received in the buffer
        }

        // concatenate in the buffer
        buffer.append((char*)localbuf, n);
    }

    return result;
}
