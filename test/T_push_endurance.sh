#!/bin/sh

set -x
# Test smit with many concurrent pullers/pushers

. $srcdir/functions

N_ISSUES=10

initTest
cleanRepo
initRepoGlobalNumbering
startServer

runClone() {
    echo "runClone $1 $2"
    i=$1 # instance of the clone
    projectNum=$2 # 1 => p1, or 2 => p2
    # initiate clone number i
    clone=clone_p${projectNum}_${i}
    rm -rf $clone
    if [ "$projectNum" = "1" ]; then
        user=$USER1
        passwd=$PASSWD1
    else
        user=$USER2
        passwd=$PASSWD2
    fi
    $SMIT clone http://127.0.0.1:$PORT --user $user --passwd $passwd $clone

    project=$clone/p${projectNum}
    # create some issues and push them
    for issue in $(seq 1 $N_ISSUES); do
        # add an issue
        r=$($SMIT issue $project -a - summary="issue #$issue in p${projectNum} [$i]" freeText="diver:${DIVERSIFICATION}_$$" +message="creation of issue: issue #$issue in p${projectNum}")
        DIVERSIFICATION=$(expr $DIVERSIFICATION + 1)
        createdIssue=$(echo $r | sed -e "s;/.*;;")
        createdEntry=$(echo $r | sed -e "s;.*/;;")
        # add a message in this issue
        $SMIT issue $project -a $createdIssue +message="some message #2"

        # add another issue "bis"
        $SMIT issue $project -a - summary="issue #$issue (bis) in p${projectNum} [$i]" freeText="diver:${DIVERSIFICATION}_$$" +message="creation of issue bis: issue #$issue (bis) in p${projectNum}"
        DIVERSIFICATION=$(expr $DIVERSIFICATION + 1)

        # push these 2 issues (pull first)
        $SMIT pull $clone
        $SMIT push $clone
    done
}
# cleanup out file
> $TEST_NAME.out
checkClone() {
    i=$1 # instance of the clone
    projectNum=$2 # 1 => p1, or 2 => p2
    # initiate clone number i
    clone=clone_p${projectNum}_${i}
    echo "-- $clone --" >> $TEST_NAME.out
    $SMIT issue $clone/p${projectNum} >> $TEST_NAME.out
}

N_CLONES=2
P=1
DIVERSIFICATION=0
for c in $(seq 1 $N_CLONES); do
    runClone $c $P
done

stopServer

for c in $(seq 1 $N_CLONES); do
    checkClone $c $P
done

diff $srcdir/$TEST_NAME.ref $TEST_NAME.out
