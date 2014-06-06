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
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>

#include "parseConfig.h"
#include "global.h"

// SM_PARSER is a small executable than helps parsing smit config file and smit entries.
#ifndef SM_PARSER

#include "logging.h"

#else

#define LOG_DEBUG(...)
#define LOG_INFO(...)
#define LOG_ERROR(...)

#endif


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
        P_NONE,
        P_IN_TOKEN,
        P_IN_COMMENT,
        P_IN_BOUNDARY_HEADER,
        P_IN_BOUNDARY
    };
    enum State state = P_NONE;
    std::string token; // current token
    std::list<std::string> line; // current line
    std::string boundary;
    std::string boundedText;
    bool doubleQuoted = false;
    bool backslash = false;
    bool percent = false;

    for (i=0; i<len; i++) {
        char c = buf[i];
        switch (state) {
        case P_IN_COMMENT:
            if (c == '\n') state = P_NONE;
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
                    state = P_NONE;
                    line.push_back(boundedText);
                    boundedText.clear();
                    linesOftokens.push_back(line);
                    line.clear();
                }
            }
            // accumulate character if still in same state
            if (state == P_IN_BOUNDARY) boundedText += c;

            break;

        case P_IN_TOKEN:
            if (backslash) {
                if (c == 'n') token += '\n';
                else if (c == 'r') token += '\r';
                else if (c == 't') token += '\t';
                else if (c == '\n') ; // nothing particular here, continue on next line
                else token += c;

                backslash = false;

            } else if (percent) {
                token += '%';
                percent = false;

                if (c == '%') ; // correct syntax, do nothing more
                else i--; // incorrect syntax, replay this character

            } else if (doubleQuoted) {
                if (c == '"') doubleQuoted = false; // end of the "..." portion
                else if (c == '%') percent = true;
                else if (c == '\\') backslash = true;
                else token += c;

            } else {
                if (c == '"') doubleQuoted = true;
                else if (c == '%') percent = true;
                else if (c == '\\') backslash = true;
                else if (c == '\t' || c == ' ') {
                    // end of token
                    line.push_back(token);
                    token.clear();
                    state = P_NONE;

                } else if (c == '\r' || c == '\n') {
                    // end of token and end of line
                    line.push_back(token);
                    token.clear();
                    linesOftokens.push_back(line);
                    line.clear();
                    state = P_NONE;

                } else token += c;
            }
            break;

        case P_NONE:
        default:
            if (c == '\r') ; // do nothing, ignore this
            else if (backslash) {
                if (c == '\n') backslash = false; // do nothing (concatenate next line with current line)
                else {
                    // this is part of a token
                    // replay this character in P_IN_TOKEN
                    i--;
                    state = P_IN_TOKEN;
                }
            } else if (c == '\n') { // go to next line
                if (line.size()) { linesOftokens.push_back(line); line.clear(); }
            } else if (c == ' ' || c == '\t') ; // do nothing
            else if (c == '#') {
                if (line.size()) { linesOftokens.push_back(line); line.clear(); }
                state = P_IN_COMMENT;
            } else if (c == '\\')  backslash = true;
            else if (c == '<') {
                state = P_IN_BOUNDARY_HEADER;
                boundary.clear();
            } else {
                // replay this character in P_IN_TOKEN
                state = P_IN_TOKEN;
                i--;
            }
            break;
        }
    }
    // purge remaininig token and line
    if (state == P_IN_TOKEN) line.push_back(token);
    if (line.size()) linesOftokens.push_back(line);

    return linesOftokens;
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

/** Look if a list contains a specific item
  */
bool has(const std::list<std::string> &L, const std::string item)
{
    std::list<std::string>::const_iterator i;
    FOREACH(i, L) {
        if (*i == item) return true;
    }
    return false;
}

/** Parse a colspec and return a list of properties
  *
  * "aaa bb ccc" or "aaa+bb+ccc"
  * @return aaa, bbb, ccc
  */
std::list<std::string> parseColspec(const char *colspec, const std::list<std::string> &knownProperties)
{
    std::list<std::string> result;
    size_t i = 0;
    size_t L = strlen(colspec);
    std::string currentToken;
    for (i=0; i<L; i++) {
        char c = colspec[i];
        if (c == ' ' || c == '+') {
            // push previous token if any
            if (currentToken.size() > 0) {
                if (knownProperties.empty() || has(knownProperties, currentToken)) result.push_back(currentToken);
            }
            currentToken.clear();
        } else currentToken += c;
    }
    if (currentToken.size() > 0 && (knownProperties.empty() || has(knownProperties, currentToken)) ) result.push_back(currentToken);
    return result;
}

