#include <stdint.h>
#include "global.h"
#include <stdio.h>

#include "jTools.h"

/** Convert a string to a javascript-compatible string
  *
  * The surrounding quotes are not included and must be added by the caller.
  *
  * Eg: abcµ! => abcµ\x21
  */
std::string enquoteJs(const std::string &in)
{
    size_t n = in.size();
    std::string result;
    size_t i;
    for (i=0; i<n; i++) {
        // keep alpha numeric and utf-8 characters unchanged, and escape all others with \x..
        char c = in[i];
        if (c >= '0' && c <= '9') result += c;
        else if (c >= 'a' && c <= 'z') result += c;
        else if (c >= 'A' && c <= 'Z') result += c;
        else if ((c & 0x80) != 0) result += c; // do not escape utf-8 characters
        else { // escape other characters
            char ord[3];
            snprintf(ord, sizeof(ord), "%02x", (uint8_t)c);
            result += "\\x";
            result += ord;
        }
    }
    return result;
}

/** Convert a list of strings to a javascript array
  *
  * Eg:
  * abc, def$, 123*
  *                    => [ 'abc', 'def\x24', '123\x2a']
  */
std::string toJavascriptArray(const std::list<std::string> &items)
{
    std::string jarray = "[";
    std::list<std::string>::const_iterator v;
    FOREACH(v, items) {
        if (v != items.begin()) {
            jarray += ", ";
        }
        jarray += '\'' + enquoteJs(*v) + '\'';
    }
    jarray += "]";
    return jarray;

}

/** Convert a C++ std::string to a JSON string
  *
  * @return quote-delimited JSON string
  *     - control characters are discarded, except: \b \f \n \r \t
  *     - utf-8 encoded unicode characters are kept unchanged, except:
  *     " \ / (they are \-escaped)
  */
std::string toJsonString(const std::string &in)
{
    size_t n = in.size();
    std::string result = "\"";
    size_t i;
    for (i=0; i<n; i++) {
        unsigned char c = in[i];
        if (c < ' ') {
            // this is a control character
            if (c == '\b') result += "\\b";
            else if (c == '\f') result += "\\f";
            else if (c == '\n') result += "\\n";
            else if (c == '\r') result += "\\r";
            else if (c == '\t') result += "\\t";
            // else the character is discarded
        }
        else if (c == '"') result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else result += c;
    }
    result += '"'; // closing double-quote
    return result;
}
