#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "subprocess.h"

void usage()
{
	printf("Usage: 1. T_subprocess --args\n"
	       "       3. T_subprocess --basic\n"
	       "       3. T_subprocess --close-std-fd\n"
	      );
	exit(1);
}

int test_args(int argc, char **argv)
{
	char *const envp[] = { "X=foo", 0 };
	Subprocess *subp = Subprocess::launch(argv, envp, 0);
	std::string data;
	while(std::getline(std::cin, data)) {
		data += "\n";
		int n = subp->write(data);
		if (n < 0) fprintf(stderr, "subp->write error");
	}
	subp->closeStdin();

	int err = subp->read(data);
	printf("stdout: err=%d, data=\n%s\n", err, data.c_str());
	err = subp->read(data, SUBP_STDERR);
	printf("stderr: err=%d, data=\n%s\n", err, data.c_str());

	int ret = subp->wait();
	printf("ret=%d\n", ret);

	delete subp;

	return 0;
}

#define PRINTF_41(...) do { \
	char buffer[1024]; \
	sprintf(buffer, __VA_ARGS__); \
	write(41, buffer, strlen(buffer)); } while (0)

int test_basic(bool close_std_fd)
{
	char *const argv[] = { "sh", "-c", "sed -e 's/^/line: /'", 0 };

	dup2(1, 41); // for PRINTF_41
	if (close_std_fd) {
		close(0);
		close(1);
		close(2);
	}
	Subprocess *subp = Subprocess::launch(argv, 0, 0);

	int n;
	n = subp->write("hello\n");
	if (n != 0) PRINTF_41("error in Subprocess::write n=%d\n", n);
	n = subp->write("world\n");
	if (n != 0) PRINTF_41("error in Subprocess::write n=%d\n", n);
	n = subp->write("goodbye.");
	if (n != 0) PRINTF_41("error in Subprocess::write n=%d\n", n);

	subp->closeStdin();

	int err;
	std::string data;
	err = subp->read(data);
	if (err) PRINTF_41("error in Subprocess::read\n");
	if (data != "hello\n") PRINTF_41("error in Subprocess::read: bad value %s\n", data.c_str());
	err = subp->read(data);
	if (err) PRINTF_41("error in Subprocess::read\n");
	if (data != "world\n") PRINTF_41("error in Subprocess::read: bad value %s\n", data.c_str());
	err = subp->read(data);
	if (err) PRINTF_41("error in Subprocess::read\n");
	if (data != "goodbye.") PRINTF_41("error in Subprocess::read: bad value %s\n", data.c_str());

	int ret = subp->wait();
	PRINTF_41("ret=%d\n", ret);

	delete subp;

	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) usage();

	if (0 == strcmp(argv[1], "--args")) return test_args(argc-2, argv+2);
	if (0 == strcmp(argv[1], "--basic")) return test_basic(false);
	if (0 == strcmp(argv[1], "--close-std-fd")) return test_basic(true);

	usage();
}
