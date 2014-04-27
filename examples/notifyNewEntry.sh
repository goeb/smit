#!/bin/sh

# Example of a shell script that parses the data 
# given by smit on its stdin

# How to install this script:
#     copy this script to your $REPO
#     echo "./notifyNewEntry" > $REPO/$PROJECT/trigger

processLine() {
    key="$1"
    shift
    i=1
    if [ x"$key" = x+message ]; then
        echo "<<-------------- message -------------->>"
        while read -r line; do
            echo $line
        done
        echo "<<--------------   end   -------------->>"
    else
        while [ x"$1" != x ]; do
            arg="$1"
            printf "$key.$i $arg\n"
            i=`expr $i + 1`
            shift
        done
    fi
}

echo "$0 starting...................."

while read -r line; do 
    eval "processLine $line"
done

echo "$0 end........................."
