#!/bin/sh

# test smit permissions - repository level
# superadmin / not superadmin

. $srcdir/functions

initTest
rm -r $TEST_NAME.out

cleanRepo
initEmptyRepo

startServer

# test smitc client
SMIT=../smit

t_runget() {
    user=$1
    passwd=$2
    resource=$3
    echo ">>> $SMIT get --user $user --passwd $passwd http://127.0.0.1:$PORT $resource" >> $TEST_NAME.out
    $SMIT get --user $user --passwd $passwd http://127.0.0.1:$PORT $resource 2>$TEST_NAME.stderr >$TEST_NAME.stdout
    cat $TEST_NAME.stderr $TEST_NAME.stdout >> $TEST_NAME.out
}

t_runclone() {
    user=$1
    passwd=$2
    localcopy=clone_x
    rm -rf $localcopy
    echo ">>> $SMIT clone --user $user --passwd $passwd http://127.0.0.1:$PORT $localcopy" >> $TEST_NAME.out
    $SMIT clone --user $user --passwd $passwd http://127.0.0.1:$PORT $localcopy 2>>$TEST_NAME.out
    find $localcopy >> $TEST_NAME.out
}

# regular user
t_runget $USER1 $PASSWD1 /
t_runget $USER1 $PASSWD1 /public
t_runget $USER1 $PASSWD1 /.smit
t_runget $USER1 $PASSWD1 /.smit/users
t_runget $USER1 $PASSWD1 /.smit/users/permissions
t_runget $USER1 $PASSWD1 /.smit/users/auth
t_runget $USER1 $PASSWD1 /.smit/templates

t_runclone $USER1 $PASSWD1

# superadmin
t_runget $USER_SUPER $PASSWD_SUPER /
t_runget $USER_SUPER $PASSWD_SUPER /public
t_runget $USER_SUPER $PASSWD_SUPER /.smit
t_runget $USER_SUPER $PASSWD_SUPER /.smit/users
t_runget $USER_SUPER $PASSWD_SUPER /.smit/users/permissions
t_runget $USER_SUPER $PASSWD_SUPER /.smit/users/auth
t_runget $USER_SUPER $PASSWD_SUPER /.smit/templates

t_runclone $USER_SUPER $PASSWD_SUPER


stopServer


# filter...
sed -e "s;/objects/.*;/objects/...;" \
    -e "s;-hash.*;...;" \
    $TEST_NAME.out > $TEST_NAME.out.fil
diff $srcdir/$TEST_NAME.ref $TEST_NAME.out.fil
