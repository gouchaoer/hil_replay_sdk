#!/usr/bin/env bash
export HIL_SDK_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
source $HIL_SDK_PATH/config.sh

export SSH_OPTION=" -o ConnectTimeout=1 -o ServerAliveInterval=2 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "
export SSH_AUTH_SOCK='' # disable ssh-agent
export LD_LIBRARY_PATH="$HIL_SDK_PATH/api:$LD_LIBRARY_PATH"

alias s="sshpass -p '$DUT_PASS' ssh $SSH_OPTION -p 22 $DUT_USER@$DUT_LAN_IP" # ssh DUT
alias t="sudo chronyd -q 'server ntp.aliyun.com iburst'" # time sync internet time
alias c="cd $HIL_SDK_PATH" # cd to common.sh path


function prepare_env(){
    ulimit -c unlimited    
    sudo sysctl -w kernel.core_pattern=$HIL_SDK_PATH/core.%e.%p &>/dev/null
    sudo chmod 777 /dev/shm/xdma_* &> /dev/null
    sudo chmod 777 /dev/shm/MONITOR__* &> /dev/null
    sudo chmod 777 /dev/xdma* &> /dev/null
    sudo chmod 777 /dev/shm/HOST_clockdiff_DUT &> /dev/null
    numa_ct=$(numactl --hardware | grep cpus | wc -l)
    [ "$numa_ct" != "4" ] && echo "warning, numa node should = 4, but numa_ct:$numa_ct"
    cpu_ct=$(grep -c 'processor' /proc/cpuinfo)
    [ "$cpu_ct" -lt "32" ] && echo "warning, cpu should >= 32, but cpu_ct:$cpu_ct"
}

function get_ts_str(){
    ts_str=$(python3 $HIL_SDK_PATH/api/hil_api.py hil_ts_get -q)
    [ "$?" != "0" ] && echo "get_ts_str error!!!" && exit 1
    echo $ts_str
}


function get_numactl(){
    [ "$BIND_CPU" == "0" ] && echo "" && return 0
    lscpu_fn=$HIL_SDK_PATH/lscpu.txt
    node_name=$1
    if [ ! -e $lscpu_fn ]; then
        echo 'lscpu.txt not found, generate it'
        lscpu -p=NODE,CPU > $lscpu_fn
        echo 'append node_name to bind cpu in lscpu.txt'
    fi
    match_lines=$(grep -e "$node_name," -e "$node_name$"  $lscpu_fn)
    [ -z "$match_lines" ] && echo "error, node_name:$node_name not found in $lscpu_fn" && exit 1
    cpu_numbers=$(echo "$match_lines" | cut -d',' -f2 | tr '\n' ',' | sed 's/,$//')
    numa_number=$(echo "$match_lines" | head -n 1 | cut -d',' -f1)
    echo "numactl --membind=$numa_number --physcpubind=$cpu_numbers "
}

function dut_exec(){
    time_out=$1
    shift
    [ "$time_out" == "0" ] && TIMEOUT='' || TIMEOUT="timeout $time_out"
    $TIMEOUT sshpass -p "$DUT_PASS" ssh $SSH_OPTION $DUT_USER@$DUT_LAN_IP "$@"
    exit_code=$?
    [ "$exit_code" == "130" ] && exit 130 # Ctrl + C
    return $exit_code
}

function dut_rsync(){
    host2dut=$1
    from_path=$2
    to_path=$3

    if [ "$host2dut" == "1" ]; then
        echo "rsync HOST:$from_path ---> DUT:$to_path"
        sshpass -p "$DUT_PASS" rsync --delete --no-owner --no-group -avl -e \
            "ssh $SSH_OPTION" $from_path $DUT_USER@$DUT_LAN_IP:$to_path >/dev/null
    else
        echo "rsync DUT:$from_path ---> HOST:$to_path"
        sshpass -p "$DUT_PASS" rsync --delete --no-owner --no-group -avl -e \
            "ssh $SSH_OPTION" $DUT_USER@$DUT_LAN_IP:$from_path $to_path >/dev/null
    fi
    exit_code=$?
    [ "$exit_code" == "130" ] && exit 130 # Ctrl + C
    return $exit_code
}
