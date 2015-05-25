#!/bin/sh

# test smit pull
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
    $SMIT project -c $REPO/$PROJECT1
    # add custom property
    $SMIT project $REPO/$PROJECT1 addProperty "color select blue green yellow"
    $SMIT project $REPO/$PROJECT1 addProperty "freeText text"

    $SMIT user $USER1 --passwd $PASSWD1 --project $PROJECT1:rw -d $REPO

    # create some entries
    # create issue 1
    $SMIT issue $REPO/$PROJECT1 -a - "summary=first issue" color=blue
    $SMIT issue $REPO/$PROJECT1 -a 1 status=open color=green
    # create issue 2
    $SMIT issue $REPO/$PROJECT1 -a - "summary=second issue" color=yellow
    mkdir $REPO/$PROJECT1/.smip/objects/00
    echo "-- file1" > $REPO/$PROJECT1/.smip/objects/00/file1
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

# add an entry server-side
$SMIT issue $REPO/$PROJECT1 -a 1 color=yellow freeText="hello world"
# add an issue
$SMIT issue $REPO/$PROJECT1 -a - summary="t---d issue" freeText="this is xyz"
$SMIT issue $REPO/$PROJECT1 -a 3 summary="third issue" freeText="this is xyz" "+file=00file2/file2.txt"

echo "-- file2" > $REPO/$PROJECT1/.smip/objects/00/file2
echo "-- contents of file2" >> $REPO/$PROJECT1/.smip/objects/00/file2

startServer
cd clone1
../$SMIT pull --user $USER1 --passwd $PASSWD1
cd -
stopServer

# check that the new entry and the new issue are pulled
$SMIT issue clone1/$PROJECT1 1 -pm > $TEST_NAME.out
$SMIT issue clone1/$PROJECT1 2 -pm >> $TEST_NAME.out
$SMIT issue clone1/$PROJECT1 3 -pm >> $TEST_NAME.out
cat clone1/$PROJECT1/.smip/objects/00/file1 >> $TEST_NAME.out
cat clone1/$PROJECT1/.smip/objects/00/file2 >> $TEST_NAME.out

diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out 


