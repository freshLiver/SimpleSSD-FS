#!/bin/bash

ssfx="$1"
PORT=${2:-3456}
if [[ -z "$ssfx" ]]; then
    echo "Usage: $0 sub-suffix [PORT]"
    exit 1
fi

read WORKNAME WORKTYPE SLET_ID <<< "grep fsa 3"

sizes=(
    "$((4 << 10))"
    "$((16 << 10))"
    "$((64 << 10))"
    "$((256 << 10))"
    "$((1024 << 10))"
)
workloads=(
    "/$WORKNAME/${sizes[4]}.dat.1 -${sizes[4]} ${sizes[4]}__"
    "/$WORKNAME/${sizes[3]}.dat.1 -${sizes[3]} ${sizes[3]}__"
    "/$WORKNAME/${sizes[2]}.dat.1 -${sizes[2]} ${sizes[2]}__"
    "/$WORKNAME/${sizes[1]}.dat.1 -${sizes[1]} ${sizes[1]}__"
    "/$WORKNAME/${sizes[0]}.dat.1 -${sizes[0]} ${sizes[0]}__"
)

for work in "${workloads[@]}"; do
    read path sfx pattern <<< "$work"

    TMP_FILE=$(mktemp -t gem5-$WORKNAME-$WORKTYPE-$sfx.XXXXXXXX)

    echo "
    #!/bin/bash
    mount /dev/sdb /mnt
    /mnt/$WORKNAME-$WORKTYPE --dev /dev/nvme0n1 --ns 1 --id $SLET_ID --path $path --pattern $pattern -init
    m5 exit" > "$TMP_FILE"

    cat "$TMP_FILE"

    MAKE_ARGS="M5_LOG_SUFFIX=$sfx-$ssfx TIME=$(date +%y%m%d-%H%M%S)"
    make run-timing GEM5_SCRIPT=$TMP_FILE $MAKE_ARGS &> /dev/null &
    sleep 5
    make socat-background PORT=${PORT} $MAKE_ARGS &

    # wait both gem5 and host
    echo "Workload ($MAKE_ARGS) is running, wait for gem5 and host..."
    jobs
    wait $(jobs -p)
done
