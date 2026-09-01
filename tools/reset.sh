#!/usr/bin/env bash
SCRIPT_DIR=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_DIR

echo "turn down CH2..."
reg_rw /dev/xdma0_user 0x6A0000 w 0x00001000  # GPIO: 0x6A0000

wait_s=5
while true; do
    wait_s=$(expr $wait_s - 1)
    if [ "$wait_s" -le 0 ]; then
        break
    fi
    echo "wait $wait_s s for reboot..."
    sleep 1
done

sudo reboot