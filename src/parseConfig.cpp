
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "parseConfig.h"
#include "ustring.h"
#include "logging.h"


std::list<std::list<ustring> > parseConfig(const uint8_t *buf, size_t len)
{
    std::list<std::list<ustring> > linesOftokens;
    size_t i = 0;
    enum State {
        P_READY,
        P_IN_DOUBLE_QUOTES,
        P_IN_BACKSLASH,
        P_IN_COMMENT,
        P_IN_BACKSLASH_IN_DOUBLE_QUOTES,
        P_IN_BOUNDARY_HEADER,
        P_IN_BOUNDARY
    };
    enum State state = P_READY;
    ustring token; // current token
    std::list<ustring> line; // current line
    ustring boundary;
    ustring boundedText;
    for (i=0; i<len; i++) {
        uint8_t c = buf[i];
        switch (state) {
        case P_IN_COMMENT:
            if (c == '\n') {
                if (line.size() > 0) { linesOftokens.push_back(line); line.clear(); }
                state = P_READY;
            }
            break;
        case P_IN_BACKSLASH:
            if (c == '\n') { // new line escaped
                // nothing particular here
            } else {
                token += c;
            }
            state = P_READY;
            break;

        case P_IN_DOUBLE_QUOTES:
            if (c == '\\') {
                state = P_IN_BACKSLASH_IN_DOUBLE_QUOTES;
            } else if (c == '"') {
                state = P_READY; // end of double-quoted string
            } else token += c;
            break;
        case P_IN_BACKSLASH_IN_DOUBLE_QUOTES:
            token += c;
            state = P_IN_DOUBLE_QUOTES;
            break;
        case P_IN_BOUNDARY_HEADER:
            if (c == '\n') {
                state = P_IN_BOUNDARY;
                boundary.insert(0, (uint8_t*)"\n"); // add a \n at the beginning
                boundedText.clear();
            } else if (isblank(c)) continue; // ignore blanks
            else {
                boundary += c;
            }
            break;
        case P_IN_BOUNDARY:
            // check if boundary was reached
            if (boundedText.size() >= boundary.size()) { // leading \n already added
                size_t bsize = boundary.size();
                size_t offset = boundedText.size() - bsize;
                if (0 == boundedText.compare(offset, bsize, boundary)) {
                    // boundary found
                    // substract the boundary fro the text
                    boundedText = boundedText.substr(0, offset);
                    state = P_READY;
                    line.push_back(boundedText);
                    boundedText.clear();
                    linesOftokens.push_back(line);
                    line.clear();
                }
            }
            // accumulate character if still in same state
            if (state == P_IN_BOUNDARY) boundedText += c;

            break;
        case P_READY:
        default:
            if (c == '\n') {
                if (token.size() > 0) { line.push_back(token); token.clear(); }
                if (line.size() > 0) { linesOftokens.push_back(line); line.clear(); }
            } else if (c == ' ' || c == '\t' || c == '\r') {
                // current toke is done (because c is a token delimiter)
                if (token.size() > 0) { line.push_back(token); token.clear(); }
            } else if (c == '#') {
                if (token.size() > 0) { line.push_back(token); token.clear(); }
                state = P_IN_COMMENT;
            } else if (c == '\\') {
                state = P_IN_BACKSLASH;
            } else if (c == '"') {
                state = P_IN_DOUBLE_QUOTES;
            } else if (c == '<') {
                state = P_IN_BOUNDARY_HEADER;
                boundary.clear();
            } else {
                token += c;
            }
            break;
        }
    }
    // purge remaininig token and line
    if (token.size() > 0) line.push_back(token);
    if (line.size() > 0) linesOftokens.push_back(line);

    return linesOftokens;
}

// Allocate a buffer (malloc) and load a file into this buffer.
// @return the number of bytes read (that is also the size of the buffer)
//         -1 in case of error
// If the file is empty, 0 is returned and the buffer is not allocated.
// It is up to the caller to free the buffer (if the return value is > 0).
int loadFile(const char *filepath, unsigned char **data)
{
    //LOG_DEBUG("Loading file '%s'...", filepath);
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
    unsigned char *buffer = (unsigned char *)malloc(filesize+1); // allow +1 for terminating null char
    size_t n = fread(buffer, 1, filesize, f);
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


// "aaa+bb+ccc"
// @return aaa, bbb, ccc
std::list<ustring> parseColspec(const char *colspec)
{
    std::list<ustring> result;
    size_t i = 0;
    size_t L = strlen(colspec);
    ustring currentToken;
    for (i=0; i<L; i++) {
        uint8_t c = colspec[i];
        if (c == '+') {
            // push previous token if any
            if (currentToken.size() > 0) result.push_back(currentToken);
            currentToken.clear();
        } else currentToken += c;
    }
    if (currentToken.size() > 0) result.push_back(currentToken);
    return result;
}

// "aaa+bbb-ccc"
// @return ('+', aaa), ('+', bbb), ('-', ccc)
std::list<std::pair<char, std::string> > parseFieldSpec(const char *fieldSpec)
{
    std::list<std::pair<char, std::string> > result;
    size_t i = 0;
    size_t L = strlen(fieldSpec);
    std::string currentToken;
    char sign = '+';
    for (i=0; i<L; i++) {
        char c = fieldSpec[i];
        if ( (c == '+') || (c == '+') ) {
            // push previous token if any
            if (currentToken.size() > 0) result.push_back(std::make_pair(sign, currentToken));

            sign = c;
            currentToken = "";
        } else currentToken += c;
    }
    if (currentToken.size() > 0) result.push_back(std::make_pair(sign, currentToken));
}

