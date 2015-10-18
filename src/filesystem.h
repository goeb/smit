#ifndef _filesystem_h
#define _filesystem_h

#include <string>
#include <sys/types.h>
#include <dirent.h>

enum FileFlags {
    NO_OVERWRITE
};

int loadFile(const std::string &filepath, std::string &data);

int loadFile(const char *filepath, std::string &data);
int loadFile(const char *filepath, const char **data);
int writeToFile(const std::string &filepath, const std::string &data);
int writeToFile(const char *filepath, const char *data, size_t len);

bool fileExists(const std::string &path);
bool isDir(const std::string &path);
int mkdirs(const std::string &path);

std::string getExePath();
std::string getFileSize(const std::string &path);

DIR *openDir(const std::string &path);
std::string getNextFile(DIR *d);
void closeDir(DIR *d);

int removeDir(const std::string &path);
int copyFile(const std::string &srcPath, const std::string &destPath);
std::string getTmpPath(const std::string &path);

int cmpFiles(const std::string &srcPath, const std::string &path2);
int cmpContents(const char *contents, size_t size, const std::string &file);
int cmpContents(const std::string &contents, const std::string &file);

int mkdir(const std::string &path);
#if defined(_WIN32)
int mkdir(const char *path, int mode);
#endif


#endif
