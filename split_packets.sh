#!/bin/sh

in="$1"
if [ ! -r "$in" ]; then
    echo "Can't read '$in'"
    exit
fi

for key in $( awk '/^key/ { print $2 }' < "$in" | sort | uniq ); do
    echo "Key: $key "
    grep "^key $key " "$in" | sed "s/^key $key//; s/#.*//" > "key.$key"
done
