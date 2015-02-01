#ifndef _filesystem_h
#define _filesystem_h

#include <string>
#include <sys/types.h>
#include <dirent.h>

int loadFile(const char *filepath, std::string &data);
int loadFile(const char *filepath, const char **data);
int writeToFile(const char *filepath, const std::string &data);
int writeToFile(const char *filepath, const char *data, size_t len);

bool fileExists(std::string &path);

std::string getExePath();
std::string getFileSize(const std::string &path);

DIR *openDir(const char *path);
std::string getNextFile(DIR *d);
void closeDir(DIR *d);

int removeDir(const std::string &path);
int copyFile(const std::string &srcPath, const std::string &destPath);


#endif
