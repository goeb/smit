
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <openssl/sha.h>

#include "utest.h"
#include "identifiers.h"

#define SIZE 20

void usage() {
    printf("usage:\n"
           "    T_identifiers \"ABCDEF\"\n"
           "    return the number 0xABCDEF in base 34.\n"
           "The length of the argument must be at most 40 characters.\n"
           );
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc != 2) usage();

    // get buffer from command line and convert to octet string
    int L = strlen(argv[1]);
    if (L > SHA_DIGEST_LENGTH*2) {
        printf("Error: size %d\n", L);
        usage();
    }
    if (L%2 != 0) {
        printf("Warning: size not event (%d)\n", L);
    }

    const char * arg = argv[1];

    uint8_t buffer[SIZE];
    memset(buffer, 0, SIZE);
    int j = 0; // index in the buffer
    char nibble = 0;
    for (int i=0; i<L; i++) {
        char c = arg[i];
        if ( (c>='0' && c <= '9') ) nibble = c-'0';
        else if ( (c>='a' && c <= 'f') ) nibble = c-'a';
        else if ( (c>='A' && c <= 'F') ) nibble = c-'A';
        else printf("Aarning: unexpected character '%c'. Assuming zero.\n", c);

        if (i%2 == 0) buffer[j] += nibble;
        else {
            buffer[j] += (nibble << 4);
            nibble = 0;
            j++;
        }
    }

    ustring result = convert2base34(buffer, SIZE);
    for (int i=0; i<result.size(); i++) printf("%c", result[i]);
    printf("\n");


#if 0
    const uint8_t BUF1[SIZE] = {1, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,};
    buffer = BUF1;
    ustring result = convert2base34(buffer, SIZE);
    for (int i=0; i<result.size(); i++) printf("%c", result[i]);
    printf("\n");

    const uint8_t BUF2[SIZE] = {0, 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,};
    buffer = BUF2;
    result = convert2base34(buffer, SIZE);
    for (int i=0; i<result.size(); i++) printf("%c", result[i]);
    printf("\n");


    const uint8_t BUF3[SIZE] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,};
    buffer = BUF3;
    result = convert2base34(buffer, SIZE);
    for (int i=0; i<result.size(); i++) printf("%c", result[i]);
    printf("\n");

    const uint8_t BUF4[SIZE] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,};
    buffer = BUF4;
    result = convert2base34(buffer, SIZE);
    for (int i=0; i<result.size(); i++) printf("%c", result[i]);
    printf("\n");
#endif

    utestEnd();
}
