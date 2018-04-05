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
    $SMIT project -c $REPO/$PROJECT1
    $SMIT project -c $REPO/$PROJECT2
    $SMIT user $USER1 --passwd $PASSWD1 --project $PROJECT1:rw -d $REPO
    $SMIT user $USER2 --passwd $PASSWD2 --project $PROJECT2:rw -d $REPO

    # create some entries
    # create issue 1
	tmpDir=$(mktemp -d)
	echo file1_abc > "$tmpDir/file1"
	echo file2_0123 > "$tmpDir/file2"
    $SMIT issue $REPO/$PROJECT1 -a - "summary=p1: first issue"
    $SMIT issue $REPO/$PROJECT1 -a 1 status=open --file "$tmpDir/file1"
    # create issue 2
    $SMIT issue $REPO/$PROJECT2 -a - "summary=p2: second issue" color=yellow --file "$tmpDir/file2"
    # issue 3
    $SMIT issue $REPO/$PROJECT1 -a - "summary=p1: third issue"
	rm -rf "$tmpDir"
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

# $1 : project path
checkProject() {
	pname="$1"
	if [ ! -d "$pname" ]; then
		echo "$pname: no such directory" >> $TEST_NAME.out
		return
	fi
	# check the attached files
	echo "$pname/issues:" >> $TEST_NAME.out
	$SMIT issue $pname >> $TEST_NAME.out
	echo "$pname/issue #1:" >> $TEST_NAME.out
	$SMIT issue $pname 1 --history | grep -v "^Date:" >> $TEST_NAME.out
	file1Id=$($SMIT issue $pname 1 --history | grep file1 | awk '{print $1;}')
	if [ -n "$file1Id" ]; then
		echo "$pname/file1:" >> $TEST_NAME.out
		git -C $pname cat-file -p $file1Id >> $TEST_NAME.out
	fi
	file2Id=$($SMIT issue $pname 1 --history | grep file2 | awk '{print $1;}')
	if [ -n "$file2Id" ]; then
		echo "$pname/file2:" >> $TEST_NAME.out
		git -C $pname cat-file -p $file2Id >> $TEST_NAME.out
	fi
}

cleanup
init
startServer

# do clone1
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone1

echo "clone1" >> $TEST_NAME.out
$SMIT project clone1 -a >> $TEST_NAME.out

checkProject clone1/$PROJECT1
checkProject clone1/$PROJECT2


# do clone2
$SMIT clone http://127.0.0.1:$PORT --user $USER2 --passwd $PASSWD2 clone2

echo "clone2" >> $TEST_NAME.out
$SMIT project clone2 -a >> $TEST_NAME.out

checkProject clone2/$PROJECT1
checkProject clone2/$PROJECT2

# check that user 1 with password of user 2 raises an error
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD2 clone3 && fail "unexpected clone with wrong password"

stopServer

diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out
