#ifndef _gitdb_h
#define _gitdb_h

#include <string>
#include <list>

#include "utils/pipe.h"

// SHA1 id of an empty tree
#define K_EMPTY_TREE "4b825dc642cb6eb9a060e54bf8d69288fbee4904"

typedef std::string ObjectId;

struct AttachedFileRef {
    ObjectId id;
    std::string filename;
    size_t size;
};

std::string gitdbStoreFile(const std::string &gitRepoPath, const char *data, size_t len);
int gitdbLsTree(const std::string &gitRepoPath, const std::string &treeid, std::list<AttachedFileRef> &files);

class GitIssueList {

public:
    int open(const std::string &gitRepoPath);
    std::string getNext();
    void close();

private:
    Pipe *p;

};


class GitIssue {

public:
    int open(const std::string &gitRepoPath, const std::string &issueId);
    std::string getNextEntry();
    void close();
    static std::string addCommit(const std::string &gitRepoPath, const std::string &issueId,
                                 const std::string &author, long ctime, const std::string &body, const std::list<AttachedFileRef> &files);

private:
    Pipe *p;
    std::string previousCommitLine;

};


#endif // _gitdb_h
