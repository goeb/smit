#!/bin/sh

# test smit pull with merge conflicts
# (local issue renamed locally)


set -e 

SMIT=../smit
. $srcdir/functions

initTest
rm -f $TEST_NAME.out

CLONE=cloneX

init() {
	initEmptyRepo

    # create some entries
    # create issue 1
    $SMIT issue $REPO/$PROJECT1 -a - "summary=first issue" freeText="creation of issue1"
    $SMIT issue $REPO/$PROJECT1 -a 1 status=open +message="some text...."

    # create issue 2
    $SMIT issue $REPO/$PROJECT1 -a - "summary=second issue" freeText="creation of issue2"
    $SMIT issue $REPO/$PROJECT1 -a 2 status=open +message="some text (issue2)...."

	tmpDir=$(mktemp -d)
	echo xxx_a_file_txt > "$tmpDir/a_file.txt"
    $SMIT issue $REPO/$PROJECT1 -a 2 status=open +message="add a file" --file "$tmpDir/a_file.txt"
	rm -rf "$tmpDir"
}
cleanup() {
    REPO=trepo # just to be sure before the rm -rf
    rm -rf $REPO
    rm -rf $CLONE
}


cleanup
init
startServer

# do clone
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 $CLONE

stopServer

# server side: add entries
$SMIT issue $REPO/$PROJECT1 -a 1 freeText=dummyValueServer
# add an issue
$SMIT issue $REPO/$PROJECT1 -a - freeText=x1server summary="third issue"
$SMIT issue $REPO/$PROJECT1 -a 3 freeText=x2server

# local side: add conflicting entries
$SMIT issue $CLONE/$PROJECT1 -a 1 freeText=dummyValueLocal +message="local conflicting entry"
# another entry
$SMIT issue $CLONE/$PROJECT1 -a 1 freeText=newValueLocal

# local side: add conflicting issue
$SMIT issue $CLONE/$PROJECT1 -a - freeText=x1local summary="local conflicting issue"
$SMIT issue $CLONE/$PROJECT1 -a 3 freeText=x2local
$SMIT issue $CLONE/$PROJECT1 -a 3 freeText=x3local

startServer
cd $CLONE
../$SMIT pull --user $USER1 --passwd $PASSWD1
cd -
stopServer

# check that the new entry and the new issue are pulled correctly
t_runcmd $SMIT issue $CLONE/$PROJECT1
t_runcmd $SMIT issue $CLONE/$PROJECT1 1 --history --properties
t_runcmd $SMIT issue $CLONE/$PROJECT1 2 --history --properties
t_runcmd $SMIT issue $CLONE/$PROJECT1 3 --history --properties
t_runcmd $SMIT issue $CLONE/$PROJECT1 3.0 --history --properties

# filter out dates
sed -e "/^Date:/d" $TEST_NAME.out > $TEST_NAME.out.fil

diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out.fil

