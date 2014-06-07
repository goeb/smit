

# take randoms

set -e
TMP=T_smp_encode_decode.out
for i in `seq 1 10000`; do 
    echo i=$i
    echo -n "smp.1.1 " > $TMP.ref
    ./get_random_value $i >> $TMP.ref
    echo >> $TMP.ref

    ./get_random_value $i | ../bin/smparser -e | ../bin/smparser - > $TMP

    diff $TMP.ref $TMP
done
    
