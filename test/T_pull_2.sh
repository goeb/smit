#!/bin/sh

# test smit pull with conflicts

SMIT=../smit

set -e 

REPO=trepo
PROJECT1=p1
USER1=tuser1
PASSWD1=tpasswd1
PORT=8099

init() {
    mkdir $REPO
    $SMIT init $REPO
    $SMIT project -c $PROJECT1 -d $REPO
    $SMIT user $USER1 --passwd $PASSWD1 --project $PROJECT1:rw -d $REPO

    # create some entries
    mkdir $REPO/$PROJECT1/issues/1
    echo +parent null > $REPO/$PROJECT1/issues/1/entry1x1
    echo +parent entry1x1 > $REPO/$PROJECT1/issues/1/entry1x2
    mkdir $REPO/$PROJECT1/issues/2
    echo +parent null > $REPO/$PROJECT1/issues/2/entry2x1
    echo file1 > $REPO/$PROJECT1/files/file1
}
cleanup() {
    REPO=trepo # just to be sure before the rm -rf
    rm -rf $REPO
    rm -rf clone1 clone2
}

startServer() {
    $SMIT serve $REPO --listen-port $PORT &
    pid=$!
    sleep 1 # wait for the server to start
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
checkFile() {
    file="$1"
    [ -f $file ] || fail "missing file '$file'"
    echo "$file: ok"
}

cleanup
init
startServer

# check that clone of p1 has correct content
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone1

cd clone1
../$SMIT pull
cd -

stopServer

# server side: add entries
ENTRY=$REPO/$PROJECT1/issues/1/entry1x3server
echo +parent entry1x2 > $ENTRY
echo "dummyProp dummyValueServer" >> $ENTRY
# add an issue
mkdir $REPO/$PROJECT1/issues/3
echo +parent null > $REPO/$PROJECT1/issues/3/entry3x1
echo +parent entry3x1 > $REPO/$PROJECT1/issues/3/entry3x2

# client side: add entries
ENTRY=clone1/$PROJECT1/issues/1/entry1x3client
echo +parent entry1x2 > $ENTRY
echo "dummyProp dummyValueClient" >> $ENTRY

startServer
cd clone1
../$SMIT pull --user $USER1 --passwd $PASSWD1
cd -
stopServer

# check that the new entry and the new issue are pulled
cd clone1
echo TODO
#checkFile $PROJECT1/issues/1/entry1x3



