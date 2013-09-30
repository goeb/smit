
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
