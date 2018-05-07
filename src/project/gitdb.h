#ifndef _gitdb_h
#define _gitdb_h

#include <string>
#include <list>

#include "utils/subprocess.h"

// SHA1 id of an empty tree
#define K_EMPTY_TREE "4b825dc642cb6eb9a060e54bf8d69288fbee4904"
#define BRANCH_PREFIX_ISSUES "issues/"
#define BRANCH_ISSUES_SHORT_NAMES "issues_short_names"
#define TABLE_ISSUES_SHORT_NAMES "issues.txt"

typedef std::string ObjectId;

struct AttachedFileRef {
    ObjectId id;
    std::string filename;
    size_t size;
};

std::string gitdbStoreFile(const std::string &gitRepoPath, const char *data, size_t len);
int gitdbLsTree(const std::string &gitRepoPath, const std::string &treeid, std::list<AttachedFileRef> &files);
int gitdbSetNotes(const std::string &gitRepoPath, const ObjectId &entryId, const std::string &data);

class GitIssueList {

public:
    int open(const std::string &gitRepoPath, const std::string &remote="");
    std::string getNext();
    void close();

private:
    Subprocess *subp;
    std::string remoteName;
};


class GitIssue {

public:
    int open(const std::string &gitRepoPath, const std::string &issueId);
    std::string getNextEntry();
    void close();
    static std::string addCommit(const std::string &bareGitRepo, const std::string &issueId,
                                 const std::string &author, long ctime, const std::string &body, const std::list<AttachedFileRef> &files);

private:
    Subprocess *subp;
    std::string previousCommitLine;

};

class GitObject {
public:
    GitObject(const std::string &gitRepoPath, const std::string &objectid);
    int getSize();
    int open();
    int read(char *buffer, size_t size);
    void close();
private:
    std::string path; // path of the git repository
    std::string id;
    Subprocess *subp;
};

#endif // _gitdb_h
