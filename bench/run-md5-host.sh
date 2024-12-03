#!/bin/bash

ssfx="$1"
PORT=${2:-3456}
if [[ -z "$ssfx" ]]; then
    echo "Usage: $0 sub-suffix [PORT]"
    exit 1
fi

workloads=(
    "/md5/x2000/ x2000"
    "/md5/x1500/ x1500"
    "/md5/x1000/ x1000"
    "/md5/x500/ x500"
)

for work in "${workloads[@]}"; do
    read path sfx <<< "$work"

    TMP_FILE=$(mktemp -t gem5-md5-host-$sfx.XXXXXXXX)

    echo "
    #!/bin/bash
    mount /dev/sdb /mnt
    mount /dev/nvme0n1 /nvme
    /mnt/md5-host --path /nvme$path
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
