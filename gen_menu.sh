#!/bin/sh


echo "gen_menu *$" 1>&2

echo "<div class='menu'>"
echo "<ul>"
for file in $*; do
    echo file=$file 1>&2
    set -x
    TITLE=`pandoc < $file | grep "^<h1" | sed -e "s;</h1>;;" -e "s;.*>;;"`
    set +x
    echo "<li>$TITLE</li>"
done

echo "</ul>"
echo "</div>"
