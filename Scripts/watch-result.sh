#!/bin/bash

MY_DIR=$(dirname $(realpath $0))
DATA_DIR=${1:-"logs"}

watch -n 30 --color -- "$MY_DIR"/print-result.sh "$DATA_DIR"