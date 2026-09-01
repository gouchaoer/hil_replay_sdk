#!/usr/bin/env bash

if [ $(id -u) != 0 ]; then
    echo "please run as root!"
    exit 1
fi

SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH

source $SCRIPT_PATH/../common.sh

prepare_env

ps aux | grep 'can_service.sh' | grep -v 'bash -c' | grep -v grep | wc -l | grep 1 &>/dev/null
[ "$?" != "0" ] && echo "error, another can_service.sh running!" && exit 1


####### init can device #######
dev_list=""
for idx in $(seq 0 7); do
    var_name=CAN_${idx}
    var_json=${!var_name}
    [ "$var_json" == "" ] && echo "$var_name skip..." && continue
    dev=$(echo "$var_json" | jq -er '.dev')
    [ "$?" != "0" ] && echo "error, $var_name parse dev error!!!" && exit 1
    fd=$(echo "$var_json" | jq -er '.fd')
    [ "$?" != "0" ] && echo "error, $var_name parse fd error!!!" && exit 1
    echo "$var_name: $var_json, dev:$dev, fd:$fd, CAN_DRIVER:$CAN_DRIVER"

    if [ "$CAN_DRIVER" == "pcan" ]; then
        ip link set $dev down
        if [ "$fd" == "1" ]; then
            ip link set $dev type can bitrate 500000 dbitrate 2000000 sample-point 0.875 fd on
        else
            ip link set $dev type can bitrate 500000
        fi
        ifconfig $dev txqueuelen 1000
        ip link set $dev up
    elif [ "$CAN_DRIVER" == "f81601a" ]; then
        set -x
        ip link set $dev down
        ip link set $dev type can restart-ms 100
        if [ "$fd" == "1" ]; then
            ip link set $dev type can bitrate 500000 sjw 1 dsjw 1 dbitrate 2000000 fd on fd-non-iso off
        else
            ip link set $dev type can bitrate 500000 sjw 1 dsjw 1
        fi
        ip link set $dev type can berr-reporting on
        ifconfig $dev txqueuelen 1000
        tc qdisc add dev $dev root handle 1: pfifo
        ip link set $dev up
    else
        echo "error, unknown CAN_DRIVER: $CAN_DRIVER!!!"
        exit 1
    fi
    set +x
    dev_list="$dev_list $dev"
done
echo -e "init dev_list: $dev_list"
####### init can device #######




####### check can status #######
declare -A tx_packets_pre_map=()
declare -A rx_packets_pre_map=()
function check_can(){
    dev=$1
    ts_str=$(get_ts_str)
    ifconfig $dev 2>&1 | grep RUNNING &> /dev/null
    [ "$?" != "0" ] && echo "$ts_str $dev not found in ifconfig" | tee /dev/shm/MONITOR__${dev}__ERROR && return

    tx_packets=$(cat /sys/class/net/$dev/statistics/tx_packets)
    rx_packets=$(cat /sys/class/net/$dev/statistics/rx_packets)
    if [ -z "${tx_packets_pre_map[$dev]}" ] ; then
        tx_packets_pre_map[$dev]=$tx_packets
    fi
    if [ -z "${rx_packets_pre_map[$dev]}" ] ; then
        rx_packets_pre_map[$dev]=$rx_packets
    fi
    tx_packets_pre=${tx_packets_pre_map[$dev]}
    tx_packets_pre_map[$dev]=$tx_packets
    rx_packets_pre=${rx_packets_pre_map[$dev]}
    rx_packets_pre_map[$dev]=$rx_packets
    tx_fps=$(expr $tx_packets - $tx_packets_pre)
    rx_fps=$(expr $rx_packets - $rx_packets_pre)
    if [ "$tx_fps" == "0" ] || [ "$rx_fps" == "0" ] ; then
        echo "$ts_str $dev, tx:$tx_packets, rx:$rx_packets" | tee /dev/shm/MONITOR__${dev}__WARN
    else
        echo "$ts_str $dev, tx:$tx_packets, rx:$rx_packets" > /dev/shm/MONITOR__${dev}__OKAY
    fi
}

while true; do
    for dev in $dev_list; do
        check_can $dev
    done
    sleep 0.9
done
####### check can status #######