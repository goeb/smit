#include <iostream>
#include <stdio.h>
#include "subprocess.h"

int main(int argc, char **argv)
{

	char *const envp[] = { "X=foo", 0 };
	Subprocess *subp = Subprocess::launch(argv+1, envp, 0);
	std::string data;
	while(std::getline(std::cin, data)) {
		data += "\n";
		int n = subp->write(data);
		if (n < 0) fprintf(stderr, "subp->write error");
	}
	subp->closeFd(SUBP_STDIN);

	int err = subp->read(data);
	printf("recv(stdout): err=%d, data=\n%s\n", err, data.c_str());
	err = subp->read(data, SUBP_STDERR);
	printf("recv(stderr): err=%d, data=\n%s\n", err, data.c_str());

	int ret = subp->wait();
	printf("ret=%d\n", ret);

}
