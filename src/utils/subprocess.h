#ifndef _subprocess_h
#define _subprocess_h

#include <string>
#include <vector>
#include <stdarg.h>

#include "argv.h"

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
    static int launchSync(char *const argv[], char * const envp[], const char *dir,
                          const char *subStdin, size_t subStdinSize, std::string &subStdout, std::string &subStderr);
    Subprocess();
    ~Subprocess();
    int wait();
    int write(const std::string &data);
    int write(const char *data, size_t len);
    int read(char *buffer, size_t size);
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
    std::string getlineBuffer; // data read from stdout but not yet consumed

};

#endif // _subprocess_h
