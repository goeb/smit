#!/bin/sh

# test smit push in a simple nominal case:
# - local entry pushed

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
    $SMIT issue $REPO/$PROJECT1 -a - "summary=first issue"
    $SMIT issue $REPO/$PROJECT1 -a 1 status=open +message="some message"
    # create issue 2
    $SMIT issue $REPO/$PROJECT1 -a - "summary=second issue" status=open
}
cleanup() {
    REPO=trepo # just to be sure before the rm -rf
    rm -rf $REPO
    rm -rf clone1
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

# main --------------
cleanup
init
startServer

# do a clone
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone1

# add an issue in the clone
$SMIT issue clone1/$PROJECT1 -a - summary="issue of clone1" freeText="this is xyz"
$SMIT issue clone1/$PROJECT1 -a 3 summary="third issue" freeText="y1y2" "+file=5ab64de1bcfb3d5500b2a95145f44f74aa6ed72f/file2.txt"

mkdir clone1/$PROJECT1/objects/5a
echo "-- file2" > clone1/$PROJECT1/objects/5a/b64de1bcfb3d5500b2a95145f44f74aa6ed72f
echo "-- contents of file2" >> clone1/$PROJECT1/objects/5a/b64de1bcfb3d5500b2a95145f44f74aa6ed72f
# sha1 of this file is: 5ab64de1bcfb3d5500b2a95145f44f74aa6ed72f

$SMIT issue clone1/$PROJECT1 -a 2 summary="issue-2" freeText="free-text-issue2" 

cd clone1
../$SMIT push
cd -

# check that the new entries, issue, and file have been pushed
# do a clone2 and compare clone1 and clone2
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone2

stopServer

diff -ru clone1 clone2 > $TEST_NAME.out

$SMIT issue clone2/$PROJECT1 1 -pm >> $TEST_NAME.out
$SMIT issue clone2/$PROJECT1 2 -pm >> $TEST_NAME.out
$SMIT issue clone2/$PROJECT1 3 -pm >> $TEST_NAME.out

diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out 


