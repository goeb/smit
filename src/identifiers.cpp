
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

const int BASE_34_VECTOR_SIZE = 31;
const int BASE_34 = 34;
// note that the least significant value is first
static const int BASE_256_TO_34_VECTOR[SHA_DIGEST_LENGTH][BASE_34_VECTOR_SIZE] =
{{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {7, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {1, 22, 23, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {12, 18, 29, 5, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {2, 26, 17, 33, 19, 21, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {20, 31, 25, 14, 27, 28, 3, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {4, 21, 21, 1, 17, 19, 17, 20, 19, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {1, 0, 30, 26, 15, 14, 10, 32, 18, 31, 1, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {7, 24, 27, 25, 6, 3, 28, 17, 4, 13, 25, 17, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {1, 24, 6, 28, 29, 20, 0, 30, 20, 33, 5, 18, 3, 33, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {12, 30, 7, 19, 12, 26, 26, 26, 17, 31, 23, 22, 14, 0, 15, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {2, 29, 1, 18, 33, 30, 9, 23, 21, 25, 0, 22, 4, 25, 17, 14, 31, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {21, 16, 23, 25, 1, 6, 0, 33, 33, 22, 12, 30, 23, 26, 3, 10, 13, 13, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {4, 25, 27, 24, 24, 16, 29, 13, 17, 31, 14, 17, 5, 4, 32, 16, 30, 8, 29, 29, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
 {1, 1, 28, 12, 26, 4, 14, 33, 11, 28, 32, 21, 7, 4, 25, 10, 23, 5, 28, 28, 30, 11, 18, 0, 0, 0, 0, 0, 0, 0, 0},
 {7, 31, 27, 22, 4, 25, 14, 27, 3, 8, 1, 21, 23, 25, 24, 20, 14, 16, 1, 7, 18, 14, 27, 18, 0, 0, 0, 0, 0, 0, 0},
 {1, 25, 25, 18, 6, 23, 25, 17, 14, 0, 12, 20, 11, 12, 29, 23, 7, 26, 32, 25, 6, 26, 27, 17, 9, 18, 0, 0, 0, 0, 0},
 {13, 7, 30, 9, 0, 14, 28, 3, 3, 16, 26, 29, 3, 22, 31, 16, 30, 23, 0, 17, 25, 3, 29, 4, 3, 25, 18, 0, 0, 0, 0},
 {2, 31, 21, 13, 29, 29, 9, 21, 17, 12, 10, 14, 7, 5, 22, 21, 5, 8, 33, 9, 31, 19, 3, 1, 8, 32, 8, 7, 18, 0, 0},
 {22, 2, 5, 6, 16, 28, 16, 17, 32, 24, 22, 14, 33, 32, 22, 11, 9, 21, 20, 22, 25, 21, 24, 29, 17, 12, 25, 30, 23, 18, 0}
};

void multiply(const int* vector256, int *currentVector, int size, int factor)
{
    // multiply vector256 by factor and add it to currentVector
    int i = 0;
    for (i=0; i<size; i++) {
        currentVector[i] += vector256[i]*factor;
        if (currentVector[i] >= BASE_34) {
            int x = currentVector[i];

            currentVector[i] = x % BASE_34;

            if (i >= size-1) {
                // error, overflow
                LOG_ERROR("multiply overflow");
            } else {
                currentVector[i+1] += (x/BASE_34);
            }
        }
    }
}

ustring convert2base34(uint8_t *buffer, size_t length)
{
    // length must be SHA_DIGEST_LENGTH

    // buffer is a base-256 number, least significant value first
    int i = 0;
    int result[BASE_34_VECTOR_SIZE];
    memset(result, 0, BASE_34_VECTOR_SIZE);
    for (i=0; i<length; i++) {
        multiply(BASE_256_TO_34_VECTOR[i], result, BASE_34_VECTOR_SIZE, buffer[i]);
    }

}
