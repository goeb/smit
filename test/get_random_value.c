#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc > 1) srand(atoi(argv[1]));
    else srand(time(0));
    int n = rand();
    n = n % 40 + 1;
    int i = 0;
    for (i=0; i<n; i++) {
        // get a random visible character
        int visibleChar = 0;
        while(!isprint(visibleChar) && visibleChar != '\n' && visibleChar != '\t' && visibleChar != '\r') {
            visibleChar = rand() % 0x7F;
        }
        printf("%c", visibleChar);
    }
    return 0;
}
