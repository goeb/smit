

# take randoms

set -e
TMP=T_smp_encode_decode.out
N=2000
echo "Running $N random combinations..."
for i in `seq 1 $N`; do 
    modulo=`expr $i % 1000 || echo >/dev/null` # prevent exit code 1 from expr
    if [ $modulo -eq 0 ]; then
        echo $i...
    fi
    echo -n "smp.1.1 " > $TMP.ref
    ./get_random_value $i >> $TMP.ref
    echo >> $TMP.ref

    ./get_random_value $i | ./smparser -e | ./smparser - > $TMP

    diff $TMP.ref $TMP
done
    
