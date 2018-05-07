#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdarg.h>
#include <fcntl.h>

#include "subprocess.h"
#include "utils/logging.h"

#define BUF_SIZ 4096

/** Initialize the pipes for communication between parent and child
 *
 *  On error, -1 is returned. Some pipes may have been initialized and others not.
 *
 */
int Subprocess::initPipes()
{
    int err;

    err = pipe2(pipes[SUBP_STDIN], O_CLOEXEC);
    if (err) {
        LOG_ERROR("Cannot initialize pipe Stdin: %s\n", STRERROR(errno));
        return -1;
    }

    err = pipe2(pipes[SUBP_STDOUT], O_CLOEXEC);
    if (err) {
        LOG_ERROR("Cannot initialize pipe Stdout: %s\n", STRERROR(errno));
        return -1;
    }

    err = pipe2(pipes[SUBP_STDERR], O_CLOEXEC);
    if (err) {
        LOG_ERROR("Cannot initialize pipe Stderr: %s\n", STRERROR(errno));
        return -1;
    }

    return 0;
}

/** Move the file descriptor to a value greater than 2
 *
 *  When duplicated, the original file descriptor and
 *  other created file descriptors are not closed.
 *  The caller is suppose to take care of closing them.
 *
 *  This function is intended to be called in the
 *  child between the fork and the exec.
 */
static void childMoveFdAbove2(int &fd)
{
    if (fd <= 2) {
        // need to be duplicated to a value >= 3
        int newFd = dup(fd);
        if (-1 == newFd) {
            // This is an unpleasant case.
            // Logging cannot be done, as we are between fork and exec
            return;
        }
        childMoveFdAbove2(newFd);
        fd = newFd;
    }
}

void Subprocess::childSetupPipes()
{
    // We are between fork and exec. We need to setup the
    // standard files descriptors 0, 1, 2 of the child.

    // the pipes have been open with the flag O_CLOEXEC, therefore
    // we do not care to close them explicitely (the exec will).
    childMoveFdAbove2(pipes[SUBP_STDIN][SUBP_READ]);
    childMoveFdAbove2(pipes[SUBP_STDOUT][SUBP_WRITE]);
    childMoveFdAbove2(pipes[SUBP_STDERR][SUBP_WRITE]);

    // The close-on-exec flag for the duplicate descriptor is off
    dup2(pipes[SUBP_STDIN][SUBP_READ], STDIN_FILENO);
    dup2(pipes[SUBP_STDOUT][SUBP_WRITE], STDOUT_FILENO);
    dup2(pipes[SUBP_STDERR][SUBP_WRITE], STDERR_FILENO);
}

static void closePipe(int &fd)
{
    int err = close(fd);
    if (err) {
        LOG_ERROR("closePipe: cannot close %d: %s", fd, STRERROR(errno));
        return;
    }
    fd = -1;
}

void Subprocess::parentClosePipes()
{
    closePipe(pipes[SUBP_STDIN][SUBP_READ]);
    closePipe(pipes[SUBP_STDOUT][SUBP_WRITE]);
    closePipe(pipes[SUBP_STDERR][SUBP_WRITE]);
}

/** Close a pipe of the parent side
 */
void Subprocess::closeStdin()
{
    closePipe(pipes[SUBP_STDIN][SUBP_WRITE]);
}

/** Close stdin, stdout, sterr
 */
void Subprocess::closePipes()
{
    int i, j;
    for (i=0; i<3; i++) for (j=0; j<2; j++) {
        if (pipes[i][j] != -1) closePipe(pipes[i][j]);
    }
}

/** Request the end of the child process and wait
 *
 *  At the moment, the termination of the child process
 *  relies on the closing of the file descriptors.
 */
void Subprocess::shutdown()
{
    closePipes();
    int err = wait();
    if (err) {
        LOG_INFO("Subprocess::shutdown: err=%d", err);
    }
}


Subprocess::Subprocess()
{
    int i, j;
    for (i=0; i<3; i++) for (j=0; j<2; j++) pipes[i][j] = -1;
}

Subprocess::~Subprocess()
{
    // close remaining open pipes
    closePipes();
}

/** Launch a subprocess
 *
 * @param args
 * @param envp
 * @param dir
 */
Subprocess *Subprocess::launch(char *const argv[], char *const envp[], const char *dir)
{
    std::string debugArgv;
    char *const *ptr;

    ptr = argv;
    while (*ptr) {
        if (ptr != argv) debugArgv += " ";
        debugArgv += *ptr;
        ptr++;
    }

    std::string debugEnvp;
    if (envp) {
        ptr = envp;
        while (*ptr) {
            if (ptr != envp) debugEnvp += " ";
            debugEnvp += *ptr;
            ptr++;
        }
    }

    const char *dirStr = "."; // use for debug
    if (dir) dirStr = dir;
    LOG_DIAG("Subprocess::launch: %s (env=%s, dir=%s)", debugArgv.c_str(), debugEnvp.c_str(), dirStr);

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
        handler->childSetupPipes();

        execvpe(argv[0], argv, envp);
        _exit(72);
    }

    // in parent
    handler->parentClosePipes();

    return handler;
}

/** Launch the subprocess, and wait for its return
 *
 */
int Subprocess::launchSync(char *const argv[], char * const envp[], const char *dir,
                           const char *subStdin, size_t subStdinSize, std::string &subStdout, std::string &subStderr)
{
    Subprocess *subp = Subprocess::launch(argv, envp, dir);
    if (!subp) return -1;

    if (subStdin) {
        subp->write(subStdin, subStdinSize);
    }

    subp->closeStdin();

    subStdout = subp->getStdout();
    subStderr = subp->getStderr();

    int err = subp->wait();
    delete subp;
    return err;
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

/** Read bytes from the stdout of the child process
 */
int Subprocess::read(char *buffer, size_t size, StandardFd fd)
{
    size_t remaining = size;

    if (fd == SUBP_STDOUT && !getlineBuffer.empty()) {
        LOG_DIAG("getlineBuffer: %s", getlineBuffer.c_str());
        // if calls to getline() and read() are mixed, we need to take care
        // of the data buffered by getline
        if (getlineBuffer.size() < size) {
            // all the buffered data shall be consumed
            memcpy(buffer, getlineBuffer.data(), getlineBuffer.size());
            remaining -= getlineBuffer.size();
            buffer += getlineBuffer.size();
            getlineBuffer.clear();

        } else {
            // requested size <= buffered data size
            // (there is more buffered data than requested)
            memcpy(buffer, getlineBuffer.data(), size);
            getlineBuffer = getlineBuffer.substr(size);
            return size;
        }
    }

    while (remaining) {

        ssize_t n = ::read(pipes[fd][SUBP_READ], buffer, remaining);

        if (n < 0) {
            LOG_ERROR("Subprocess::read() error: %s", STRERROR(errno));
            return -1;
        } else {
            LOG_DEBUG("Subprocess::read(): recv %ld bytes", (long)n);
        }

        remaining -= n;
        buffer += n;

        if (n == 0) break; // end of file
    }

    return size-remaining;
}

/** Read all from the stdout or stderr of the child process
 */
int Subprocess::read(std::string &data, StandardFd fd)
{
    data.clear();

    char buffer[BUF_SIZ];
    int n;
    while ( (n = read(buffer, BUF_SIZ, fd)) > 0) {
        data.append(buffer, n);
    }

    if (n == 0) return data.size();
    return n; // negative value
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
        } else {
            LOG_DEBUG("Subprocess::getline(): recv %ld bytes", (long)n);

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
