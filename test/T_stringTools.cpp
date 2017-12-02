
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "utest.h"
#include "stringTools.h"

void test_popToken()
{
	//  default mode TOK_TRIM_BOTH
    std::string x = "//a//b/c";
    std::string y = popToken(x, '/');
    ASSERT(x == "b/c");
    ASSERT(y == "a");
	y = popToken(x, '/');
	ASSERT(x == "c");
	ASSERT(y == "b");
	y = popToken(x, '/');
	ASSERT(x == "");
	ASSERT(y == "c");
	y = popToken(x, '/');
	ASSERT(x == "");
	ASSERT(y == "");

	// TOK_TRIM_BEFORE
	x = "//a//b/c";
	y = popToken(x, '/', TOK_TRIM_BEFORE);
	ASSERT(x == "/b/c");
	ASSERT(y == "a");

	// TOK_STRICT
	x = "//a//b/c";
	y = popToken(x, '/', TOK_STRICT);
	ASSERT(x == "/a//b/c");
	ASSERT(y == "");
	y = popToken(x, '/', TOK_STRICT);
	ASSERT(x == "a//b/c");
	ASSERT(y == "");
	y = popToken(x, '/', TOK_STRICT);
	ASSERT(x == "/b/c");
	ASSERT(y == "a");
}

void test_basename()
{
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
}

int main(int argc, char **argv)
{
	test_basename();
	test_popToken();

    utestEnd();
}
