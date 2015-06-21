#!/bin/sh

# test smit pull with no merge conflict

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
    $SMIT project -c $REPO/$PROJECT1
    $SMIT project $REPO/$PROJECT1 addProperty "freeText text"
    $SMIT project $REPO/$PROJECT1 addProperty "manager text"

    $SMIT user $USER1 --passwd $PASSWD1 --project $PROJECT1:rw -d $REPO

    # create issue 1
    $SMIT issue $REPO/$PROJECT1 -a - "summary=first issue" freeText="creation of issue1"
    $SMIT issue $REPO/$PROJECT1 -a 1 status=open +message="some text...."
    tobeAmended=$($SMIT issue $REPO/$PROJECT1 -a 1 freeText=textServer0 +message="message server 0" | sed -e "s/.*Entry *//")
    echo tobeAmended=$tobeAmended

}
cleanup() {
    rm -rf $REPO $CLONE
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

cleanup
init
startServer

# clone the repo
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 $CLONE

stopServer

# server side: add entries
$SMIT issue $REPO/$PROJECT1 -a 1 freeText=textServer1 +message="message server 1"
$SMIT issue $REPO/$PROJECT1 -a 1 freeText=textServer2 +message="message server 2"
$SMIT issue $REPO/$PROJECT1 -a 1 +amend=$tobeAmended +message="amendment of msg server 2"

# local side: add NON-conflicting entries
$SMIT issue $CLONE/$PROJECT1 -a 1 manager="john smith" +message="local msg 1"
$SMIT issue $CLONE/$PROJECT1 -a 1 manager="hector" +message="local msg 2"
$SMIT issue $CLONE/$PROJECT1 -a 1 +message="local msg 3"

startServer
cd $CLONE
# normally no interactive prompt raised
../$SMIT pull --user $USER1 --passwd $PASSWD1
cd -
stopServer

# check that the new entry and the new issue are pulled correctly
(
    $SMIT issue $CLONE/$PROJECT1 1 -ph
) | grep -v "^Date:" > $TEST_NAME.out

diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out

