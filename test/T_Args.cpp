#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "Args.h"

#define FAIL(_format,...) printf(_format, __VA_ARGS__); exit(1);

void usage()
{
	printf("Usage:\n");
	printf("    t_Args <test-num> ... \n");
	printf("\n");
	printf("Example:\n");
	printf("    t_Args test1 -v --one xyz1 -2 xyz2\n");
	printf("    t_Args test1 -2 xyz2\n");
	printf("    t_Args test1 a1 a2 a3\n");
	printf("    t_Args test1 --wrong-one\n");
	printf("    t_Args test1 a1 a2 a3 wrong4\n");
	printf("\n");

	exit(1);
}

int test1(int argc, char **argv) {
	Args *args = new Args();
	args->setDescription("test1: Some description");
	args->setUsage("test1: usage string ...");
	args->setOpt("verbose", 'v', "be verbose", 0);
	args->setOpt("one", 0, "option 1", 1);
	args->setOpt(NULL, '2', "option 2", 1);
	args->setNonOptionLimit(3);

	args->parse(argc, argv);

	const char *x;
   	x = args->get("verbose");
	if (x) printf("verbose: %s\n", x);

   	x = args->get("v");
	if (x) printf("v: %s\n", x);

	x = args->get("one");
	if (x) printf("one: %s\n", x);

	x = args->get("2");
	if (x) printf("2: %s\n", x);

	while (x = args->pop()) {
		printf("non-option ARGV: %s\n", x);
	}
	return 0;
}

int main(int argc, char **argv) {

	if (argc <= 1) usage();

	// Only one test implemented at the moment
	if (0 != strcmp(argv[1], "test1")) usage();

	return test1(argc-2, argv+2);
}
