#include <iostream>
#include <string>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <termios.h>
  #include <unistd.h>
  #include <stdio.h>
#endif

#include "console.h"

#ifdef _WIN32
int getch()
{
	DWORD con_mode;
	DWORD dwRead;

	HANDLE hIn=GetStdHandle(STD_INPUT_HANDLE);
	GetConsoleMode(hIn, &con_mode);
	SetConsoleMode(hIn, con_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));

    unsigned char c = 0;

    BOOL r = ReadConsoleA( hIn, &c, 1, &dwRead, NULL);

	if (!r) {
		printf("getch error: %d\n", GetLastError());
		exit(1);
	}
    return c;
}
#else
int getch() {
    int c;
    struct termios t_old, t_new;

    tcgetattr(STDIN_FILENO, &t_old);
    t_new = t_old;
    t_new.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t_new);

    c = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &t_old);
    return c;
}
#endif

/** Use getString with hide = true for passwords
  */
std::string getString(const char *prompt, bool hide)
{

#ifdef _WIN32
	const char BACKSPACE=8;
	const char RETURN=13;
#else  
	// Linux
	const char BACKSPACE=127;
	const char RETURN=10;
#endif

    std::string result;
	unsigned char c = 0;

    std::cout << prompt;

	while ( (c = getch()) != RETURN) {
		if (c == BACKSPACE) {
            if (result.size() > 0) {
                std::cout << "\b \b";
                result.resize(result.size()-1);
			}
		} else {
            result += c;
            if (hide) std::cout << '*';
            else std::cout << c;
		}
	}
	std::cout << std::endl;
    return result;
}

