#ifndef _argv_h
#define _argv_h

#include <vector>
#include <string>
#include <stdarg.h>

/** Helper for passing command line arguments
 *
 *  Typical usage:
 *  Argv argv();
 *  argv.set("ls", "-l", 0);
 *  Subprocess::launch(argv.getv(), 0, 0);
 */
class Argv {
public:
    void set(const char *first, ...);
    void append(const char *first, ...);
    char *const* getv() const;
    std::string toString(const char *separator=", ") const;

private:
    std::vector<const char*> argv;
    void vappend(const char *first, va_list ap);
};

#endif // _argv_h
