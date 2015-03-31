#!/bin/sh

# test smit pull with merge conflicts
TEST_NAME=`basename $0`
# remove suffix .sh
TEST_NAME=`echo $TEST_NAME | sed -e "s/\.sh//"`

exec > $TEST_NAME.log 2>&1

SMIT=../smit

set -e 

REPO=trepo
PROJECT1=p1
USER1=tuser1
PASSWD1=tpasswd1
PORT=8099
CLONE=cloneX

init() {
    mkdir $REPO
    $SMIT init $REPO
    $SMIT project -c $PROJECT1 -d $REPO
    $SMIT user $USER1 --passwd $PASSWD1 --project $PROJECT1:rw -d $REPO
    # add custom property
    chmod u+w $REPO/$PROJECT1/project
    echo "addProperty freeText text" >> $REPO/$PROJECT1/project

    # create some entries
    # create issue 1
    $SMIT issue $REPO/$PROJECT1 -a - "summary=first issue" freeText="creation of issue1"
    $SMIT issue $REPO/$PROJECT1 -a 1 status=open +message="some text...."

    # create issue 2
    $SMIT issue $REPO/$PROJECT1 -a - "summary=second issue" freeText="creation of issue2"
    $SMIT issue $REPO/$PROJECT1 -a 2 status=open +message="some text (issue2)...."
    $SMIT issue $REPO/$PROJECT1 -a 2 status=open +message="add a file" +file="0123/a_file.txt"
    mkdir $REPO/$PROJECT1/objects/01
    echo file1_data_yy > $REPO/$PROJECT1/objects/01/23
}
cleanup() {
    REPO=trepo # just to be sure before the rm -rf
    rm -rf $REPO
    rm -rf $CLONE clone2
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

# do clone
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 $CLONE

cd $CLONE
../$SMIT pull --resolve-conflict keep-local
cd -

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
../$SMIT pull --user $USER1 --passwd $PASSWD1 --resolve-conflict keep-local
cd -
stopServer

# check that the new entry and the new issue are pulled correctly
(
    $SMIT issue $CLONE/$PROJECT1
    $SMIT issue $CLONE/$PROJECT1 1 -ph
    $SMIT issue $CLONE/$PROJECT1 2 -ph
    $SMIT issue $CLONE/$PROJECT1 3 -ph
    $SMIT issue $CLONE/$PROJECT1 4 -ph
) | grep -v "^Date:" > $TEST_NAME.out

diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out

