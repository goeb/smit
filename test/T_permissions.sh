#!/bin/sh

# test smit permissions
# - roles: none ref, ro, rw, admin
# - superadmin

. $srcdir/functions

initTest
rm -r $TEST_NAME.out

# trepo/p1    user1    none
# trepo/p2    user1    ref
# trepo/p3    user1    ro
# trepo/p4    user1    rw
# trepo/p5    user1    admin
if [ "$REPO" != "trepo" ]; then
    fail "REPO != trepo (REPO=$REPO)"
fi
rm -rf $REPO
mkdir $REPO
$SMIT init $REPO
for p in 1 2 3 4 5; do
    $SMIT project -c $REPO/p$p
done
$SMIT user user1 -d $REPO --passwd user1
$SMIT user user1 -d $REPO --project p1:none
$SMIT user user1 -d $REPO --project p2:ref
$SMIT user user1 -d $REPO --project p3:ro
$SMIT user user1 -d $REPO --project p4:rw
$SMIT user user1 -d $REPO --project p5:admin
# create 1 issue in each project
for p in 1 2 3 4 5; do
    $SMIT issue $REPO/p$p -a - "summary=p$p:first issue"
done

# start the smit server
startServer

# test smitc client
SMITC=$srcdir/../bin/smitc
$SMITC signin http://127.0.0.1:$PORT user1 user1
for p in 1 2 3 4 5; do
    echo "smitc POST p$p" >> $TEST_NAME.out
    $SMITC post "http://127.0.0.1:$PORT/p$p/issues/1" "+message=test-xxx-p$p" summary="new-summary/p$p" >> $TEST_NAME.out
    echo "smitc GET p$p" >> $TEST_NAME.out
    $SMITC get "http://127.0.0.1:$PORT/p$p/issues/?colspec=id+summary&sort=id&format=text" >> $TEST_NAME.out
done

# test cloning
rm -rf clone1
$SMIT clone http://127.0.0.1:$PORT --user user1 --passwd user1 clone1

# test pulling/pushing TODO

stopServer


# check results
for p in 1 2 3 4 5; do
    echo "Issues of clone1/p$p:" >> $TEST_NAME.out
    $SMIT issue clone1/p$p -h | grep -v ^Date >> $TEST_NAME.out
done

diff $srcdir/$TEST_NAME.ref $TEST_NAME.out
