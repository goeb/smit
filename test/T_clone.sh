#!/bin/sh

# Verify that a clone only contains the allowed projects

TEST_NAME=`basename $0`
# remove suffix .sh
TEST_NAME=`echo $TEST_NAME | sed -e "s/\.sh//"`
exec > $TEST_NAME.log 2>&1

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

    # create some files
    mkdir $REPO/$PROJECT1/issues/abc1
    echo +parent null > $REPO/$PROJECT1/issues/abc1/abc1
    mkdir $REPO/$PROJECT2/issues/abc2
    echo +parent null > $REPO/$PROJECT2/issues/abc2/abc2
    echo file1 > $REPO/$PROJECT1/files/file1
    echo file2 > $REPO/$PROJECT2/files/file2
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

# check that clone of p1 has correct content
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone1
echo $?
[ -f clone1/$PROJECT1/issues/abc1/abc1 ] || fail "missing file 'clone1/$PROJECT1/issues/abc1/abc1'"
[ -f clone1/$PROJECT1/files/file1 ] || fail "missing file 'clone1/$PROJECT1/files/file1'"
[ -f clone1/$PROJECT2/issues/abc2/abc2 ] && fail "unexpected file 'clone1/$PROJECT2/issues/abc2/abc2'"
[ -f clone1/$PROJECT2/files/file2 ] && fail "unexpected file 'clone1/$PROJECT2/files/file2'"

# check that clone of p2 has correct content
$SMIT clone http://127.0.0.1:$PORT --user $USER2 --passwd $PASSWD2 clone2
[ -f clone2/$PROJECT1/issues/abc1/abc1 ] && fail "unexpected file 'clone2/$PROJECT1/issues/abc1/abc1'"
[ -f clone2/$PROJECT1/files/file1 ] && fail "unexpected file 'clone2/$PROJECT1/files/file1'"
[ -f clone2/$PROJECT2/issues/abc2/abc2 ] || fail "missing file 'clone2/$PROJECT2/issues/abc2/abc2'"
[ -f clone2/$PROJECT2/files/file2 ] || fail "missing file 'clone2/$PROJECT2/files/file2'"

# check that user 1 with password of user 2 raises an error
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD2 clone2 && fail "unexpected clone with wrong password"

stopServer

