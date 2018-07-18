#ifndef _gitTools_h
#define _gitTools_h

#include <string>

#define GIT_OPT_FORCE 0x1

int gitdbCommitMaster(const std::string &gitRepoPath, const std::string &subpath,
                      const std::string &data, const std::string &author);
int gitInit(const std::string &gitRepoPath);
int gitAdd(const std::string &gitRepoPath, const std::string &subpath);
int gitCommit(const std::string &gitRepoPath, const std::string &author);
int gitAddCommitDir(const std::string &gitRepoPath, const std::string &author);
int gitShowRef(const std::string &gitRepo, std::string branchName, std::string &gitRef);
std::string gitGetFirstCommit(const std::string &gitRepo, const std::string &ref);
int gitRenameBranch(const std::string &gitRepo, const std::string &oldName, const std::string &newName);
std::string gitGetLocalBranchThatContains(const std::string &gitRepo, const std::string &gitRef);
std::string gitMergeBase(const std::string &gitRepo, const std::string &branch1, const std::string &branch2);
int gitUpdateRef(const std::string &gitRepo, const std::string &gitRef, const std::string &newValue);
int gitRevListReverse(const std::string &gitRepo, const std::string &base, const std::string &branch, std::string &out);
int gitBranchCreate(const std::string &gitRepo, const std::string &branch, const std::string &startPoint, int options=0);
int gitBranchRemove(const std::string &gitRepo, const std::string &branch, int options=0);
int gitWorktreeAdd(const std::string &gitRepo, const std::string &pathWorkingTree, const std::string &branch);
int gitWorktreeRemove(const std::string &gitRepo, const std::string &pathWorkingTree);

#endif // _gitTools_h
