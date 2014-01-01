
SMIT=../smit
SMITC=../bin/smitc

set -e 

REPO=testdir
PROJECT=myTestProject
USER=tuser
PASSWD=tpasswd
PORT=8099

init() {
    REPO=testdir # just to be sure before the rm -rf
    rm -rf $REPO && mkdir $REPO
    $SMIT init $REPO
    $SMIT addproject $PROJECT -d $REPO
    $SMIT adduser $USER --passwd $PASSWD --project $PROJECT rw -d $REPO
}
start() {
    $SMIT serve $REPO --listen-port $PORT &
    pid=$!
    sleep 2 # wait for the server to start
    $SMITC signin http://127.0.0.1:$PORT $USER $PASSWD
}

doreads() {
    n=$1
    for i in `seq 1 $n`; do
        $SMITC get "http://127.0.0.1:$PORT/$PROJECT/issues?colspec=id+summary\&sort=id" |
        sed -e "s/,.*//" | (
        while read id; do
            if [ "$id" = "id" ]; then continue; fi
            echo "doreads[$$]: id=$id"
            $SMITC get "http://127.0.0.1:$PORT/$PROJECT/issues/$id?format=html" >/dev/null # get.html
            sleep 0.1
        done
        )
    done
}

dowritesnew() {
    n=$1
    nentries=$2 # number of messages under each issue
    for i in `seq 1 $n`; do
        echo "dowrites[$$]: $i"
        r=`$SMITC post "http://127.0.0.1:$PORT/$PROJECT/issues/new" "summary=test-xxx-$i" "+message=new-message-$i"`
        issueId=`echo $r | sed -e "s;/.*;;"`
        entryId=`echo $r | sed -e "s;.*/;;"`
        # add entries
        for j in `seq 1 $nentries`; do
            $SMITC post "http://127.0.0.1:$PORT/$PROJECT/issues/$issueId" "+message=test-xxx-$i-$j" summary=title-$i-$j
        done
    done
}

dowritesUpdateAllSummaries() {
    # set all summaries to 
    $SMITC get "http://127.0.0.1:$PORT/$PROJECT/issues?colspec=id+summary\&sort=id" |
    sed -e "s/,.*//" | (
    while read id; do
        if [ "$id" = "id" ]; then continue; fi
        $SMITC post "http://127.0.0.1:$PORT/$PROJECT/issues/$id" "+message=test-xxx-$id-z" summary=title-$id-zz
    done
    )
}

init
start
doreads 10 &
doreads 10 &
doreads 10 &
doreads 10 &
doreads 10 &
dowritesnew 10 10 &
dowritesnew 15 8 
sleep 2
dowritesUpdateAllSummaries

echo killing pid=$pid
kill $pid

sleep 2

# relaunch the serveur and check that we did not lose anything

start

$SMITC get "http://127.0.0.1:$PORT/$PROJECT/issues?colspec=id+summary\&sort=id" > functest.log

echo killing pid=$pid
kill $pid

cmp functest.ref functest.log

echo -n "$0 ... "
if [ $? -eq 0 ]; then echo OK 1>&2
else echo "ERROR (check diff functest.ref functest.log, repo=$REPO)" 1>&2
fi

