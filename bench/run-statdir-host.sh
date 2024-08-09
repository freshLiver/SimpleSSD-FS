#!/bin/bash

PORT=${1:-3456}

for dep in {1,4}; do
    for nf in {1000,2000,4000}; do
        sfx="-d$dep-f$nf"

        subdir=""
        if [[ "$dep" -eq 4 ]]; then
            subdir="/1/2/3"
        fi

        TMP_FILE=$(mktemp -t gem5-statdir-host-$sfx.XXXXXXXX)

        echo "
        #!/bin/bash
        mount /dev/sdb /mnt
        mount /dev/nvme0n1 /nvme
        /mnt/statdir-host --dir /nvme/statdir$sfx-contig$subdir
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
done
