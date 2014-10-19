#ifndef _filesystem_h
#define _filesystem_h

#include <string>

int loadFile(const char *filepath, std::string &data);
int loadFile(const char *filepath, const char **data);
int writeToFile(const char *filepath, const std::string &data);
int writeToFile(const char *filepath, const char *data, size_t len);

bool fileExists(std::string &path);

std::string getExePath();
std::string getFileSize(std::string &path);


#endif
