#!/bin/bash
#!/bin/bash
ssfx="$1"
MAKE_ARGS_EX="$2"
if [[ -z "$ssfx" ]]; then
    echo "Usage: $0 sub-suffix [MAKE_ARGS_EX]"
    exit 1
fi

read WORKNAME WORKTYPE SLET_ID MODE64 <<< "stats slet 5 -mode64" # sid 4 == stats32, 5 == stats64

workloads=(
    "/$WORKNAME/x2000/ x2000"
    # "/$WORKNAME/x1500/ x1500"
    # "/$WORKNAME/x1000/ x1000"
    # "/$WORKNAME/x500/ x500"
)

for work in "${workloads[@]}"; do
    read path sfx <<< "$work"

    TMP_FILE=$(mktemp -t gem5-$WORKNAME-$WORKTYPE-$sfx.XXXXXXXX)

    echo "
    #!/bin/bash
    mount /dev/sdb /mnt
    mount /dev/nvme0n1 /nvme
    stat -fc %s /nvme
    /mnt/$WORKNAME-$WORKTYPE --dev /dev/nvme0n1 --ns 1 --id $SLET_ID --mountpoint /nvme --path $path $MODE64 -init
    m5 exit" > "$TMP_FILE"

    cat "$TMP_FILE"

    MAKE_ARGS="M5_LOG_SUFFIX=$sfx-$ssfx TIME=$(date +%y%m%d-%H%M%S) $MAKE_ARGS_EX"
    make run-timing GEM5_SCRIPT=$TMP_FILE $MAKE_ARGS &> /dev/null &
    sleep 5
    make socat-background $MAKE_ARGS &

    # wait both gem5 and host
    echo "Workload ($MAKE_ARGS) is running, wait for gem5 and host..."
    jobs
    wait $(jobs -p)
done
