#!/bin/sh
# Test configuration of users via the web interface
# - modify password
# - modify permissions
# 

. $srcdir/functions
SMITC=$srcdir/../bin/smitc

initTest
cleanRepo
initRepo
startServer

echo "Starting Test"

dostep "sign in as USER1"
$SMITC signin http://127.0.0.1:$PORT $USER1 $PASSWD1

dostep "modify USER1 password (nominal case)"
$SMITC postconfig "http://127.0.0.1:$PORT/users/$USER1" sm_passwd1=xxx sm_passwd2=xxx

dostep "post empty password for USER1"
$SMITC postconfig "http://127.0.0.1:$PORT/users/$USER1" sm_passwd1= sm_passwd2=

dostep "post USER1 password: error, passwords do not match"
$SMITC postconfig "http://127.0.0.1:$PORT/users/$USER1" sm_passwd1=xxx sm_passwd2=x-x

dostep "sign out"
$SMITC signout "http://127.0.0.1:$PORT"

dostep "post USER1 password, expect error, not signed in"
$SMITC postconfig "http://127.0.0.1:$PORT/users/$USER1" sm_passwd1=yyy sm_passwd2=yyy

dostep "sign in: check old password $PASSWD1, expect error"
$SMITC signin http://127.0.0.1:$PORT $USER1 $PASSWD1

dostep "sign in: check new password xxx, expect OK"
$SMITC signin http://127.0.0.1:$PORT $USER1 xxx

echo "Stopping Test"
stopServer

# Keep only logs from "Starting Test" -> "Stopping Test", and remove \r
sed -e "1,/Starting Test/ d" \
    -e "/Stopping Test/,$ d" \
    -e "s///" $TEST_NAME.log > $TEST_NAME.out
diff -u $srcdir/$TEST_NAME.ref $TEST_NAME.out
