
#include <sstream>
#include "stringTools.h"

/** take first token name out of uri
  * /a/b/c -> a and b/c
  * a/b/c -> a and b/c
  */
std::string popToken(std::string & uri, char separator)
{
    if (uri.empty()) return "";

    size_t i = 0;
    trimLeft(uri, separator);

    char sepStr[2];
    sepStr[0] = separator;
    sepStr[1] = 0;
    size_t pos = uri.find_first_of(sepStr, i); // skip the first leading / of the uri
    std::string firstToken = uri.substr(i, pos-i);

    if (pos == std::string::npos) uri = "";
    else {
        uri = uri.substr(pos);
        trimLeft(uri, separator);
    }

    return firstToken;
}

/** Remove characters at the end of string
  */
void trimRight(std::string &s, char c)
{
    size_t i = s.size()-1;
    while ( (i>=0) && (s[i] == c) ) i--;

    if (i < 0) s = "";
    else s = s.substr(0, i+1);
}

/** Remove characters at the beginning of string
  */
void trimLeft(std::string &s, char c)
{
    size_t i = 0;
    while ( (s.size() > i) && (s[i] == c) ) i++;

    if (i >= s.size()) s = "";
    else s = s.substr(i);
}

void trim(std::string &s, char c)
{
    trimLeft(s, c);
    trimRight(s, c);
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
    int i, j, a, b;
    std::string dst;
#define HEXTOI(x) (isdigit(x) ? x - '0' : x - 'W')
    int n = src.size();
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

