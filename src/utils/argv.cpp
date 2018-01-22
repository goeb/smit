#include <stdarg.h>

#include "argv.h"

/** Set a list of arguments (must be terminated by a null pointer)
 */
void Argv::set(const char *first, ...) {
    argv.clear();
    va_list ap;
    va_start(ap, first);
    vappend(first, ap);
    va_end(ap);
}

/** Continue a list of arguments (must be terminated by a null pointer)
 */
void Argv::append(const char *first, ...) {
    va_list ap;
    va_start(ap, first);
    vappend(first, ap);
    va_end(ap);
}

char *const* Argv::getv()
{
    return (char *const*) &argv[0];
}

std::string Argv::toString(const char *separator)
{
    std::string result;
    std::vector<const char*>::const_iterator arg;
    for(arg = argv.begin(); arg < argv.end(); arg++) {
        if (!*arg) continue;
        if (separator && arg != argv.begin()) result += separator;
        result += *arg;
    }
    return result;
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
    argv.push_back(first);
    char *p;
    while((p = va_arg(ap, char *))) {
        argv.push_back(p);
    }
    argv.push_back(0); // add a terminating null
}
