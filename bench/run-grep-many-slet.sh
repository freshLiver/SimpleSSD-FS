#!/bin/bash

sfx="$1"
PORT=${2:-3456}
if [[ -z "$sfx" ]]; then
    echo "Usage: $0 suffix [PORT]"
    exit 1
fi

read WORKNAME WORKTYPE SLET_ID <<< "grep slet 3"

workloads=(
    "1024-x2000 ISCgrep"
    "1024-x1500 ISCgrep"
    "1024-x1000 ISCgrep"
    "1024-x500 ISCgrep"

    "4096-x2000 ISCgrep"
    "256-x2000 ISCgrep"
    "512-x2000 ISCgrep"

    # "4096-x1500 ISCgrep"
    # "4096-x1000 ISCgrep"
    # "4096-x500 ISCgrep"
    # "256-x1500 ISCgrep"
    # "256-x1000 ISCgrep"
    # "256-x500 ISCgrep"
    # "512-x1500 ISCgrep"
    # "512-x1000 ISCgrep"
    # "512-x500 ISCgrep"
)

for work in "${workloads[@]}"; do
    read task pattern <<< "$work"
    path="/$WORKNAME/$task/"

    TMP_FILE=$(mktemp -t gem5-$WORKNAME-$WORKTYPE-$task.XXXXXXXX)

    echo "
    #!/bin/bash
    mount /dev/sdb /mnt
    mount /dev/nvme0n1 /nvme
    /mnt/$WORKNAME-$WORKTYPE --dev /dev/nvme0n1 --mountpoint /nvme --ns 1 --id $SLET_ID --path $path --pattern $pattern -init
    m5 exit" > "$TMP_FILE"

    cat "$TMP_FILE"

    MAKE_ARGS="M5_LOG_SUFFIX=-$task-$sfx TIME=$(date +%y%m%d-%H%M%S)"
    make run-timing GEM5_SCRIPT=$TMP_FILE $MAKE_ARGS &> /dev/null &
    sleep 5
    make socat-background PORT=${PORT} $MAKE_ARGS &

    # wait both gem5 and host
    echo "Workload ($MAKE_ARGS) is running, wait for gem5 and host..."
    jobs
    wait $(jobs -p)
done
