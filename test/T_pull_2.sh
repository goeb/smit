#!/bin/sh

# test smit pull with merge conflicts
TEST_NAME=`basename $0`
# remove suffix .sh
TEST_NAME=`echo $TEST_NAME | sed -e "s/\.sh//"`

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
    $SMIT issue $REPO/$PROJECT1 -a 2 status=open +message="add a file" +file="a_file.txt"
    echo file1_data_yy > $REPO/$PROJECT1/files/a_file.txt
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

cleanup
init
startServer

# check that clone of p1 has correct content
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone1

cd clone1
../$SMIT pull --resolve-conflict keep-local
cd -

stopServer

# server side: add entries
$SMIT issue $REPO/$PROJECT1 -a 1 freeText=dummyValueServer
# add an issue
$SMIT issue $REPO/$PROJECT1 -a - freeText=x1server summary="third issue"
$SMIT issue $REPO/$PROJECT1 -a 3 freeText=x2server

# local side: add conflicting entries
$SMIT issue clone1/$PROJECT1 -a 1 freeText=dummyValueLocal +message="local conflicting entry"
# another entry
$SMIT issue clone1/$PROJECT1 -a 1 freeText=newValueLocal

# local side: add conflicting issue
$SMIT issue clone1/$PROJECT1 -a - freeText=x1local summary="local conflicting issue"
$SMIT issue clone1/$PROJECT1 -a 3 freeText=x2local
$SMIT issue clone1/$PROJECT1 -a 3 freeText=x3local

startServer
cd clone1
../$SMIT pull --user $USER1 --passwd $PASSWD1 --resolve-conflict keep-local
cd -
stopServer

# check that the new entry and the new issue are pulled correctly
(
    $SMIT issue clone1/$PROJECT1
    $SMIT issue clone1/$PROJECT1 1 -ph
    $SMIT issue clone1/$PROJECT1 2 -ph
    $SMIT issue clone1/$PROJECT1 3 -ph
    $SMIT issue clone1/$PROJECT1 4 -ph
) | grep -v "^Date:" > $TEST_NAME.out

diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out

