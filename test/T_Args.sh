#/bin/sh

# Unit Test of Args.cpp

. $srcdir/functions

initTest

rm -f $TEST_NAME.out

runtest1() {
	echo ">>> ./T_Args test1 $*" >> $TEST_NAME.out
	./T_Args test1 $* > $TEST_NAME.out.sdtout 2> $TEST_NAME.out.stderr
	cat $TEST_NAME.out.stderr $TEST_NAME.out.sdtout >> $TEST_NAME.out
	rm -f $TEST_NAME.out.stderr $TEST_NAME.out.sdtout
}

runtest1
runtest1 -v 
runtest1 --verbose
runtest1 -v --one
runtest1 -v --one aaa
runtest1 -2 bbb --one aaa
runtest1 --wrong-one
runtest1 -x
runtest1 a1 a2 a3
runtest1 a1 a2 a3 --one aaa
runtest1 a1 a2 a3 wrong4



diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out
