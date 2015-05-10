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
# Check contents of the clone 
for p in 1 2 3 4 5; do
    echo "Issues of clone1/p$p:" >> $TEST_NAME.out
    $SMIT issue clone1/p$p -h | grep -v ^Date >> $TEST_NAME.out
done

# test pulling/pushing TODO
$SMIT pull clone1 >> $TEST_NAME.out 2>&1
$SMIT issue clone1/p4 -a - "summary=issue-to-be-pushed / p4" >> $TEST_NAME.out
$SMIT issue clone1/p5 -a - "summary=issue-to-be-pushed / p5" >> $TEST_NAME.out
$SMIT push clone1 >> $TEST_NAME.out 2>&1
# test pushing an issue with role read-only
$SMIT issue clone1/p3 -a - "summary=issue that should not be pushed" >> $TEST_NAME.out
$SMIT push clone1 >> $TEST_NAME.out 2>&1

stopServer

# 
sed -e "s///" -e "s;/[0-9a-f]\{40\}$;/...;" $TEST_NAME.out > $TEST_NAME.out.fil
diff $srcdir/$TEST_NAME.ref $TEST_NAME.out.fil
