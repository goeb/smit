#ifndef _subprocess_h
#define _subprocess_h

#include <string>
#include <vector>

enum StandardFd {
    SUBP_STDIN = 0,
    SUBP_STDOUT = 1,
    SUBP_STDERR = 2
};

enum PipeDirection {
    SUBP_READ = 0,
    SUBP_WRITE = 1
};

class Subprocess {

public:
    static Subprocess *launch(char *const argv[], char * const envp[], const char *dir);
    Subprocess();
    ~Subprocess();
    int wait();
    int write(const std::string &data);
    int read(std::string &data, StandardFd fd=SUBP_STDOUT);
    std::string getline();
    void closeStdin();
    std::string getStdout();
    std::string getStderr();

private:
    pid_t pid;
    int pipes[3][2]; // stdin/stdout/stderr, read/write,
    int initPipes();
    void setupChildPipes();
    void setupParentPipes();
    std::string getlineBuffer; // data read but not yet consumed

};

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
    char *const* getv();

private:
    std::vector<const char*> argv;
    void vappend(const char *first, va_list ap);
};


#endif // _subprocess_h
