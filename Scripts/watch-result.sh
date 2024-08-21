#!/bin/bash

DIR=${1:-"logs"}

HOST_LOGS="$DIR/*.host.log"
DEBUG_LOGS="$DIR/*.log"

CMD_SIM_TIME="grep -ir 'Simulation Time' $HOST_LOGS"
CMD_DATA_PATH="grep -m 3 -oE \"EXT4: lookup: '.*'\" $DEBUG_LOGS"
watch -n 5 -x bash -c "sort <($CMD_SIM_TIME) <($CMD_DATA_PATH)"
