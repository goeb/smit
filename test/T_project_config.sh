#!/bin/sh

# test project config modification

. $srcdir/functions

initTest
rm -r $TEST_NAME.out

# trepo/p1*    user1       rw
# trepo/p1*    user2       admin
if [ "$REPO" != "trepo" ]; then
    fail "REPO != trepo (REPO=$REPO)"
fi
rm -rf $REPO
mkdir $REPO
$SMIT init $REPO
$SMIT project -c $REPO/p1
$SMIT project -c $REPO/p1/sub1
$SMIT user user1 -d $REPO --passwd user1
$SMIT user user1 -d $REPO --project "p1*:rw"
$SMIT user user2 -d $REPO --passwd user2
$SMIT user user2 -d $REPO --project "p1*:admin"

# create some issues
$SMIT issue $REPO/p1 -a - "summary=first issue of p1"
$SMIT issue $REPO/p1/sub1 -a - "summary=first issue of p1/sub1"

# start the smit server
startServer

# test modification of project config (nominal case)
echo "Modification of project config by authorized user" >> $TEST_NAME.out
SMITC=$srcdir/../bin/smitc
$SMITC signin http://127.0.0.1:$PORT user2 user2
$SMITC postconfig "http://127.0.0.1:$PORT/p1/config" \
    projectName=p1 \
    propertyName=propx \
    type=text \
    label="the label of propx" \
    >> $TEST_NAME.out
$SMITC postconfig "http://127.0.0.1:$PORT/p1/sub1/config" \
    projectName=p1/sub1 \
    propertyName=propx_sub1 \
    type=select \
    selectOptions="option-one" \
    >> $TEST_NAME.out
$SMIT project -al $REPO | \
    sed -e "s/+parent .*/+parent .../" -e "s/+ctime .*/+ctime .../" \
        >> $TEST_NAME.out

# test modification of project config by unauthorized user1 (error case)
echo "Modification of project config by unauthorized user" >> $TEST_NAME.out
SMITC=$srcdir/../bin/smitc
$SMITC signin http://127.0.0.1:$PORT user1 user1
$SMITC postconfig "http://127.0.0.1:$PORT/p1/config" \
    projectName=p1 \
    propertyName=prop-yy \
    type=text \
    label="the label of prop-yy" \
    >> $TEST_NAME.out
$SMITC postconfig "http://127.0.0.1:$PORT/p1/sub1/config" \
    projectName=p1/sub1 \
    propertyName=propx_sub1-yy \
    type=select \
    selectOptions="option-one-yy" \
    >> $TEST_NAME.out
$SMIT project -al $REPO | \
    sed -e "s/+parent .*/+parent .../" -e "s/+ctime .*/+ctime .../" \
        >> $TEST_NAME.out
$SMITC get http://127.0.0.1:$PORT/p1/config

stopServer

diff $srcdir/$TEST_NAME.ref $TEST_NAME.out
