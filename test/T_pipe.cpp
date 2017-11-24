#include <string>
#include <stdio.h>

#include "pipe.h"

int main(int argc, char **argv)
{
	const char *cmd ="cat"; // default command
	if (argc >= 2) cmd = argv[1];

	Pipe *p = Pipe::open(cmd, "r");
	if (!p) {
		fprintf(stderr, "error, cannot open pipe\n");
		return 1;
	}

	while (1) {
		std::string line = p->getline();
		if (line.empty()) break;
		printf("line: %s\n", line.c_str());
	}

	Pipe::close(p);
	printf("p=%x\n", p);
	return 0;
}
