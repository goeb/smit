
#include <unistd.h>

#include "filesystem.hpp"

#if defined(_WIN32)

#include <windows.h>

#else

#define MAX_PATH 2048

#endif

std::string getExePath()
{
    char buffer[MAX_PATH];

#if defined(_WIN32)
    DWORD n = GetModuleFileName(NULL, buffer, MAX_PATH);

#else // Linux
    ssize_t n = readlink( "/proc/self/exe", buffer, sizeof(buffer));
#endif
    if (n < 0) return "";

    std::string exePath(buffer, n);
    return exePath;
}

