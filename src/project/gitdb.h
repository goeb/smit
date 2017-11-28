#ifndef _gitdb_h
#define _gitdb_h

#include <string>
#include <list>

#include "utils/pipe.h"

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
                                 const std::string &author, long ctime, const std::string &body, const std::list<std::string> &files);

private:
    Pipe *p;
    std::string previousCommitLine;

};


#endif // _gitdb_h
