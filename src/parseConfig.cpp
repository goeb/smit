
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "parseConfig.h"
#include "logging.h"
#include "global.h"

/** Parse a text buffer and return a list of lines of tokens
  *
  * The syntax is:
  * text := line *
  * line := token * [ '\\' '\n' line ] *
  * token := simple-token | double-quote-token | boundary-token
  * simple-token := [^ \t\n\r"] *
  * double-quote-token := '"' [^"] * '"'
  * boundary-token := '<' boundary '\n' any-text '\n' boundary
  */
std::list<std::list<std::string> > parseConfigTokens(const char *buf, size_t len)
{
    std::list<std::list<std::string> > linesOftokens;
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
    std::string token; // current token
    std::list<std::string> line; // current line
    std::string boundary;
    std::string boundedText;
    for (i=0; i<len; i++) {
        char c = buf[i];
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
                boundary.insert(0, "\n"); // add a \n at the beginning
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
int loadFile(const char *filepath, char **data)
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

/** Write a string to a file
  *
  * @return
  *    0 if success
  *    <0 if error
  */
int writeToFile(const char *filepath, const std::string &data)
{
    int result = 0;
    mode_t mode = O_CREAT | O_TRUNC | O_WRONLY;
    int flags = S_IRUSR;

    std::string tmp = filepath;
    tmp += ".tmp";
    int f = open(tmp.c_str(), mode, flags);
    if (-1 == f) {
        LOG_ERROR("Could not create file '%s', (%d) %s", tmp.c_str(), errno, strerror(errno));
        return -1;
    }

    size_t n = write(f, data.c_str(), data.size());
    if (n != data.size()) {
        LOG_ERROR("Could not write all data, incomplete file '%s': (%d) %s",
                  filepath, errno, strerror(errno));
        return -1;
    }

    close(f);

    int r = rename(tmp.c_str(), filepath);
    if (r != 0) {
        LOG_ERROR("Cannot rename '%s' -> '%s': (%d) %s", tmp.c_str(), filepath, errno, strerror(errno));
        return -1;
    }

    return result;
}


// "aaa bb ccc"
// @return aaa, bbb, ccc
std::list<std::string> parseColspec(const char *colspec)
{
    std::list<std::string> result;
    size_t i = 0;
    size_t L = strlen(colspec);
    std::string currentToken;
    for (i=0; i<L; i++) {
        char c = colspec[i];
        if (c == ' ' || c == '+') {
            // push previous token if any
            if (currentToken.size() > 0) result.push_back(currentToken);
            currentToken.clear();
        } else currentToken += c;
    }
    if (currentToken.size() > 0) result.push_back(currentToken);
    return result;
}

/** Double quotes are needed whenever the value contains one of: \n \t \r "
  */
std::string doubleQuote(const std::string &input)
{
    size_t n = input.size();
    std::string result = "\"";

    size_t i;
    for (i=0; i<n; i++) {
        if (input[i] == '\\') result += "\\\\";
        else if (input[i] == '"') result += "\\\"";
        else result += input[i];
    }
    result += '"';
    return result;
}

std::string serializeSimpleToken(const std::string token)
{
    if (token.empty()) return "\"\"";
    else if (token.find_first_of("#\n \t\r\"") == std::string::npos) return token;
    else return doubleQuote(token); // some characters needs escaping
}


std::string serializeProperty(const std::string &propertyName, const std::list<std::string> &values)
{
    std::ostringstream s;
    s << propertyName; // preamble that indicates the name of the property

    if ( (values.size() == 1) && (values.front().find('\n') != std::string::npos) ) {
        // serialize as multi-line
        const char *delimiter = "-----------endofmsg---"; // TODO manage case where a value contains this delimiter
        s << " < " << delimiter << "\n";
        s << values.front() << "\n";
        s << delimiter;

    } else {
        std::list<std::string>::const_iterator v;
        for (v = values.begin(); v != values.end(); v++) {
            s << " " << serializeSimpleToken(*v);
        }
    }
    s << "\n";
    return s.str();
}

std::string popListToken(std::list<std::string> &tokens)
{
    if (tokens.empty()) return "";
    std::string token = tokens.front();
    tokens.pop_front();
    return token;
}

std::string serializeTokens(const std::list<std::list<std::string> > &linesOfTokens)
{
    std::string result;
    std::list<std::list<std::string> >::const_iterator line;
    FOREACH(line, linesOfTokens) {
        std::list<std::string>::const_iterator token;
        FOREACH(token, (*line)) {
            result += serializeSimpleToken(*token);
            result += ' '; // separator on the same line
        }
        result += '\n'; // line separator
    }
    return result;
}
