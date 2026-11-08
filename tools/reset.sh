#!/usr/bin/env bash
SCRIPT_DIR=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_DIR

echo "turn down CH2..."
reg_rw /dev/xdma0_user 0x6A0000 w 0x00001000  # GPIO: 0x6A0000

echo "plz run sudo reboot to restart linux"