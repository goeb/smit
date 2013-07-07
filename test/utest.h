
#ifndef _utest_h
#define _utest_h

#include <stdlib.h>


int nErrors = 0;
int nCheckpoints = 0;

#define ASSERT(_x)  { nCheckpoints++; if (!(_x)) { \
                        fprintf(stderr, "%s:%d: ASSERT error\n", __FILE__, __LINE__); \
                        nErrors++; \
                    } }

void utestEnd() {
    if (nErrors > 0) {
       fprintf(stderr, "%d error(s)/%d checkpoints\n", nErrors, nCheckpoints);
       exit(1);
    } else {
       fprintf(stderr, "OK (%d checkpoints)\n", nCheckpoints);
       exit(0);
    }

} 

#endif
