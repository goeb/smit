#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "argv.h"

/** Set a list of arguments (must be terminated by a null pointer)
 *
 *  The input char strings are duplicated in the Argv instance.
 */
void Argv::set(const char *first, ...) {
    argv.clear();
    va_list ap;
    va_start(ap, first);
    vappend(first, ap);
    va_end(ap);
}

/** Continue a list of arguments (must be terminated by a null pointer)
 *
 *  The input char strings are duplicated in the Argv instance.
 */
void Argv::append(const char *first, ...) {
    va_list ap;
    va_start(ap, first);
    vappend(first, ap);
    va_end(ap);
}

char *const* Argv::getv() const
{
    return (char *const*) &argv[0];
}

std::string Argv::toString(const char *separator) const
{
    std::string result;
    std::vector<char*>::const_iterator arg;
    for(arg = argv.begin(); arg < argv.end(); arg++) {
        if (!*arg) continue;
        if (separator && arg != argv.begin()) result += separator;
        result += *arg;
    }
    return result;
}

Argv::~Argv()
{
    std::vector<char*>::iterator arg;
    for(arg = argv.begin(); arg < argv.end(); arg++) {
        if (*arg) free(*arg);
    }
}

Argv::Argv(const Argv &other)
{
    std::vector<char*>::const_iterator arg;
    for(arg = other.argv.begin(); arg < other.argv.end(); arg++) {
        if (*arg) argv.push_back(strdup(*arg));
        else  argv.push_back(0);
    }
}


void Argv::vappend(const char *first, va_list ap) {
    if (argv.empty()) {
        // nothing to do
    } else {
        if (!argv.back()) {
            // remove terminating null
            argv.pop_back();
        }
    }
    if (first) {
        argv.push_back(strdup(first));
        char *p;
        while((p = va_arg(ap, char *))) {
            argv.push_back(strdup(p));
        }
    }
    argv.push_back(0); // add a terminating null
}
