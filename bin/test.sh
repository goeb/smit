

./smitc  signin http://127.0.0.1:8080 homer homer

for i in `seq 1 ^C00`; do ./smitc post http://127.0.0.1:8080 things_to_do new "summary=test$i\&in_charge=toto$i\&status=open"; done
