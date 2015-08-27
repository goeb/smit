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

#include <sstream>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>

#include "stringTools.h"
#include "global.h"

std::string bin2hex(const ustring & in)
{
    return bin2hex(in.data(), in.size());
}

std::string bin2hex(const uint8_t *buffer, size_t len)
{
    const char hexTable[] = { '0', '1', '2', '3',
                              '4', '5', '6', '7',
                              '8', '9', 'a', 'b',
                              'c', 'd', 'e', 'f' };
    std::string hexResult;
    size_t i;
    for (i=0; i<len; i++) {
        int c = buffer[i];
        hexResult += hexTable[c >> 4];
        hexResult += hexTable[c & 0x0f];
    }
    return hexResult;
}


/** take first token name out of uri
  * Examples:
  *     popToken("/a/b/c", '/') -> return "a" and set uri to "b/c"
  *     popToken("a/b/c", '/') -> return "a" and set uri to "b/c"
  *     popToken("x=1&y=2", '&') -> return "x=1" and set uri to "y=2"
  */
std::string popToken(std::string & uri, char separator)
{
    if (uri.empty()) return "";

    size_t i = 0;

    // convert char to char *
    char sepStr[2];
    sepStr[0] = separator;
    sepStr[1] = 0;

    trimLeft(uri, sepStr);

    size_t pos = uri.find_first_of(sepStr, i); // skip the first leading / of the uri
    std::string firstToken = uri.substr(i, pos-i);

    if (pos == std::string::npos) uri = "";
    else {
        uri = uri.substr(pos);
        trimLeft(uri, sepStr);
    }

    return firstToken;
}

/** Remove characters at the end of string
  */
void trimRight(std::string &s, const char *c)
{
    size_t i = s.size()-1;
    while ( (i>=0) && strchr(c, s[i]) ) i--;

    if (i < 0) s = "";
    else s = s.substr(0, i+1);
}

/** Remove characters at the beginning of string
  */
void trimLeft(std::string &s, const char* c)
{
    size_t i = 0;
    while ( (s.size() > i) && strchr(c, s[i]) ) i++;

    if (i >= s.size()) s = "";
    else s = s.substr(i);
}

void trim(std::string &s, const char *c)
{
    trimLeft(s, c);
    trimRight(s, c);
}

void trimBlanks(std::string &s)
{
    trimLeft(s, "\n\t\r ");
    trimRight(s, "\n\t\r ");
}

/** Concatenate a list of strings to a string
  * By default the strings are separated by ", "
  */
std::string toString(const std::list<std::string> &values, const char *sep)
{
    std::string text;
    std::list<std::string>::const_iterator v;
    for (v=values.begin(); v!=values.end(); v++) {
        if (v != values.begin()) {
            if (sep) text += sep;
            else text += ", ";
        }
        text += v->c_str();
    }
    return text;
}

/** Decode an URL
  *
  * Example: Hello+World%20And%2FMore
  * => Hello World And/More
  */
std::string urlDecode(const std::string &src, int is_form_url_encoded, char mark)
{
    size_t i, a, b;
    std::string dst;
#define HEXTOI(x) (isdigit(x) ? x - '0' : x - 'W')
    size_t n = src.size();
    for (i = 0; i < n; i++) {
        if (src[i] == mark && i < n - 2 &&
                isxdigit((const unsigned char)src[i+1]) &&
                isxdigit((const unsigned char)src[i+2])) {
            a = tolower((const unsigned char)src[i+1]);
            b = tolower((const unsigned char)src[i+2]);
            dst += (char) ((HEXTOI(a) << 4) | HEXTOI(b));
            i += 2;
        } else if (is_form_url_encoded && src[i] == '+') {
            dst += ' ';
        } else {
            dst += src[i];
        }
    }

    return dst;
}

/** Characters reserved by rfc3986 (Uniform Resource Identifier (URI): Generic Syntax)
  *
  *   reserved    = gen-delims / sub-delims
  *
  *   gen-delims  = ":" / "/" / "?" / "#" / "[" / "]" / "@"
  *
  *   sub-delims  = "!" / "$" / "&" / "'" / "(" / ")"
  *               / "*" / "+" / "," / ";" / "="
  *
  *  "%" and " " are also added.
  */
const char *getReservedUriCharacters()
{
    return ":/?#[]@!$&\"'()*+,;=% ";
}

/** Encode a string for URL
  *
  * Used typically for a parameter in the query string.
  * Also used for file names than may originally cause conflicts with slashes, etc.
  */
std::string urlEncode(const std::string &src, char mark, const char *dontEscape)
{
    static const char *hex = "0123456789abcdef";
    std::string dst;
    size_t n = src.size();
    size_t i;
    for (i = 0; i < n; i++) {
        char c = src[i];
        if (strchr(dontEscape, c)) dst += src[i]; // do not escape
        else if (strchr(getReservedUriCharacters(), c)) {
            // Do escape reserved characters
            // 'c' is a supposed to have a value 0 <= c < 128
            dst += mark;
            dst += hex[c >> 4];
            dst += hex[c & 0xf];
        } else {
            // Do not escape other characters.
            // UTF-8 characters fall into this category.
            dst += src[i];
        }
    }
    return dst;
}

