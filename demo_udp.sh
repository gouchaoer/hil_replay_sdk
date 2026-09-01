#!/usr/bin/env bash
if [ $(id -u) == 0 ]; then
    echo "please do not run as root!"
    exit 1
fi
SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH
source $SCRIPT_PATH/common.sh
prepare_env

sudo pkill -f demo_udp.py

NUMA=$(get_numactl demo_udp)

echo "NUMA: $NUMA"
$NUMA python3 demo_udp.py