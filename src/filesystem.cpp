
#include "filesystem.hpp"


#if defined(_WIN32)

#include <windows.h>

std::string getExePath()
{
    char buffer[MAX_PATH];
    DWORD n = GetModuleFileName(NULL, buffer, MAX_PATH);
    std::string exePath(buffer, n);
    return exePath;
}

#else // Linux

std::string getExePath()
{
    char buffer[1024];
    ssize_t n = readlink( "/proc/self/exe", buffer, sizeof(buffer));

    if (n < 0) return "";

    std::string exePath(buffer, n);
    return exePath;
}

#endif
