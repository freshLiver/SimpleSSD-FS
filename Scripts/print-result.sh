#!/bin/bash

DIR=${1:-"logs"}

HOST_LOGS="$DIR/*.host.log"
DEBUG_LOGS="$DIR/*.log"

simtime="$(grep -ir 'Simulation Time' $HOST_LOGS)"
iscpath="$(grep -m 3 -oE 'EXT4: lookup: '.*'' $DEBUG_LOGS)"

sort <(echo "$simtime") <(echo "$iscpath") \
    | sed 's/host.log:/host.log \t:/g' \
    | sed 's/~/ /g' \
    | sed 's/:EXT4: lookup:/     \t: /g' \
    | grep -P --color=always "(\d+ \d+|'(/[\w\.]+)+')"