/** Double quotes are needed whenever the value contains one of: \n \t \r " %
  *
  * The resulting can be passed to printf for deserializing.
  * Except some rare characters that are not managed (\a, \b \xHH, etc.)
  */
std::string doubleQuote(const std::string &input)
{
    size_t n = input.size();
    std::string result = "\"";

    size_t i;
    for (i=0; i<n; i++) {
        if (input[i] == '\\') result += "\\\\";
        else if (input[i] == '"') result += "\\\"";
        else if (input[i] == '\n') result += "\\n";
        else if (input[i] == '\t') result += "\\t";
        else if (input[i] == '\r') result += "\\r";
        else if (input[i] == '%') result += "%%";
        else result += input[i];
    }
    result += '"';
    return result;
}

std::string serializeSimpleToken(const std::string token)
{
    if (token.empty()) return "\"\"";
    else if (token.find_first_of("!()[]&#\n \t\r\"<%") == std::string::npos) return token;
    else return doubleQuote(token); // some characters needs escaping
}

std::string getBoundary(const std::string &text)
{
    std::stringstream randomStr;

    while(1) {
        randomStr.str(""); // clear the contents
        randomStr << std::hex << rand() << rand() << rand();
        if (text.find(randomStr.str()) == std::string::npos) break;
    }
    std::string boundary = "boundary:" + randomStr.str();
    LOG_DEBUG("boundary: %s", boundary.c_str());
    return boundary;
}

std::string serializeProperty(const std::string &propertyName, const std::list<std::string> &values)
{
    std::ostringstream s;
    s << propertyName; // preamble that indicates the name of the property

    if ( (values.size() == 1) && (values.front().find('\n') != std::string::npos) ) {
        // serialize as multi-line
        std::string delimiter = getBoundary(values.front());
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


#ifdef SM_PARSER

/** smparser is a standalone executable that help parsing smit config and entries syntax.
  * It may be useful for triggers that need parsing of entries.
  *
  *
  */
#include <iostream>
void usage()
{
    printf("Usage:\n"
           "   1. smparser <file> [<key>] [<n>]\n"
           "   2. smparser -e\n"
           "\n"
           "1.\n"
           "<file>  file to parse. Use - to specify standard input.\n"
           "<n>     value to retrieve. Defaults to 1.\n"
           "\n"
           "Example:\n"
           "cat << EOF | smparser - author 2\n"
           "author John Harper\n"
           "ctime 1396295409\n"
           "EOF\n"
           "\n"
           "This shall print 'Harper' (ie: the second value of line with key 'author').\n"
           "\n"
           "2. smparser -e\n"
           "    Take on stdin a value, and encode it.\n"
           );
    exit(1);
}

int encodeStdin()
{
    std::string data;
    std::string line;
    while (getline(std::cin, line)) {
        if (data.size()) data += '\n';
        data += line;
    }
    printf("%s", serializeSimpleToken(data).c_str());
    return 0;
}

int main(int argc, char **argv)
{

    if (argc < 2) usage();
    if (argc > 4) usage();

    if (0 == strcmp (argv[1], "-e")) {
        return encodeStdin();
    }
    const char *file = argv[1];
    const char *key = 0;
    if (argc >= 2) key = argv[2];
    int n = 1;
    if (argc == 4) n = atoi(argv[3]);

    std::string data;
    if (0 == strcmp(file, "-")) {
        // read from stdin
        std::string line;
        while (getline(std::cin, line)) {
            if (data.size()) data += '\n';
            data += line;
        }
    } else {
        int r = loadFile(file, data);
        if (r < 0) {
            fprintf(stderr, "Error: cannot open file '%s'\n", file);
            return 1;
        }
    }

    std::list<std::list<std::string> > tokens = parseConfigTokens(data.c_str(), data.size());
    std::list<std::list<std::string> >::iterator line;

    if (key) {
        FOREACH(line, tokens) {
            if (line->size() > 0 && line->front() == key) {
                // key found
                if (line->size() > n) {
                    int i;
                    for (i=0; i<n; i++) line->pop_front();
                    printf("%s", line->front().c_str());
                }
                printf("\n");
                return 0;
            }
        }
        return 1;

    } else {
        // print each value prefiex by line.column
        // 1.1 ...
        // 1.2 ...
        // 1.3 ...
        // 2.1 ...
        int row = 1;
        FOREACH(line, tokens) {
            std::list<std::string>::const_iterator tok;
            int column = 1;
            FOREACH(tok, (*line)) {
                printf("smp.%d.%d %s\n", row, column, tok->c_str());
                column++;
            }
            row++;
        }
    }
}

#endif
