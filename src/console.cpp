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

	unsigned char ch = 0;

	BOOL r = ReadConsoleA( hIn, &ch, 1, &dwRead, NULL);

	if (!r) {
		printf("getch error: %d\n", GetLastError());
		exit(1);
	}
	return ch;
}
#else
int getch() {
    int ch;
    struct termios t_old, t_new;

    tcgetattr(STDIN_FILENO, &t_old);
    t_new = t_old;
    t_new.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t_new);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &t_old);
    return ch;
}
#endif
std::string getPasswd(const char *prompt, bool showAsterisk)
{

#ifdef _WIN32
	const char BACKSPACE=8;
	const char RETURN=13;
#else  
	// Linux
	const char BACKSPACE=127;
	const char RETURN=10;
#endif

	std::string password;
	unsigned char c = 0;

	std::cout << prompt << std::endl;

	while ( (c = getch()) != RETURN) {
		if (c == BACKSPACE) {
			if(password.size() != 0) {
				if(showAsterisk) std::cout << "\b \b"; // erase an asterisk
				password.resize(password.length()-1);
			}
		} else {
			password += c;
			if(showAsterisk) std::cout << '*';
		}
	}
	std::cout << std::endl;
	return password;
}