std::string htmlEscape(const std::string &value)
{
    std::string result = replaceAll(value, '&', "&#38;");
    result = replaceAll(result, '"', "&quot;");
    result = replaceAll(result, '<', "&lt;");
    result = replaceAll(result, '>', "&gt;");
    result = replaceAll(result, '\'', "&#39;");
    return result;
}

std::string pop(std::list<std::string> & L)
{
    std::string token = "";
    if (!L.empty()) {
        token = L.front();
        L.pop_front();
    }
    return token;
}

std::string getProperty(const PropertiesMap &properties, const std::string &name)
{
    std::map<std::string, std::list<std::string> >::const_iterator t = properties.find(name);
    std::string propertyValue = "";
    if (t != properties.end() && (t->second.size()>0) ) propertyValue = toString(t->second);

    return propertyValue;
}

/** Replace all occurrences of the given character by a string
  *
  * Example: replaceAll(in, '"', "&quot;")
  * Replace all " by &quot;
  */
std::string replaceAll(const std::string &in, char c, const char *replaceBy)
{
    std::string out;
    size_t len = in.size();
    size_t i = 0;
    size_t savedOffset = 0;
    while (i < len) {
        if (in[i] == c) {
            if (savedOffset < i) out += in.substr(savedOffset, i-savedOffset);
            out += replaceBy;
            savedOffset = i+1;
        }
        i++;
    }
    if (savedOffset < i) out += in.substr(savedOffset, i-savedOffset);
    return out;
}

std::list<std::string> split(const std::string &s, const char *c, int limit)
{
    // use limit = -1 for no limit (almost)
    std::list<std::string> tokens;
    size_t found;

    int index = 0;
    found = s.find_first_of(c, index);
    while ( (found != std::string::npos) && (limit != 0) )
    {
        tokens.push_back(s.substr(index, found-index));

        index = found + 1;
        found = s.find_first_of(c, index);
        limit --;
    }
    tokens.push_back(s.substr(index));

    return tokens;
}

std::list<std::string> splitLinesAndTrimBlanks(const std::string &s)
{
    std::list<std::string> lines;
    size_t found;

    int index = 0;
    while ((found = s.find('\n', index)) != std::string::npos)
    {
        std::string line = s.substr(index, found-index);
        trimBlanks(line);
        lines.push_back(line);

        index = found + 1;
    }
    std::string lastLine = s.substr(index);
    trimBlanks(lastLine);
    lines.push_back(lastLine);

    return lines;
}


std::string join(const std::list<std::string> &items, const char *separator)
{
    std::string out;
    std::list<std::string>::const_iterator i;
    FOREACH(i, items) {
        if (i != items.begin()) out += separator;
        out += (*i);
    }
    return out;
}

/** basename / -> ""
  * basename . -> .
  * basename "" -> ""
  * basename a/b/c -> c
  * basename a/b/c/ -> c
  */
std::string getBasename(const std::string &path)
{
    if (path.empty()) return "";
    size_t i;
#if defined(_WIN32)
    i = path.find_last_of("/\\");
#else
    i = path.find_last_of("/");
#endif
    if (i == std::string::npos) return path;
    else if (i == path.size()-1) return getBasename(path.substr(0, path.size()-1));
    else return path.substr(i+1);
}

std::string getDirname(const std::string &path)
{
    char *buf = (char*)malloc(path.size()+1);
    memcpy(buf, path.data(), path.size());
    buf[path.size()] = 0;

    char *dir = dirname(buf);
    std::string result = dir;

    free(buf);
    return result;
}


/** If uri is "x=y&a=bc+d" and param is "a"
  * then return "bc d".
  * @param param
  *     Must be url-encoded.
  *
  * @return
  *     Url-decoded value
  */
std::string getFirstParamFromQueryString(const std::string & queryString, const char *param)
{
    std::string q = queryString;
    std::string paramEqual = param;
    paramEqual += "=";
    std::string token;
    while ((token = popToken(q, '&')) != "") {
        if (0 == token.compare(0, paramEqual.size(), paramEqual.c_str())) {
            popToken(token, '='); // remove the 'param=' part
            token = urlDecode(token);

            return token;
        }
    }
    return "";
}

/** if uri is "x=y&a=bcd&a=efg" and param is "a"
  * then return a list [ "bcd", "efg" ]
  * @return
  *     Url-decoded values
  */
std::list<std::string> getParamListFromQueryString(const std::string & queryString, const char *param)
{
    std::list<std::string> result;
    std::string q = queryString;
    std::string paramEqual = param;
    paramEqual += "=";
    std::string token;
    while ((token = popToken(q, '&')) != "") {
        if (0 == token.compare(0, paramEqual.size(), paramEqual.c_str())) {
            popToken(token, '='); // remove the param= part
            token = urlDecode(token);

            result.push_back(token);
        }
    }
    return result;
}


std::string toString(int n)
{
    char buffer[128];
    sprintf(buffer, "%d", n);
    return buffer;
}

/** Indent lines of a text
  */
void printfIndent(const char *text, const char *indent)
{
    size_t n = strlen(text);
    size_t i;
    for (i = 0; i < n; i++) {
        if (text[i] == '\r') continue; // skip this
        if (i==0) printf("%s", indent); // indent first line of message
        printf("%c", text[i]);
        if (text[i] == '\n') printf("%s", indent); // indent each new line
    }
}
