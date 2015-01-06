#!/bin/sh

# test smit pull with merge conflicts

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
    not=0
    if [ "$1" = "-not" ]; then
        not=1
        shift
    fi
    file="$1"
    if [ "$not" = "1" ]; then
        [ -f $file ] && fail "missing file '$file'"
        echo "$file: ok (does not exist)"
    else
        [ -f $file ] || fail "missing file '$file'"
        echo "$file: ok"
    fi

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
ENTRY=$REPO/$PROJECT1/issues/1/entry1x3server
echo +parent entry1x2 > $ENTRY
echo "dummyProp dummyValueServer" >> $ENTRY
# add an issue
mkdir $REPO/$PROJECT1/issues/3
echo +parent null > $REPO/$PROJECT1/issues/3/entry3x1server
echo +parent entry3x1server > $REPO/$PROJECT1/issues/3/entry3x2server

# local side: add conflicting entries
ENTRY=clone1/$PROJECT1/issues/1/entry1x3local
echo +parent entry1x2 > $ENTRY
echo "dummyProp dummyValueLocal" >> $ENTRY
echo "otherProp otherValue" >> $ENTRY
# another entry
ENTRY=clone1/$PROJECT1/issues/1/entry1x4local
echo +parent entry1x3local  > $ENTRY
echo "dummyProp newValue" >> $ENTRY

# local side: add conflicting issue
mkdir clone1/$PROJECT1/issues/3
echo +parent null > clone1/$PROJECT1/issues/3/entry3x1local
echo +parent entry3x1local > clone1/$PROJECT1/issues/3/entry3x2local

startServer
cd clone1
../$SMIT pull --user $USER1 --passwd $PASSWD1 --resolve-conflict keep-local
cd -
stopServer

# check that the new entry and the new issue are pulled correctly
ISSUE=clone1/$PROJECT1/issues/1
checkFile $ISSUE/entry1x1
checkFile $ISSUE/entry1x2
checkFile $ISSUE/entry1x3server
checkFile -not $ISSUE/entry1x3local
checkFile -not $ISSUE/entry1x4local
n=`ls $ISSUE | wc -l`
[ $n -eq 5 ] || fail "[$ISSUE] n=$n"

ISSUE=clone1/$PROJECT1/issues/3
checkFile $ISSUE/entry3x1server
checkFile $ISSUE/entry3x2server
n=`ls $ISSUE | wc -l`
[ $n -eq 2 ] || fail "[clone1/$PROJECT1/issues/3] n=$n"

ISSUE=clone1/$PROJECT1/issues/4
checkFile $ISSUE/entry3x1local
checkFile $ISSUE/entry3x2local
n=`ls $ISSUE | wc -l`
[ $n -eq 2 ] || fail "[$ISSUE] n=$n"

#checkFile $PROJECT1/issues/1/entry1x3



