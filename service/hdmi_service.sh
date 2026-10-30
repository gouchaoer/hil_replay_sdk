#!/usr/bin/env bash

if [ $(id -u) != 0 ]; then
    echo "please run as root!"
    exit 1
fi

SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH

source $SCRIPT_PATH/../common.sh

prepare_env

ps aux | grep 'hdmi_service.sh' |  grep -v 'bash -c' | grep -v grep | wc -l | grep 1 &>/dev/null
[ "$?" != "0" ] && echo "error, another hdmi_service.sh running!" && exit 1


function clean(){
    pkill -9 -f hdmi_service_app
    rm -rf /dev/shm/hdmi*
}
clean
trap clean EXIT

# set cpu performance mode
#cpupower frequency-set --governor performance
cpupower frequency-set --governor ondemand

service_sum=0

function start_hdmi_service_app_bg(){
    local numa_cmd="$1"
    local node_name="$2"
    local node_args="$3"
    local retry_max=3
    local retry_ct=0
    local log_file="hdmi_service_app.${node_name}.log"
    
    #rotate log
    for i in {8..0}; do 
        [ -f $log_file.$i.bak ] && mv -f $log_file.$i.bak $log_file.$((i+1)).bak
    done
    [ -f $log_file ] && mv -f $log_file $log_file.0.bak

    (
        while true; do
            retry_ct=$(expr "$retry_ct" + 1)
            echo "start hdmi_service_app try:${retry_ct}/${retry_max}, node_name:${node_name}, node_args:${node_args}" | tee -a "$log_file"
            $numa_cmd stdbuf -oL -eL ./hdmi_service_app "$node_args" &> "$log_file"
            ret=$?
            echo "hdmi_service_app exited, ret:${ret}, node_name:${node_name}, node_args:${node_args}" | tee -a "$log_file"
            [ "$retry_ct" -ge "$retry_max" ] && break
            sleep 1
        done
    ) &
}


for HDMI_IDX in A B C D E F G H I J ; do
    node_name="HDMI_${HDMI_IDX}"
    node_args="${!node_name}"
    [ -z "$node_args" ] && continue
    NUMA=$(get_numactl $node_name)
    echo "node_name:$node_name, NUMA:$NUMA"
    start_hdmi_service_app_bg "$NUMA" "$node_name" "$node_args"
    service_sum=$(expr $service_sum + 1)
done






while true ; do
    running_service_sum=$(ps aux | grep './hdmi_service_app' | grep -v grep | wc -l)
    echo "service_sum:$service_sum, running_service_sum: $running_service_sum"
    if [ "$running_service_sum" -lt "$service_sum" ]; then
        sleep 1
        running_service_sum2=$(ps aux | grep './hdmi_service_app' | grep -v grep | wc -l)
        if [ "$running_service_sum2" -lt "$service_sum" ]; then
            echo "error, some hdmi_service_app not running!"
            exit 1
        fi
    fi
    sleep 1
done