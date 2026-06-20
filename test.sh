#!/usr/bin/env bash

longest=0

for cfile in ./tests/*.c; do
    base=$(basename $cfile)
    if [[ $(printf $base | wc -c) -gt $longest ]]; then
        longest=$(printf $base | wc -c)
    fi
done

for cfile in ./tests/*.c; do
    scfile=${cfile%.c}
    base=$(basename $cfile)
    cc -o $scfile $cfile
    printf -v outfile "%s.%s" "$scfile" "out"
    $scfile | diff - $outfile > /dev/null
    if [[ $? -eq 0 ]]; then
        printf "%-*s - P\n" $longest $base
    else
        printf "%-*s - U\n" $longest $base
    fi
    rm $scfile
done
