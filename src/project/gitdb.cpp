#include <string>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

//#include "gitdb.h"
//#include "utils/logging.h"

#define LOG_ERROR(...)
#define LOG_DEBUG(format,...) printf("debug: " format, __VA_ARGS__);


#define BUF_SIZ 4096
#define BRANCH_PREFIX_ISSUES "issues/"

struct PopenIterator {
    FILE *fp;
    std::string buffer; // data read but not yet consumed
};

PopenIterator* gitdbIssuesOpen(const std::string &path)
{
    FILE *fp;
    std::string cmd = "git -C \"" + path + "\" branch --list \"" BRANCH_PREFIX_ISSUES "*\"";
    fp = popen(cmd.c_str(), "r");
    if (!fp) return 0;

    PopenIterator *result = new PopenIterator;
    result->fp = fp;
    return result;
}

static std::string pipeReadLine(PopenIterator *iterator)
    {
    std::string result;

    while (1) {
        // look if a whole line is available in the buffer
        size_t pos = iterator->buffer.find_first_of('\n');
        if (pos != std::string::npos) {

            result = iterator->buffer.substr(0, pos);

            // pop the line from the buffer
            iterator->buffer = iterator->buffer.substr(pos+1);
            break;
        }

        // so far no whole line has been received

        // check end of file
        if (feof(iterator->fp) || ferror(iterator->fp)) {
            result = iterator->buffer;
            iterator->buffer = ""; // clear the buffer
            break;
        }

        // read from the pipe
        uint8_t buffer[BUF_SIZ];
        size_t n = fread(buffer, 1, BUF_SIZ, iterator->fp);

        if (ferror(iterator->fp)) {
            LOG_ERROR("Error in fread: %s", STRERROR(errno));
            // error is not raised immediately. First process
            // data previously received in the buffer
        }

        // concatenate in the buffer
        iterator->buffer.append((char*)buffer, n);
    }

    return result;
}

std::string gitdbIssuesNext(PopenIterator *iterator)
{
    std::string line = pipeReadLine(iterator);

    // trim the "  issues/" prefix
    if (line.size() > strlen(BRANCH_PREFIX_ISSUES)+2) {
        line = line.substr(strlen(BRANCH_PREFIX_ISSUES)+2);
    }
    return line;
}

void gitdbIssuesClose(PopenIterator *iterator)
{
    pclose(iterator->fp);

}


int main()
{
    PopenIterator *iterator = gitdbIssuesOpen(".");

    while (1) {
        std::string issueId = gitdbIssuesNext(iterator);
        if (issueId.empty()) break;
        LOG_DEBUG("issueId=%s\n", issueId.c_str());
    }
    gitdbIssuesClose(iterator);
}
