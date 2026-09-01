#!/usr/bin/env bash

if [ $(id -u) != 0 ]; then
    echo "please run as root!"
    exit 1
fi

SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH

source $SCRIPT_PATH/../common.sh

prepare_env

ps aux | grep 'video_service.sh' |  grep -v 'bash -c' | grep -v grep | wc -l | grep 1 &>/dev/null
[ "$?" != "0" ] && echo "error, another video_service.sh running!" && exit 1


function clean(){
    pkill -9 -f video_service_app
    pkill -9 -f decode_video_app
    rm -rf /dev/shm/xdma_*
}
clean
trap clean EXIT

# set cpu performance mode
#cpupower frequency-set --governor performance
cpupower frequency-set --governor ondemand

service_sum=0

function start_video_service_app_bg(){
    local numa_cmd="$1"
    local xdma_idx="$2"
    local h2c_idx="$3"
    local retry_max=3
    local retry_ct=0
    local log_file="video_service_app.${xdma_idx}.${h2c_idx}.log"
    local uart_log_file="xdma_${xdma_idx}_uart.log"

    #rotate log
    for i in {8..0}; do 
        [ -f $log_file.$i.bak ] && mv -f $log_file.$i.bak $log_file.$((i+1)).bak
        [ "$h2c_idx" == "0" ] && [ -f $uart_log_file.$i.bak ] && mv -f $uart_log_file.$i.bak $uart_log_file.$((i+1)).bak
    done
    [ -f $log_file ] && mv -f $log_file $log_file.0.bak
    [ "$h2c_idx" == "0" ] && [ -f $uart_log_file ] && mv -f $uart_log_file $uart_log_file.0.bak

    (
        while true; do
            retry_ct=$(expr "$retry_ct" + 1)
            echo "start video_service_app try:${retry_ct}/${retry_max}, XDMA_IDX:${xdma_idx}, H2C_IDX:${h2c_idx}" | tee -a "$log_file"
            $numa_cmd stdbuf -oL -eL ./video_service_app "$xdma_idx" "$h2c_idx" &> "$log_file"
            ret=$?
            echo "video_service_app exited, ret:${ret}, XDMA_IDX:${xdma_idx}, H2C_IDX:${h2c_idx}" | tee -a "$log_file"
            [ "$retry_ct" -ge "$retry_max" ] && break
            sleep 1
        done
    ) &
}

