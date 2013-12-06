
SMIT=../smit
SMITC=../bin/smitc

set -e 

REPO=testdir
PROJECT=myTestProject
USER=tuser
PASSWD=tpasswd

init() {
    REPO=testdir # just to be sure before the rm -rf
    rm -rf $REPO && mkdir $REPO
    $SMIT init $REPO
    $SMIT addproject $PROJECT -d $REPO
    $SMIT adduser $USER --passwd $PASSWD --project $PROJECT rw -d $REPO
}
start() {
    $SMIT serve $REPO &
    pid=$!
    sleep 2 # wait for the server to start
    $SMITC signin http://127.0.0.1:8090 $USER $PASSWD
}

doreads() {
    n=$1
    for i in `seq 1 $n`; do
        $SMITC get "http://127.0.0.1:8090/$PROJECT/issues?colspec=id+summary\&sort=id" |
        sed -e "s/,.*//" | (
        while read id; do
            if [ "$id" = "id" ]; then continue; fi
            echo "doreads[$$]: id=$id"
            $SMITC get "http://127.0.0.1:8090/$PROJECT/issues/$id?format=html" >/dev/null # get.html
            sleep 0.1
        done
        )
    done
}

dowrites() {
    n=$1
    nentries=$2 # number of messages under each issue
    for i in `seq 1 $n`; do
        echo "dowrites[$$]: $i"
        r=`$SMITC post "http://127.0.0.1:8090/$PROJECT/issues/new" "summary=test-xxx-$i" "+message=new-message-$i"`
        issueId=`echo $r | sed -e "s;/.*;;"`
        entryId=`echo $r | sed -e "s;.*/;;"`
        # add entries
        for j in `seq 1 $nentries`; do
            $SMITC post "http://127.0.0.1:8090/$PROJECT/issues/$issueId" "+message=test-xxx-$i-$j" summary=title-$i-$j
        done
    done
}

init
start
doreads 10 &
doreads 10 &
doreads 10 &
doreads 10 &
doreads 10 &
dowrites 10 10

sleep 2

# check 
$SMITC get "http://127.0.0.1:8090/$PROJECT/issues?colspec=id+summary\&sort=id" > functest.log

echo killing pid=$pid
kill $pid

sleep 2

cmp functest.ref functest.log
echo -n "$0 ... "
if [ $? -eq 0 ]; then echo OK
else echo "ERROR (check diff functest.ref functest.log, repo=$REPO)"
fi

