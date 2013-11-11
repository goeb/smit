
#include <sstream>
#include <string.h>


#include "stringTools.h"
#include "global.h"

/** take first token name out of uri
  * /a/b/c -> a and b/c
  * a/b/c -> a and b/c
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
std::string urlDecode(const std::string &src, int is_form_url_encoded)
{
    size_t i, a, b;
    std::string dst;
#define HEXTOI(x) (isdigit(x) ? x - '0' : x - 'W')
    size_t n = src.size();
    for (i = 0; i < n; i++) {
        if (src[i] == '%' && i < n - 2 &&
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

/** Encode a string for URL
  *
  * Used typically for a parameter in the query string.
  * Also used for file names than may originally cause conflicts with slashes, etc.
  */
std::string urlEncode(const std::string &src)
{
    static const char *dontEscape = "._-$,;~()";
    static const char *hex = "0123456789abcdef";
    std::string dst;
    size_t n = src.size();
    size_t i;
    for (i = 0; i < n; i++) {
        if (isalnum((unsigned char) src[i]) ||
                strchr(dontEscape, (unsigned char) src[i]) != NULL) dst += src[i];
        else {
            dst += '%';
            dst += hex[((const unsigned char) src[i]) >> 4];
            dst += hex[((const unsigned char) src[i]) & 0xf];
        }
    }
    return dst;
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

std::string getProperty(const std::map<std::string, std::list<std::string> > &properties, const std::string &name)
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

std::string toJavascriptArray(const std::list<std::string> &items)
{
    std::string jarray = "[";
    std::list<std::string>::const_iterator v;
    FOREACH(v, items) {
        if (v != items.begin()) {
            jarray += ", ";
        }
        jarray += '\'' + replaceAll(*v, '\'', "\\'") + '\'';
    }
    jarray += "]";
    return jarray;

}

std::vector<std::string> split(const std::string &s, const char *c, int limit)
{
    // use limit = -1 for no limit (almost)
    std::vector<std::string> tokens;
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

