#!/bin/sh

# test smit push with conflict (error case)

. $srcdir/functions

initTest
cleanRepo
initRepo
startServer

# do a clone
rm -rf clone1
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone1

# do another clone
rm -rf clone2
$SMIT clone http://127.0.0.1:$PORT --user $USER1 --passwd $PASSWD1 clone2

# add an issue in clone1
$SMIT issue clone1/$PROJECT1 -a - summary="issue of clone1" freeText="this is xyz"

# push it, so that clone2 misses it
set -x
cd clone1
../$SMIT push
cd -

# add an issue in clone2
$SMIT issue clone2/$PROJECT1 -a - summary="issue of clone2" freeText="abcdef"

# push from clone2. it must be rejected
cd clone2
if [ ../$SMIT push ]; then
    echo ERROR: push should have failed
    stopServer
    exit 1
else
    echo Expected failure OK
fi
cd -

stopServer
