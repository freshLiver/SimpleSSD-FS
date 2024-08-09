#!/bin/bash

PORT=${1:-3456}

workloads=(
    #"/md5/x100/ x100"
    "/md5/x500/ x500"
    #"/md5/x1000/ x1000"
    #"/md5/x2000/ x2000"
    #"/md5/x4000/ x4000"
)

for work in "${workloads[@]}"; do
    read path sfx <<< "$work"

    TMP_FILE=$(mktemp -t gem5-md5-fsa-$sfx.XXXXXXXX)

    echo "
    #!/bin/bash
    mount /dev/sdb /mnt
    /mnt/md5-fsa --dev /dev/nvme0n1 --ns 1 --id 2 --path $path -init
    m5 exit" > "$TMP_FILE"

    cat "$TMP_FILE"

    MAKE_ARGS="M5_LOG_SUFFIX=$sfx TIME=$(date +%y%m%d-%H%M%S)"
    make run-timing GEM5_SCRIPT=$TMP_FILE $MAKE_ARGS &> /dev/null &
    sleep 5
    make socat-background PORT=${PORT} $MAKE_ARGS &

    # wait both gem5 and host
    echo "Workload ($MAKE_ARGS) is running, wait for gem5 and host..."
    jobs
    wait $(jobs -p)
done
