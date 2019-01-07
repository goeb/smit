
SMIT=../linux/smit
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
    $SMIT project -c $REPO/$PROJECT
    $SMIT user $USER --passwd $PASSWD --project $PROJECT:rw -d $REPO
}
start() {
    $SMIT serve $REPO --listen-port $PORT &
    smit_pid=$!
    sleep 2 # wait for the server to start
    $SMITC signin http://127.0.0.1:$PORT $USER $PASSWD
}
stop() {
	echo killing smit_pid=$smit_pid
	kill $smit_pid
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
        r=`$SMITC post "http://127.0.0.1:$PORT/$PROJECT/issues/new" "summary=test-xxx-$i-$$" "+message=new-message-$i"`
        issueId=`echo $r | sed -e "s;/.*;;"`
        entryId=`echo $r | sed -e "s;.*/;;"`
        echo "created NEW $issueId/$entryId"
        # add entries
        for j in `seq 1 $nentries`; do
            $SMITC post "http://127.0.0.1:$PORT/$PROJECT/issues/$issueId" "+message=test-xxx-$i-$j-$$" summary=title-$i-$j-$$
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
N_100=5
pids=""
for reads in 1 2 3 4 5; do
	doreads $N_100 &
	pids="$pids $!"
done

for writes in 1 2 3 4 5; do
	dowritesnew $N_100 10 &
	pids="$pids $!"
done

# wait until all are completed
for pid in $pids; do
	wait $pid
	echo "wait: got pid $pid"
done

dowritesUpdateAllSummaries

stop

sleep 2

# relaunch the serveur and check that we did not lose anything

start

$SMITC get "http://127.0.0.1:$PORT/$PROJECT/issues?colspec=id+summary&sort=id" > endurancetest.log

stop

cmp endurancetest.ref endurancetest.log

echo -n "$0 ... "
if [ $? -eq 0 ]; then echo OK
else echo "ERROR (check diff functest.ref functest.log, repo=$REPO)"
fi

