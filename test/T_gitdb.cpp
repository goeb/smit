#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gitdb.h"

void usage()
{
	printf("usage: 1. T_gitdb issue [<id>]\n");
	printf("       2. T_gitdb commit\n");
	exit(1);
}

void listIssues()
{
	GitIssueList ilist;
	int ret = ilist.open(".");
	if (ret) {
		fprintf(stderr, "ilist.open error\n");
		exit(1);
	}
	while (1) {
		std::string id = ilist.getNext();
		if (id.empty()) break;
		printf("%s\n", id.c_str());
	}
	ilist.close();
}

void listEntries(const std::string &issueId)
{
	GitIssue elist;
	int ret = elist.open(".", issueId);
	if (ret) {
		fprintf(stderr, "elist.open error\n");
		exit(1);
	}
	while (1) {
		std::string entry = elist.getNextEntry();
		if (entry.empty()) break;
		printf("--------------------------------------------------------------------------------\n"
		       "%s\n", entry.c_str());
	}
	elist.close();
}

void test_commit(const char *text)
{
	std::list<std::string> files;
	const char *body = "hello world\ngoodbye.";
	if (text) body = text;
	std::string eid = GitIssue::addCommit(".", "1", "John", 1511992000, body, files);
	printf("eid=%s\n", eid.c_str());
}

int main(int argc, char **argv)
{
	if (argc < 2) usage();

	if (0 == strcmp("issue", argv[1])) {
		if (argc == 3) listEntries(argv[2]);
		else listIssues();
	} else if (0 == strcmp("commit", argv[1])) {
		if (argc > 2) test_commit(argv[2]);
		else test_commit(0);
	}
	else usage();

	return 0;
}
