#!/bin/sh

# Check that entries added server-side are correctly pulled

TEST_NAME=`basename $0`
# remove suffix .sh
TEST_NAME=`echo $TEST_NAME | sed -e "s/\.sh//"`
exec > $TEST_NAME.log 2>&1
rm -f $TEST_NAME.out

SMIT=../smit

set -e 

. $srcdir/functions

initTest

init() {
	initEmptyRepo

    # add custom property
    $SMIT project $REPO/$PROJECT1 addProperty "color select blue green yellow"

    # create some entries
    # create issue 1
    $SMIT issue $REPO/$PROJECT1 -a - "summary=first issue" color=blue
    $SMIT issue $REPO/$PROJECT1 -a 1 status=open color=green
    # create issue 2
    $SMIT issue $REPO/$PROJECT1 -a - "summary=second issue" color=yellow

}
cleanup() {
    REPO=trepo # just to be sure before the rm -rf
    rm -rf $REPO
    rm -rf clone1
}

cleanup
init
startServer

# smit-clone
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone1

# smit-pull
cd clone1
../$SMIT pull
cd -

stopServer

# add an entry (server-side)
$SMIT issue $REPO/$PROJECT1 -a 1 color=yellow freeText="hello world"
# add an issue (server-side)
$SMIT issue $REPO/$PROJECT1 -a - summary="t---d issue" freeText="this is xyz"

# add attached files to issues #2 and #3
tmpDir=$(mktemp -d)
echo file1_abc > "$tmpDir/file1"
echo file2_0123 > "$tmpDir/file2"
$SMIT issue $REPO/$PROJECT1 -a 2 --file "$tmpDir/file1"
$SMIT issue $REPO/$PROJECT1 -a 3 summary="third issue" freeText="this is xyz" --file "$tmpDir/file2"
rm -rf "$tmpDir"

startServer
cd clone1
../$SMIT pull --user $USER1 --passwd $PASSWD1
cd -
stopServer

# check that the new entry and the new issue have been pulled
t_runcmd $SMIT issue clone1/$PROJECT1 1 --history --properties
t_runcmd $SMIT issue clone1/$PROJECT1 2 --history --properties
t_runcmd $SMIT issue clone1/$PROJECT1 3 --history --properties

# filter out dates
sed -e "/^Date:/d" $TEST_NAME.out > $TEST_NAME.out.fil

# check conformance
diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out.fil


