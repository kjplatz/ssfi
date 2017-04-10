#!/bin/bash

B=$(for i in $(cat $1 | sort -u); do
echo "$(grep $i $1 | wc -l) $i"
done)

echo "$B" | sort --reverse
