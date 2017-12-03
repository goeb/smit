#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdarg.h>

#include "subprocess.h"
#include "utils/logging.h"

#define BUF_SIZ 4096

int Subprocess::initPipes()
{
    int err;

    err = pipe(pipes[SUBP_STDIN]);
    if (err) {
        LOG_ERROR("Cannot initialize pipe Stdin: %s\n", STRERROR(errno));
        return -1;
    }

    err = pipe(pipes[SUBP_STDOUT]);
    if (err) {
        LOG_ERROR("Cannot initialize pipe Stdout: %s\n", STRERROR(errno));
        return -1;
    }

    err = pipe(pipes[SUBP_STDERR]);
    if (err) {
        LOG_ERROR("Cannot initialize pipe Stderr: %s\n", STRERROR(errno));
        return -1;
    }

    return 0;
}

#define ENSURE_NOT_012(_pipe_fd) \
    while (_pipe_fd <= 2) { \
        _pipe_fd = dup(_pipe_fd); \
    }

#define CLOSE_PIPE(_pipe_fd) do { close(_pipe_fd); _pipe_fd = -1; } while (0)

void Subprocess::setupChildPipes()
{
    // As dup2 closes the new file descriptor if previously open
    // we need to ensure that:
    // - a needed file descriptor is not closed,
    // - a file descriptor is not closed twice

    // first close the unneeded pipes
    CLOSE_PIPE(pipes[SUBP_STDIN][SUBP_WRITE]);
    CLOSE_PIPE(pipes[SUBP_STDOUT][SUBP_READ]);
    CLOSE_PIPE(pipes[SUBP_STDERR][SUBP_READ]);

    // if one of them is in 0..2, then duplicate and used another number
    ENSURE_NOT_012(pipes[SUBP_STDIN][SUBP_READ]);
    ENSURE_NOT_012(pipes[SUBP_STDOUT][SUBP_WRITE]);
    ENSURE_NOT_012(pipes[SUBP_STDERR][SUBP_WRITE]);

    dup2(pipes[SUBP_STDIN][SUBP_READ], STDIN_FILENO);
    dup2(pipes[SUBP_STDOUT][SUBP_WRITE], STDOUT_FILENO);
    dup2(pipes[SUBP_STDERR][SUBP_WRITE], STDERR_FILENO);

    // close the remaining unused pipes
    CLOSE_PIPE(pipes[SUBP_STDIN][SUBP_READ]);
    CLOSE_PIPE(pipes[SUBP_STDOUT][SUBP_WRITE]);
    CLOSE_PIPE(pipes[SUBP_STDERR][SUBP_WRITE]);
}

void Subprocess::setupParentPipes()
{
    CLOSE_PIPE(pipes[SUBP_STDIN][SUBP_READ]);
    CLOSE_PIPE(pipes[SUBP_STDOUT][SUBP_WRITE]);
    CLOSE_PIPE(pipes[SUBP_STDERR][SUBP_WRITE]);
}

/** Close a pipe of the parent side
 */
void Subprocess::closeStdin()
{
    close(pipes[SUBP_STDIN][SUBP_WRITE]);
    pipes[SUBP_STDIN][SUBP_WRITE] = -1;
}

Subprocess::Subprocess()
{
    int i, j;
    for (i=0; i<3; i++) for (j=0; j<2; j++) pipes[i][j] = -1;
}

Subprocess::~Subprocess()
{
    int i, j;
    for (i=0; i<3; i++) for (j=0; j<2; j++) {
        if (pipes[i][j] != -1) CLOSE_PIPE(pipes[i][j]);
    }
}

/** Launch a subprocess
 *
 * @param args
 * @param envp
 * @param dir
 */
Subprocess *Subprocess::launch(char *const argv[], char *const envp[], const char *dir)
{
    std::string debugStr;
    char *const *ptr = argv;
    while (*ptr) {
        debugStr += " ";
        debugStr += *ptr;
        ptr++;
    }
    const char *dirStr = "."; // use for debug
    if (dir) dirStr = dir;
    LOG_DIAG("Subprocess::launch: %s (%s)", debugStr.c_str(), dirStr);

    Subprocess *handler = new Subprocess();

    int err = handler->initPipes();
    if (err) {
        delete handler;
        return 0;
    }

    handler->pid = fork();
    if (handler->pid < 0) {
        LOG_ERROR("Cannot fork");
        delete handler;
        return 0;
    }

    if (handler->pid == 0) {
        // in child
        if (dir) {
            if (chdir(dir) != 0) {
                _exit(72);
            }
        }

        // setup redirection
        handler->setupChildPipes();

        execvpe(argv[0], argv, envp);
        _exit(72);
    }

    // in parent
    handler->setupParentPipes();

    return handler;
}

int Subprocess::wait()
{
    int status;
    int rc;
    rc = waitpid(pid, &status, 0);
    if (rc < 0) {
        LOG_ERROR("waitpid error: %s", STRERROR(errno));
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return -WTERMSIG(status);
    }
    LOG_ERROR("waitpid: unexpected code path");
    return -1;
}

/** Write to the standard input of the subprocess
 */
int Subprocess::write(const std::string &data)
{
    return write(data.data(), data.size());
}

int Subprocess::write(const char *data, size_t len)
{
    size_t remaining = len;

    while (remaining) {
        ssize_t n = ::write(pipes[SUBP_STDIN][SUBP_WRITE], data, remaining);
        if (n < 0) {
            LOG_ERROR("Subprocess::write() error: %s", STRERROR(errno));
            return -1;
        }
        remaining -= n;
        data += n;
    }
    return 0;

}


/** Read from the stdout or stderr of the child process
 */
int Subprocess::read(std::string &data, StandardFd fd)
{
    data.clear();
    char buffer[BUF_SIZ];
    while (1) {

        ssize_t n = ::read(pipes[fd][SUBP_READ], buffer, BUF_SIZ);

        if (n < 0) {
            LOG_ERROR("Subprocess::read() error: %s", STRERROR(errno));
            return -1;
        }

        if (n == 0) break; // end of file

        data.append(buffer, n);
    }

    return 0;
}

/** Get the next line from the child stdout
 *
 *  (including the LF if not at end of file)
 *  Return an empty string if end of file is reached or an error occurred.
 */
std::string Subprocess::getline()
{
    std::string result;
    int err = 0; // flag for error or end of file

    while (1) {
        // look if a whole line is available in the buffer
        size_t pos = getlineBuffer.find_first_of('\n');
        if (pos != std::string::npos) {

            result = getlineBuffer.substr(0, pos+1);

            // pop the line from the buffer
            getlineBuffer = getlineBuffer.substr(pos+1);
            break;
        }

        // so far no whole line has been received

        // check end of file or error
        if (err) {
            result = getlineBuffer;
            getlineBuffer = ""; // clear the buffer
            break;
        }

        // read from the pipe
        uint8_t localbuf[BUF_SIZ];
        ssize_t n = ::read(pipes[SUBP_STDOUT][SUBP_READ], localbuf, BUF_SIZ);

        if (n < 0) {
            LOG_ERROR("Subprocess::getline() error: %s", STRERROR(errno));
            err = 1;
            // error is not raised immediately. First process
            // data previously received in the buffer.
        }

        if (n == 0) err = 1;

        // concatenate in the buffer
        getlineBuffer.append((char*)localbuf, n);
    }

    return result;
}

std::string Subprocess::getStdout()
{
    std::string data;
    read(data, SUBP_STDOUT);
    return data;
}

std::string Subprocess::getStderr()
{
    std::string data;
    read(data, SUBP_STDERR);
    return data;
}


// ------------- class Argv
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
