
#include <openssl/sha.h>
#include <string.h>


#include "identifiers.h"
#include "logging.h"


ustring bin2hex(const ustring & in)
{
    const char hexTable[] = { '0', '1', '2', '3',
                              '4', '5', '6', '7',
                              '8', '9', 'a', 'b',
                              'c', 'd', 'e', 'f' };
    ustring hexResult;
    size_t i;
    size_t L = in.size();
    for (i=0; i<L; i++) {
        int c = in[i];
        hexResult += hexTable[c >> 4];
        hexResult += hexTable[c & 0x0f];
    }
    return hexResult;
}


ustring computeIdBase34(uint8_t *buffer, size_t length)
{
    // compute sha1 and convert it to base34 visible string
    unsigned char md[SHA_DIGEST_LENGTH];
    SHA1(buffer, length, md);

    ustring sha1sum;
    sha1sum.assign(md, SHA_DIGEST_LENGTH);
    LOG_DEBUG("sha1=%s", (char*)bin2hex(sha1sum).c_str());

    return sha1sum; // TODO not visible string
}

// vector that for each of 256^0, 256^1, 256^2, ... 256^19
// gives the decomposition in base 34
// Python code that returns this array:
//def int2baseVector(x, base):
//    if x==0: return [0]
//    digits = []
//    while x:
//        digits.append(x % base)
//        x /= base
//    digits.reverse()
//    return digits
//
//L = []
//MAX_STRING_BASE_256 = 20
//for i in range(MAX_STRING_BASE_256):
//L.append(int2baseVector(pow(256, i), 34))
//
//maxSize = len(L[-1])+1 # +1 for the "retenue"
//
//for i in range(MAX_STRING_BASE_256):
//    while len(L[i]) < maxSize:
//        L[i].append(0);

const int BASE_34_VECTOR_SIZE = 32;
const int BASE_34 = 34;
// note that the least significant value is first
static const int BASE_256_TO_34_VECTOR[SHA_DIGEST_LENGTH+1][BASE_34_VECTOR_SIZE] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 23, 22, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 5, 29, 18, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 21, 19, 33, 17, 26, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 3, 28, 27, 14, 25, 31, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 19, 20, 17, 19, 17, 1, 21, 21, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 1, 31, 18, 32, 10, 14, 15, 26, 30, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 17, 25, 13, 4, 17, 28, 3, 6, 25, 27, 24, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 33, 3, 18, 5, 33, 20, 30, 0, 20, 29, 28, 6, 24, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 15, 0, 14, 22, 23, 31, 17, 26, 26, 26, 12, 19, 7, 30, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 31, 14, 17, 25, 4, 22, 0, 25, 21, 23, 9, 30, 33, 18, 1, 29, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 13, 13, 10, 3, 26, 23, 30, 12, 22, 33, 33, 0, 6, 1, 25, 23, 16, 21, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 29, 29, 8, 30, 16, 32, 4, 5, 17, 14, 31, 17, 13, 29, 16, 24, 24, 27, 25, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 11, 30, 28, 28, 5, 23, 10, 25, 4, 7, 21, 32, 28, 11, 33, 14, 4, 26, 12, 28, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 27, 14, 18, 7, 1, 16, 14, 20, 24, 25, 23, 21, 1, 8, 3, 27, 14, 25, 4, 22, 27, 31, 7, 0, 0, 0, 0, 0, 0, 0, 0},
    {18, 9, 17, 27, 26, 6, 25, 32, 26, 7, 23, 29, 12, 11, 20, 12, 0, 14, 17, 25, 23, 6, 18, 25, 25, 1, 0, 0, 0, 0, 0, 0},
    {18, 25, 3, 4, 29, 3, 25, 17, 0, 23, 30, 16, 31, 22, 3, 29, 26, 16, 3, 3, 28, 14, 0, 9, 30, 7, 13, 0, 0, 0, 0, 0},
    {18, 7, 8, 32, 8, 1, 3, 19, 31, 9, 33, 8, 5, 21, 22, 5, 7, 14, 10, 12, 17, 21, 9, 29, 29, 13, 21, 31, 2, 0, 0, 0},
    {18, 23, 30, 25, 12, 17, 29, 24, 21, 25, 22, 20, 21, 9, 11, 22, 32, 33, 14, 22, 24, 32, 17, 16, 28, 16, 6, 5, 2, 22, 0, 0},
    {18, 5, 3, 1, 3, 28, 6, 8, 21, 1, 11, 21, 23, 18, 32, 4, 30, 23, 31, 32, 22, 13, 8, 15, 16, 26, 30, 2, 7, 4, 30, 4},
};


void multiply(const int* vector256, int *currentVector, int size, int factor)
{
    // multiply vector256 by factor and add it to currentVector
    int i = 0;
    for (i=0; i<size; i++) {
        currentVector[i] += vector256[i]*factor;
        int j = i; // start a sceond level index
        while (currentVector[j] >= BASE_34) {
            int x = currentVector[j];

            currentVector[j] = x % BASE_34;

            if (j >= size-1) {
                // error, overflow
                LOG_ERROR("multiply overflow");
                break;
            } else {
                j++;
                currentVector[j] += (x/BASE_34);
            }
        }
    }
}

ustring convert2base34(const uint8_t *buffer, size_t length)
{
    // length must be SHA_DIGEST_LENGTH

    // buffer is a base-256 number, least significant value first
    int i = 0;
    int result[BASE_34_VECTOR_SIZE];
    memset(result, 0, sizeof(int)*BASE_34_VECTOR_SIZE);
    for (i=0; i<length; i++) {
        multiply(BASE_256_TO_34_VECTOR[i], result, BASE_34_VECTOR_SIZE, buffer[i]);
    }
    for (i=0; i<BASE_34_VECTOR_SIZE; i++) {
        printf("%u, ", result[i]);
    }
    printf("\n");

    const uint8_t alphabet[BASE_34+2] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                               'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
                               'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
                                          'U', 'V', 'W', 'X', 'Y', 'Z'};
    ustring base34result;
    bool skip_io = false; // for tests
    for (i=0; i<BASE_34_VECTOR_SIZE; i++) {
        uint8_t c = alphabet[result[i]];
        if (skip_io) {
            // skip letters 'I' and 'O' because they look like
            // 1 (one) and 0 (zero)
            if (c >= 'I') c = alphabet[result[i]+1];
            if (c >= 'O') c = alphabet[result[i]+2];
        }
        base34result.push_back(alphabet[result[i]]);
    }
    return base34result;
}
