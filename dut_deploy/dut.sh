#!/usr/bin/env bash

SCRIPT_DIR=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_DIR
source config.sh

pkill -9 -f dut_chronyd
pkill -9 -f dut_phc2sys
pkill -9 -f dut_ptp4l

if [ "$DUT_HOST_SYNC" == "ptp4l" ]; then
    device=eth0
    while true ; do
        ifconfig | grep $device | grep RUNNING
        [ "$?" == "0" ] && echo "$device ok" && break
        echo "wait $device RUNNING..."
        sleep 1
    done
    ./dut_phc2sys  -s CLOCK_REALTIME -c $device -O 0 --step_threshold=1 -m --use_syslog=0 &> dut_phc2sys.log &
    ./dut_ptp4l -i $device --serverOnly=1 -m --step_threshold=1 \
        --hybrid_e2e=1 --unicast_listen=1 -m --use_syslog=0 -l 7 &> dut_ptp4l.log &
fi

if [ "$DUT_HOST_SYNC" == "clockdiff" ]; then
    ./dut_chronyd -f $SCRIPT_DIR/dut_chrony.conf -d &> dut_chronyd.log &
fi

sleep infinity
