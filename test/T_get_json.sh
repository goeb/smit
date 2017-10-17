#!/bin/sh

# test smit get services, json format
# prerequisite: tool 'jq'

. $srcdir/functions


initTest
rm -r $TEST_NAME.out

cleanRepo
initRepo

SMIT=../smit
SMITC=$srcdir/../bin/smitc

startServer

t_signin() {
    user=$USER1
    passwd=$PASSWD1
    echo ">>> $SMITC signing http://127.0.0.1:$PORT $user $passwd" >> $TEST_NAME.out
	$SMITC signin http://127.0.0.1:$PORT $user $passwd >> $TEST_NAME.out 2>&1
}

t_runget() {
    resource="$1"
    echo ">>> $SMITC get http://127.0.0.1:$PORT/$PROJECT1/$resource" >> $TEST_NAME.out
	$SMITC get "http://127.0.0.1:$PORT/$PROJECT1/$resource" |
   		jq . |
	   	sed -e 's/"ctime": [0-9]*/"ctime": .../' |
	   	sed -e 's/"parent": ".*"/"parent": .../' |
	   	sed -e 's/"id": ".*"/"id": .../' >> $TEST_NAME.out 2>&1
}

t_signin

t_runget "/issues/?format=json&colspec=id+status+summary"
t_runget "/issues/1?format=json"
t_runget "/issues/2?format=json"
t_runget "/entries/?format=json"

stopServer


diff $srcdir/$TEST_NAME.ref $TEST_NAME.out
