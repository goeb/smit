#!/bin/sh

# Test smit push with renumbering of issue by server
# This is achieved by having 2 projects on the server repo
# having a global numbering policy.

. $srcdir/functions

initTest
cleanRepo
initRepoGlobalNumbering
startServer

# do a clone
rm -rf clone1
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone1

# add an issue and push
$SMIT issue clone1/p1 -a - summary="an issue in p1"
$SMIT push clone1

# do another clone
rm -rf clone2
$SMIT clone http://127.0.0.1:$PORT --user $USER2 --passwd $PASSWD2 clone2

# add an issue and push
$SMIT issue clone2/p2 -a - summary="an issue in p2"
$SMIT push clone2

stopServer

$SMIT issue clone1/p1 -h | grep -v ^Date > $TEST_NAME.out
$SMIT issue clone2/p2 -h | grep -v ^Date >> $TEST_NAME.out

diff $srcdir/$TEST_NAME.ref $TEST_NAME.out