if [ "$VIDEO_A_XDMA" != "" ]; then
    XDMA_IDX=$VIDEO_A_XDMA
    VIDEO_0_PATH=$VIDEO_A_0_PATH
    VIDEO_0_SIZE=$(( $((VIDEO_A_01_SIZE_H)) * VIDEO_A_01_SIZE_W * 2))
    VIDEO_1_PATH=$VIDEO_A_1_PATH
    VIDEO_1_SIZE=$(( $((VIDEO_A_01_SIZE_H)) * VIDEO_A_01_SIZE_W * 2))
    VIDEO_2_PATH=$VIDEO_A_2_PATH
    VIDEO_2_SIZE=$(( $((VIDEO_A_23_SIZE_H)) * VIDEO_A_23_SIZE_W * 2))
    VIDEO_3_PATH=$VIDEO_A_3_PATH
    VIDEO_3_SIZE=$(( $((VIDEO_A_23_SIZE_H)) * VIDEO_A_23_SIZE_W * 2))
    VIDEO_4_PATH=$VIDEO_A_4_PATH
    VIDEO_4_SIZE=$(( $((VIDEO_A_45_SIZE_H)) * VIDEO_A_45_SIZE_W * 2))
    VIDEO_5_PATH=$VIDEO_A_5_PATH
    VIDEO_5_SIZE=$(( $((VIDEO_A_45_SIZE_H)) * VIDEO_A_45_SIZE_W * 2))
    VIDEO_6_PATH=$VIDEO_A_6_PATH
    VIDEO_6_SIZE=$(( $((VIDEO_A_67_SIZE_H)) * VIDEO_A_67_SIZE_W * 2))
    VIDEO_7_PATH=$VIDEO_A_7_PATH
    VIDEO_7_SIZE=$(( $((VIDEO_A_67_SIZE_H)) * VIDEO_A_67_SIZE_W * 2))
    VIDEO_8_PATH=$VIDEO_A_8_PATH
    VIDEO_8_SIZE=$(( $((VIDEO_A_89_SIZE_H)) * VIDEO_A_89_SIZE_W * 2))
    VIDEO_9_PATH=$VIDEO_A_9_PATH
    VIDEO_9_SIZE=$(( $((VIDEO_A_89_SIZE_H)) * VIDEO_A_89_SIZE_W * 2))
    VIDEO_10_PATH=$VIDEO_A_10_PATH
    VIDEO_10_SIZE=$(( $((VIDEO_A_1011_SIZE_H)) * VIDEO_A_1011_SIZE_W * 2))
    VIDEO_11_PATH=$VIDEO_A_11_PATH
    VIDEO_11_SIZE=$(( $((VIDEO_A_1011_SIZE_H)) * VIDEO_A_1011_SIZE_W * 2))
    
    xdma_dev="/dev/xdma${XDMA_IDX}_user" 
    while true; do
        [ -e $xdma_dev ] && break
        echo "waiting $xdma_dev"
        sleep 1
    done

    for H2C_IDX in $(seq 0 3); do
        export VIDEO_IDX_SIZE_LIST=""
        for i in $(seq 0 2); do
            video_idx=$(expr $H2C_IDX + $i \* 4)
            var_video_path="VIDEO_${video_idx}_PATH"
            var_video_size="VIDEO_${video_idx}_SIZE"
            video_path=${!var_video_path}
            video_size=${!var_video_size}
            if [ -n "$video_path" ] && [ -n "$video_size" ]; then
                VIDEO_IDX_SIZE_LIST="$VIDEO_IDX_SIZE_LIST $video_idx $video_size"
            fi
        done

        node_name=video_service_app_${XDMA_IDX}_${H2C_IDX}
        NUMA=$(get_numactl $node_name)
        echo "node_name:$node_name, NUMA:$NUMA, H2C_IDX:$H2C_IDX, VIDEO_IDX_SIZE_LIST:$VIDEO_IDX_SIZE_LIST"
        if [ -n "$VIDEO_IDX_SIZE_LIST" ]; then
            start_video_service_app_bg "$NUMA" "$XDMA_IDX" "$H2C_IDX"
            service_sum=$(expr $service_sum + 1)
        else
            echo "VIDEO_IDX_SIZE_LIST empty for XDMA_IDX:$XDMA_IDX, H2C_IDX:$H2C_IDX"
        fi
    done
fi



