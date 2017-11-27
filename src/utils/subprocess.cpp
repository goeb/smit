#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdint.h>

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

void Subprocess::setupChildPipes()
{
    dup2(pipes[SUBP_STDIN][SUBP_READ], STDIN_FILENO);
    close(pipes[SUBP_STDIN][SUBP_READ]);
    close(pipes[SUBP_STDIN][SUBP_WRITE]);

    dup2(pipes[SUBP_STDOUT][SUBP_WRITE], STDOUT_FILENO);
    close(pipes[SUBP_STDOUT][SUBP_READ]);
    close(pipes[SUBP_STDOUT][SUBP_WRITE]);

    dup2(pipes[SUBP_STDERR][SUBP_WRITE], STDERR_FILENO);
    close(pipes[SUBP_STDERR][SUBP_READ]);
    close(pipes[SUBP_STDERR][SUBP_WRITE]);

    // close all open file descriptors other than stdin, stdout, stderr
    //for (int fd=3; fd < sysconf(_SC_OPEN_MAX); fd++) {
    //    close(fd);
    //}
}

void Subprocess::setupParentPipes()
{
    close(pipes[SUBP_STDIN][SUBP_READ]);
    close(pipes[SUBP_STDOUT][SUBP_WRITE]);
    close(pipes[SUBP_STDERR][SUBP_WRITE]);
}

/** Close a pipe of the parent side
 */
void Subprocess::closeFd(StandardFd fd)
{
    if (fd == SUBP_STDIN) close(pipes[fd][SUBP_WRITE]);
    else close(pipes[fd][SUBP_READ]);
}

/** Launch a subprocess
 *
 * @param args
 * @param envp
 * @param dir
 */
Subprocess *Subprocess::launch(char *const argv[], char *const envp[], const char *dir)
{
// TODO catch SIGPIPE ?

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
    size_t remaining = data.size();

    while (remaining) {
        ssize_t n = ::write(pipes[SUBP_STDIN][SUBP_WRITE], data.data(), data.size());
        if (n < 0) {
            LOG_ERROR("Subprocess::write() error: %s", STRERROR(errno));
            return -1;
        }
        remaining -= n;
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

