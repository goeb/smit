#!/bin/sh

#set -x
# Test smit with many concurrent pullers/pushers

. $srcdir/functions

N_ISSUES=30

initTest
cleanRepo
initRepoGlobalNumbering
startServer


runClone() {
    echo "START runClone $1 $2"
    local i=$1 # instance of the clone
    local project=$2
    # initiate clone number i
    local clone=clone_${i}
    set -x
    rm -rf $clone
    if [ "$project" = "p1" ]; then
        local user=$USER1
        local passwd=$PASSWD1
    else
        local user=$USER2
        local passwd=$PASSWD2
    fi
    echo "runClone[$i]: do clone"
    $SMIT clone http://127.0.0.1:$PORT --user $user --passwd $passwd $clone
    set +x

    projectPath=$clone/${project}
    # create some issues and push them
    for issue in $(seq 1 $N_ISSUES); do
        # add an issue
        echo "runClone[$i]: add an issue ($issue)"
        r=$($SMIT issue $projectPath -a - summary="#$i.$issue.a" freeText="diver:${DIVERSIFICATION}_$$" +message="creation of issue: #$i.$issue.a")
        DIVERSIFICATION=$(expr $DIVERSIFICATION + 1)
        createdIssue=$(echo $r | sed -e "s;/.*;;")
        createdEntry=$(echo $r | sed -e "s;.*/;;")
        # add a message in this issue
        echo "runClone[$i]: add an entry to this issue ($issue)"
        $SMIT issue $projectPath -a $createdIssue +message="some message #2"

        # add another issue "bis"
        echo "runClone[$i]: add an issue bis ($issue)"
        $SMIT issue $projectPath -a - summary="#$i.$issue.b" freeText="diver:${DIVERSIFICATION}_$$" +message="creation of issue: #$i.$issue.b"
        DIVERSIFICATION=$(expr $DIVERSIFICATION + 1)

        # push these 2 issues (pull first)
        echo "runClone[$i]: pull ($issue)"
        $SMIT pull $clone
        echo "runClone[$i]: push ($issue)"
        $SMIT push $clone
        # if some entries could not be pushed because
        # of conflicts, they will be pushed again at next 
        # iteration or at the end
        sleep 0.1
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
    $SMIT issue $clone/${project} | 
        sed -e "s/^Issue [0-9]*:/Issue ...:/" |
        sort >> $TEST_NAME.out
}

N_CLONES=5
DIVERSIFICATION=0
pids=""
for c in $(seq 1 $N_CLONES); do
    runClone $c p1 &
    pids="$pids $!"
done

# wait for all sub-processes
for p in $pids; do
    wait $p
done

# push all, in order to fix possible previous pushing conflicts
for c in $(seq 1 $N_CLONES); do
    $SMIT pull clone_$c
    $SMIT push clone_$c
done

# pull all, to synchronize all clones
for c in $(seq 1 $N_CLONES); do
    $SMIT pull clone_$c
done

stopServer

# check first clone and do a diff with others
checkClone 1 p1
for c in $(seq 2 $N_CLONES); do
    diff -ru clone_1/p1 clone_${c}/p1 >> $TEST_NAME.out
done

diff $srcdir/$TEST_NAME.ref $TEST_NAME.out
