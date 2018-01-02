#ifndef _gitTools_h
#define _gitTools_h

#include <string>

int gitdbCommitMaster(const std::string &gitRepoPath, const std::string &subpath,
                      const std::string &data, const std::string &author);
int gitInit(const std::string &gitRepoPath);

#endif // _gitTools_h
