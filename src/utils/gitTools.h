#ifndef _gitTools_h
#define _gitTools_h

#include <string>

enum GitRefType {
    GIT_REF_LOCAL,
    GIT_REF_REMOTE
};

int gitdbCommitMaster(const std::string &gitRepoPath, const std::string &subpath,
                      const std::string &data, const std::string &author);
int gitInit(const std::string &gitRepoPath);
int gitAdd(const std::string &gitRepoPath, const std::string &subpath);
int gitCommit(const std::string &gitRepoPath, const std::string &author);
int gitAddCommitDir(const std::string &gitRepoPath, const std::string &author);
int gitGetBranchRef(const std::string &gitRepo, std::string branchName, GitRefType type, std::string &gitRef);
std::string gitGetFirstCommit(const std::string &gitRepo, const std::string &ref);

#endif // _gitTools_h
