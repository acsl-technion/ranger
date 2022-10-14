#!/bin/bash

function msg() {
    echo "* $(date "+%Y-%m-%d %H:%M:%S") $@"
}

file=.test-tmp.txt
final=
function collect() {
    data=$(cat $file | grep -i seed)
    rm -f $file
    final=$(echo -e "$@ $data\n$final")
}

function finish() {
    echo -e "\r* Final error report: *"
    echo "$final"
    rm -f $file
    exit 0
}

trap finish INT

while (( 1 > 0 )); do
    for x in $(ls bin/test*.exe); do
        msg Running $x
        eval $x | tee $file
        if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
            msg Error
            collect $x
        else
            msg Okay
        fi
    done
    echo "Finished running tests. Rerunning in 2 seconds..."
    sleep 2
done

