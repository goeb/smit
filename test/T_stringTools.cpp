
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "utest.h"
#include "stringTools.h"


int main(int argc, char **argv)
{
    // test getBasename
    std::string out;
    out = getBasename("");
    ASSERT(out == "");

    out = getBasename("/");
    ASSERT(out == "");

    out = getBasename(".");
    ASSERT(out == ".");

    out = getBasename("a/b/c");
    ASSERT(out == "c");

    out = getBasename("a/b/c/");
    ASSERT(out == "c");

    out = getBasename("a/b/cdef/");
    ASSERT(out == "cdef");

    out = getBasename("a/bcdef");
    ASSERT(out == "bcdef");

    utestEnd();
}
