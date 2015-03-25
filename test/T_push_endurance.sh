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
    echo "START runClone $1 $2"
    i=$1 # instance of the clone
    project=$2
    # initiate clone number i
    clone=clone_${i}
    rm -rf $clone
    if [ "$project" = "p1" ]; then
        user=$USER1
        passwd=$PASSWD1
    else
        user=$USER2
        passwd=$PASSWD2
    fi
    $SMIT clone http://127.0.0.1:$PORT --user $user --passwd $passwd $clone

    project=$clone/${project}
    # create some issues and push them
    for issue in $(seq 1 $N_ISSUES); do
        # add an issue
        echo "runClone[$i]: add an issue ($issue)"
        r=$($SMIT issue $project -a - summary="#$i.$issue.a" freeText="diver:${DIVERSIFICATION}_$$" +message="creation of issue: #$i.$issue.a")
        DIVERSIFICATION=$(expr $DIVERSIFICATION + 1)
        createdIssue=$(echo $r | sed -e "s;/.*;;")
        createdEntry=$(echo $r | sed -e "s;.*/;;")
        # add a message in this issue
        echo "runClone[$i]: add an entry to this issue ($issue)"
        $SMIT issue $project -a $createdIssue +message="some message #2"

        # add another issue "bis"
        echo "runClone[$i]: add an issue bis ($issue)"
        $SMIT issue $project -a - summary="#$i.$issue.b" freeText="diver:${DIVERSIFICATION}_$$" +message="creation of issue: #$i.$issue.b"
        DIVERSIFICATION=$(expr $DIVERSIFICATION + 1)

        # push these 2 issues (pull first)
        echo "runClone[$i]: pull ($issue)"
        $SMIT pull $clone
        echo "runClone[$i]: push ($issue)"
        $SMIT push $clone
        echo "push: $?"
        sleep 0.2
    done
    echo "END runClone $1 $2"
}
# cleanup out file
> $TEST_NAME.out
checkClone() {
    i=$1 # instance of the clone
    project=$2
    # initiate clone number i
    clone=clone_${i}
    echo "-- $clone --" >> $TEST_NAME.out
    $SMIT issue $clone/${project} >> $TEST_NAME.out
}

N_CLONES=2
DIVERSIFICATION=0
pids=""
for c in $(seq 1 $N_CLONES); do
    runClone $c p1
    pids="$pids $!"
done

# wait for all sub-processes
#for p in $pids; do
#    wait $p
#done

stopServer

for c in $(seq 1 $N_CLONES); do
    checkClone $c p1
done

diff $srcdir/$TEST_NAME.ref $TEST_NAME.out