if [ "$VIDEO_B_XDMA" != "" ]; then
    XDMA_IDX=$VIDEO_B_XDMA
    VIDEO_0_PATH=$VIDEO_B_0_PATH
    VIDEO_0_SIZE=$(( $((VIDEO_B_01_SIZE_H)) * VIDEO_B_01_SIZE_W * 2))
    VIDEO_1_PATH=$VIDEO_B_1_PATH
    VIDEO_1_SIZE=$(( $((VIDEO_B_01_SIZE_H)) * VIDEO_B_01_SIZE_W * 2))
    VIDEO_2_PATH=$VIDEO_B_2_PATH
    VIDEO_2_SIZE=$(( $((VIDEO_B_23_SIZE_H)) * VIDEO_B_23_SIZE_W * 2))
    VIDEO_3_PATH=$VIDEO_B_3_PATH
    VIDEO_3_SIZE=$(( $((VIDEO_B_23_SIZE_H)) * VIDEO_B_23_SIZE_W * 2))
    VIDEO_4_PATH=$VIDEO_B_4_PATH
    VIDEO_4_SIZE=$(( $((VIDEO_B_45_SIZE_H)) * VIDEO_B_45_SIZE_W * 2))
    VIDEO_5_PATH=$VIDEO_B_5_PATH
    VIDEO_5_SIZE=$(( $((VIDEO_B_45_SIZE_H)) * VIDEO_B_45_SIZE_W * 2))
    VIDEO_6_PATH=$VIDEO_B_6_PATH
    VIDEO_6_SIZE=$(( $((VIDEO_B_67_SIZE_H)) * VIDEO_B_67_SIZE_W * 2))
    VIDEO_7_PATH=$VIDEO_B_7_PATH
    VIDEO_7_SIZE=$(( $((VIDEO_B_67_SIZE_H)) * VIDEO_B_67_SIZE_W * 2))
    VIDEO_8_PATH=$VIDEO_B_8_PATH
    VIDEO_8_SIZE=$(( $((VIDEO_B_89_SIZE_H)) * VIDEO_B_89_SIZE_W * 2))
    VIDEO_9_PATH=$VIDEO_B_9_PATH
    VIDEO_9_SIZE=$(( $((VIDEO_B_89_SIZE_H)) * VIDEO_B_89_SIZE_W * 2))
    VIDEO_10_PATH=$VIDEO_B_10_PATH
    VIDEO_10_SIZE=$(( $((VIDEO_B_1011_SIZE_H)) * VIDEO_B_1011_SIZE_W * 2))
    VIDEO_11_PATH=$VIDEO_B_11_PATH
    VIDEO_11_SIZE=$(( $((VIDEO_B_1011_SIZE_H)) * VIDEO_B_1011_SIZE_W * 2))
    
    xdma_dev="/dev/xdma${XDMA_IDX}_user" 
    while true; do
        [ -e $xdma_dev ] && break
        echo "waiting $xdma_dev"
        sleep 1
    done

    for H2C_IDX in $(seq 0 3); do
        export VIDEO_IDX_SIZE_LIST=""
        for i in $(seq 0 2); do
            video_idx=$(expr $H2C_IDX + $i \* 4)
            var_video_path="VIDEO_${video_idx}_PATH"
            var_video_size="VIDEO_${video_idx}_SIZE"
            video_path=${!var_video_path}
            video_size=${!var_video_size}
            if [ -n "$video_path" ] && [ -n "$video_size" ]; then
                VIDEO_IDX_SIZE_LIST="$VIDEO_IDX_SIZE_LIST $video_idx $video_size"
            fi
        done

        node_name=video_service_app_${XDMA_IDX}_${H2C_IDX}
        NUMA=$(get_numactl $node_name)
        echo "node_name:$node_name, NUMA:$NUMA, H2C_IDX:$H2C_IDX, VIDEO_IDX_SIZE_LIST:$VIDEO_IDX_SIZE_LIST"
        if [ -n "$VIDEO_IDX_SIZE_LIST" ]; then
            start_video_service_app_bg "$NUMA" "$XDMA_IDX" "$H2C_IDX"
            service_sum=$(expr $service_sum + 1)
        else
            echo "VIDEO_IDX_SIZE_LIST empty for XDMA_IDX:$XDMA_IDX, H2C_IDX:$H2C_IDX"
        fi
    done
fi




while true ; do
    running_service_sum=$(ps aux | grep './video_service_app' | grep -v grep | wc -l)
    echo "service_sum:$service_sum, running_service_sum: $running_service_sum"
    if [ "$running_service_sum" -lt "$service_sum" ]; then
        sleep 1
        running_service_sum2=$(ps aux | grep './video_service_app' | grep -v grep | wc -l)
        if [ "$running_service_sum2" -lt "$service_sum" ]; then
            echo "error, some video_service_app not running!"
            exit 1
        fi
    fi
    sleep 1
done