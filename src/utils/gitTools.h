#ifndef _gitTools_h
#define _gitTools_h

#include <string>

#define SIZE_COMMIT_ID 40

int gitdbCommitMaster(const std::string &gitRepoPath, const std::string &subpath,
                      const std::string &data, const std::string &author);
int gitInit(const std::string &gitRepoPath);
int gitAdd(const std::string &gitRepoPath, const std::string &subpath);
int gitCommit(const std::string &gitRepoPath, const std::string &author);
int gitAddCommitDir(const std::string &gitRepoPath, const std::string &author);
std::string gitStoreFile(const std::string &gitRepoPath, const char *data, size_t len);
int gitLoadFile(const std::string &gitRepoPath, const std::string &gitRef, const std::string &filepath, std::string &data);

#endif // _gitTools_h
