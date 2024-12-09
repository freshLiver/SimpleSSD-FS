#!/bin/bash
sfx="$1"
PORT=${2:-3456}
if [[ -z "$sfx" ]]; then
    echo "Usage: $0 suffix [PORT]"
    exit 1
fi

read WORKNAME WORKTYPE MODE64 <<< "stats host-o2"

workloads=(
    # "1024-x2000"
    # "1024-x1500"
    # "1024-x1000"
    # "1024-x500"
    "4096-x2000"
    "256-x2000"
    "512-x2000"

    # "4096-x1500"
    # "4096-x1000"
    # "4096-x500"
    # "256-x1500"
    # "256-x1000"
    # "256-x500"
    # "512-x1500"
    # "512-x1000"
    # "512-x500"
)


for work in "${workloads[@]}"; do
    read task pattern <<< "$work"
    path="/$WORKNAME/$task/"

    TMP_FILE=$(mktemp -t gem5-$WORKNAME-$WORKTYPE-$task.XXXXXXXX)

    echo "
    #!/bin/bash
    mount /dev/sdb /mnt
    mount /dev/nvme0n1 /nvme
    /mnt/$WORKNAME-$WORKTYPE --path /nvme$path $MODE64
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
