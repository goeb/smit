
#ifndef _utest_h
#define _utest_h

#include <stdlib.h>
#include <sstream>

int nErrors = 0;
int nCheckpoints = 0;

#define ASSERT(_x) utAssert(_x, __FILE__, __LINE__)

void utAssert(bool condition, const char * file, int line)
{
    if (!condition) {
        fprintf(stderr, "Error: %s:%d", file, line);
        //std::ostringstream s;
        //s << "sed -n '" << line << "p' " << file;
        //system(s.str().c_str());
        nErrors++;
    }
    nCheckpoints++;
}

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
