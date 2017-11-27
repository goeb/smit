#ifndef _subprocess_h
#define _subprocess_h

#include <string>

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
    int wait();
    int write(const std::string &data);
    int read(std::string &data, StandardFd fd=SUBP_STDOUT);
    void closeFd(StandardFd fd);

private:
    pid_t pid;
    int pipes[3][2]; // stdin/stdout/stderr, read/write,
    int initPipes();
    void setupChildPipes();
    void setupParentPipes();
};

#endif // _subprocess_h
