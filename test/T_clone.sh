#!/bin/sh

# Verify that a clone only contains the allowed projects

TEST_NAME=`basename $0`
# remove suffix .sh
TEST_NAME=`echo $TEST_NAME | sed -e "s/\.sh//"`
exec > $TEST_NAME.log 2>&1
rm $TEST_NAME.out

SMIT=../smit

set -e 

REPO=trepo
PROJECT1=p1
PROJECT2=p2
USER1=tuser1
PASSWD1=tpasswd1
USER2=tuser2
PASSWD2=tpasswd2
PORT=8099

init() {
    REPO=trepo # just to be sure before the rm -rf
    mkdir $REPO
    $SMIT init $REPO
    $SMIT project -c $PROJECT1 -d $REPO
    $SMIT project -c $PROJECT2 -d $REPO
    $SMIT user $USER1 --passwd $PASSWD1 --project $PROJECT1:rw -d $REPO
    $SMIT user $USER2 --passwd $PASSWD2 --project $PROJECT2:rw -d $REPO

    # create some entries
    # create issue 1
    $SMIT issue $REPO/$PROJECT1 -a - "summary=p1: first issue"
    $SMIT issue $REPO/$PROJECT1 -a 1 status=open
    # create issue 2
    $SMIT issue $REPO/$PROJECT2 -a - "summary=p2: second issue" color=yellow
    mkdir $REPO/$PROJECT1/objects/00
    echo file1 > $REPO/$PROJECT1/objects/00/file1
    mkdir $REPO/$PROJECT2/objects/00
    echo file2 > $REPO/$PROJECT2/objects/00/file2
    # issue 3
    $SMIT issue $REPO/$PROJECT1 -a - "summary=p1: third issue"
}
cleanup() {
    rm -rf $REPO
    rm -rf clone1 clone2
}

startServer() {
    $SMIT serve $REPO --listen-port $PORT &
    pid=$!
    sleep 0.25 # wait for the server to start
}
stopServer() {
    echo killing pid=$pid
    kill $pid
}

fail() {
    echo "ERROR: $1"
    [ -n "$pid" ] && kill $pid
    exit 1
}

cleanup
init
startServer

# do clone1
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone1
echo $?
# check that clone of p1 has the files
echo "Clone of p1" >> $TEST_NAME.out
$SMIT project -d clone1 >> $TEST_NAME.out
[ -f clone1/$PROJECT1/objects/00/file1 ] || fail "missing file 'clone1/$PROJECT1/files/file1'"
[ -f clone1/$PROJECT2/objects/00/file2 ] && fail "unexpected file 'clone1/$PROJECT2/files/file2'"

$SMIT issue clone1/$PROJECT1 >> $TEST_NAME.out
$SMIT issue clone1/$PROJECT1 1 -pm >> $TEST_NAME.out


# do clone2
$SMIT clone http://127.0.0.1:$PORT --user $USER2 --passwd $PASSWD2 clone2

# check that clone of p2 has the files
echo "Clone of p2" >> $TEST_NAME.out
$SMIT project -d clone2 >> $TEST_NAME.out
[ -f clone2/$PROJECT1/objects/00/file1 ] && fail "unexpected file 'clone2/$PROJECT1/files/file1'"
[ -f clone2/$PROJECT2/objects/00/file2 ] || fail "missing file 'clone2/$PROJECT2/files/file2'"

$SMIT issue clone2/$PROJECT2 >> $TEST_NAME.out
$SMIT issue clone2/$PROJECT2 1 -pm >> $TEST_NAME.out

# check that user 1 with password of user 2 raises an error
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD2 clone2 && fail "unexpected clone with wrong password"

stopServer

diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out
