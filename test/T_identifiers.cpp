
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "utest.h"
#include "identifiers.h"

#define SIZE 20

void usage() {
    printf("usage:\n"
           "    T_identifiers  [[--skip-io] \"ABCDEF\"]\n"
           "    return the number 0xABCDEF in base 34.\n"
           "The length of the argument must be at most 40 characters.\n"
           "\n"
           "If no argument is given, then the unit test is run.\n"
           );
    exit(1);
}

void hex2bin(const char* arg, uint8_t *buffer, int bufSize)
{
    memset(buffer, 0, bufSize);
    int L = strlen(arg);

    int j = 0; // index in the buffer
    for (int i=0; i<L; i++) {
        char nibble = 0;
        char c = arg[i];
        if ( (c>='0' && c <= '9') ) nibble = c-'0';
        else if ( (c>='a' && c <= 'f') ) nibble = c-'a'+10;
        else if ( (c>='A' && c <= 'F') ) nibble = c-'A'+10;
        else printf("Warning: unexpected character '%c'. Assuming zero.\n", c);

        if (j>=bufSize) {
            printf("Error: j > bufSize\n");
            return;
        }
        if (i%2 == 0) buffer[j] += nibble;
        else {
            buffer[j] += (nibble << 4);
            j++;
        }
    }

}

void test(const char *base16hex, const char* base34hexExpected)
{
    // least significant nibbles first
    uint8_t base16[SIZE];
    hex2bin(base16hex, base16, SIZE);

    std::string result = convert2base34(base16, SIZE);

    ASSERT(0 == result.compare(0, strlen(base34hexExpected), base34hexExpected));
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        // run unit test
        // least significant nibbles first

        test("200000000000000000000000000000000000001", "knupchtoLpmkL9bmwxemowhgsg652m00");
        test("1", "1");
        test("22", "01");
        test("32", "11");
        test("001", "i7");
        test("00001", "inm1");
        test("000000000000000000000000000000000000001", "inupchtoLpmkL9bmwxemowhgsg652m00");
        test("000000000000000000000000000000000000002", "2drhp0pf9hb79jmavxtafv1xmxca4a10");
        test("00000000000000000000000000000000000000FF", "0g69oabhx9m029LgvngaweowL9ov4gt4");
        test("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", "h5313s68L1bLniw4unvwmd8fgqu274u4");
        test("21564AFEDCB53465121", "2cmwsjaL42ciqv1");


        utestEnd();

    } else if (argc >= 2) {
        // run command line tool
        int i = 1;
        bool skip_io = false;
        char *hexa;
        while (i < argc) {
            char * arg = argv[i];

            if (0 == strcmp("--skip-io", arg)) {
                skip_io = true;
            } else {
                hexa = arg;
            }
            i++;
        }

        // convert to octet string
        int L = strlen(argv[1]);
        if (L > SIZE*2) {
            printf("Error: size %d\n", L);
            usage();
        }

        uint8_t buffer[SIZE];
        hex2bin(hexa, buffer, SIZE);

        std::string result = convert2base34(buffer, SIZE);
        for (int i=0; i<result.size(); i++) printf("%c", result[i]);
        printf("\n");
    } else usage();


}
