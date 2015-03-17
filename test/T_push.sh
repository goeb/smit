#!/bin/sh

# test smit push in a simple nominal case:
# - push local issue, entry and attached file

. $srcdir/functions

initTest
cleanRepo
initRepo
startServer

# do a clone
rm -rf clone1
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone1

# add an issue in the clone
$SMIT issue clone1/$PROJECT1 -a - summary="issue of clone1" freeText="this is xyz"
$SMIT issue clone1/$PROJECT1 -a 3 summary="third issue" freeText="y1y2" "+file=5ab64de1bcfb3d5500b2a95145f44f74aa6ed72f/file2.txt"

# add the attached file in the clone
mkdir clone1/$PROJECT1/objects/5a
echo "-- file2" > clone1/$PROJECT1/objects/5a/b64de1bcfb3d5500b2a95145f44f74aa6ed72f
echo "-- contents of file2" >> clone1/$PROJECT1/objects/5a/b64de1bcfb3d5500b2a95145f44f74aa6ed72f
# sha1 of this file is: 5ab64de1bcfb3d5500b2a95145f44f74aa6ed72f

# add another issue in the clone
$SMIT issue clone1/$PROJECT1 -a 2 summary="issue-2" freeText="free-text-issue2" 

# do the push
cd clone1
../$SMIT push
cd -

# check that the new entries, issue, and file have been pushed
# do a clone2 and compare clone1 and clone2
rm -rf clone2
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone2

stopServer

echo "diff clone1/p1 clone2/p1" > $TEST_NAME.out
diff -ru clone1/p1 clone2/p1 >> $TEST_NAME.out
echo OK >> $TEST_NAME.out

$SMIT issue clone2/$PROJECT1 1 -ph | grep -v ^Date >> $TEST_NAME.out
$SMIT issue clone2/$PROJECT1 2 -ph | grep -v ^Date >> $TEST_NAME.out
$SMIT issue clone2/$PROJECT1 3 -ph | grep -v ^Date >> $TEST_NAME.out

diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out 


