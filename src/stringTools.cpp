
#include <sstream>
#include <string.h>

#include "stringTools.h"

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
  */
std::string toString(const std::list<std::string> &values)
{
    std::ostringstream text;
    std::list<std::string>::const_iterator v;
    for (v=values.begin(); v!=values.end(); v++) {
        if (v != values.begin()) text << ", ";
        text << v->c_str();
    }
    return text.str();
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
