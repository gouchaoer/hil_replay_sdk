#!/usr/bin/env bash
if [ $(id -u) == 0 ]; then
    echo "please do not run as root!"
    exit 1
fi
SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH
source $SCRIPT_PATH/common.sh


prepare_env


function clean(){
    sudo pkill -f decode_video_app
    sudo pkill -f demo_can.sh
    sudo pkill -f demo_udp.sh
}
trap clean EXIT


function parse_video_ts(){
    echo -e "\n##### parse_video_ts start #####"
    for i in $(seq 0 11) ; do
        video_path_var="VIDEO_A_${i}_PATH"
        if [ -e "${!video_path_var}" ]; then
            export VIDEO_PATH="${!video_path_var}"
            python3 $HIL_SDK_PATH/tools/h264.py parse_timestamp &
        else
            echo "$video_path_var skip..."
        fi
        video_path_var="VIDEO_B_${i}_PATH"
        if [ -e "${!video_path_var}" ]; then
            export VIDEO_PATH="${!video_path_var}"
            python3 $HIL_SDK_PATH/tools/h264.py parse_timestamp &
        else
            echo "$video_path_var skip..."
        fi
    done
    while true; do
        job_ct=$(ps aux | grep 'parse_timestamp' | grep -v grep | wc -l)
        [ "$job_ct" == "0" ] && break
        echo "parse_video_ts jobs:$job_ct running..."
        sleep 1
    done
    echo -e "\n\n"
}
parse_video_ts


function set_start_ts(){
    echo "##### set_start_ts start #####"
    VIDEO_0_1ST_TS=$(head -n 1 $VIDEO_A_0_PATH.ts)
    [ -z "$VIDEO_0_1ST_TS" ] && echo "VIDEO_0_1ST_TS empty"  && exit 1
    video_0_1st_date=$(date -d "@$VIDEO_0_1ST_TS" +"%Y-%m-%d %H:%M:%S")

    ts=$(date -d "@$VIDEO_0_1ST_TS" +%s)
    replay_start_hil_ts=$(expr $ts - $REPLAY_AHEAD) 
    replay_start_hil_date=$(date -d "@$replay_start_hil_ts" +"%Y-%m-%d %H:%M:%S")
    echo "VIDEO_0_1ST_TS:$VIDEO_0_1ST_TS($video_0_1st_date), REPLAY_AHEAD:$REPLAY_AHEAD, replay_start_hil_ts:$replay_start_hil_ts($replay_start_hil_date)"

    echo $ENABLE_SERVICES | grep "timesync" &>/dev/null
    if [ "$?" == "0" ]; then
        echo "change date on DUT..."
        dut_exec 5 "echo $DUT_PASS | sudo date -s '$replay_start_hil_date'"
        if [ "$DUT_HOST_SYNC" == "chrony" ]; then
            echo "restart chrony to force ntp sync with $DUT_LAN_IP..."
            sudo systemctl restart chrony
        fi
        sleep 2 # wait monitor update status
        if [ "$DUT_HOST_SYNC" != "0" ]; then
            TIMEOUT=$REPLAY_AHEAD
            while true ; do
                timesync_status=$($HIL_SDK_PATH/monitor.sh query | grep timesync)
                echo $timesync_status
                echo $timesync_status | grep OKAY &> /dev/null
                if [ "$?" == "0" ]; then
                    echo "clockdiff(ms)=0, ok..."
                    break
                else
                    echo "clockdiff(ms)>0, TIMEOUT:$TIMEOUT..."
                    TIMEOUT=$((TIMEOUT-1))
                    [ "$TIMEOUT" -lt "0" ] && echo "timeout error!!! exit 1" && exit 1
                fi
                sleep 1
            done
        fi
    else
        echo "timesync disabled, directly change date on HOST..."
        sudo date -s "$replay_start_hil_date"
    fi
    echo -e "\n\n"
}
set_start_ts



echo "##### start demo_can.sh #####"
setsid ./demo_can.sh &> demo_can.log < /dev/null &
echo ""



echo "##### start demo_udp.sh #####"
setsid ./demo_udp.sh &> demo_udp.log < /dev/null &
echo ""

function decode_video_app_A(){
    if [ "$VIDEO_A_XDMA" != "" ]; then
        echo "##### start decode_video_app_A #####"
        
        XDMA_IDX=$VIDEO_A_XDMA
        for video_idx in $(seq 0 11); do
            var_video_path="VIDEO_A_${video_idx}_PATH"
            VIDEO_PATH=${!var_video_path}
            [ ! -e "$VIDEO_PATH" ] && echo "$var_video_path: $VIDEO_PATH not found, skip..." && continue

            [ "$video_idx" == "0" ] && VIDEO_SIZE_H=$VIDEO_A_01_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_01_SIZE_W && VIDEO_NAME=$VIDEO_A_01_NAME
            [ "$video_idx" == "1" ] && VIDEO_SIZE_H=$VIDEO_A_01_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_01_SIZE_W && VIDEO_NAME=$VIDEO_A_01_NAME
            [ "$video_idx" == "2" ] && VIDEO_SIZE_H=$VIDEO_A_23_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_23_SIZE_W && VIDEO_NAME=$VIDEO_A_23_NAME
            [ "$video_idx" == "3" ] && VIDEO_SIZE_H=$VIDEO_A_23_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_23_SIZE_W && VIDEO_NAME=$VIDEO_A_23_NAME
            [ "$video_idx" == "4" ] && VIDEO_SIZE_H=$VIDEO_A_45_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_45_SIZE_W && VIDEO_NAME=$VIDEO_A_45_NAME
            [ "$video_idx" == "5" ] && VIDEO_SIZE_H=$VIDEO_A_45_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_45_SIZE_W && VIDEO_NAME=$VIDEO_A_45_NAME
            [ "$video_idx" == "6" ] && VIDEO_SIZE_H=$VIDEO_A_67_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_67_SIZE_W && VIDEO_NAME=$VIDEO_A_67_NAME
            [ "$video_idx" == "7" ] && VIDEO_SIZE_H=$VIDEO_A_67_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_67_SIZE_W && VIDEO_NAME=$VIDEO_A_67_NAME
            [ "$video_idx" == "8" ] && VIDEO_SIZE_H=$VIDEO_A_89_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_89_SIZE_W && VIDEO_NAME=$VIDEO_A_89_NAME
            [ "$video_idx" == "9" ] && VIDEO_SIZE_H=$VIDEO_A_89_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_89_SIZE_W && VIDEO_NAME=$VIDEO_A_89_NAME
            [ "$video_idx" == "10" ] && VIDEO_SIZE_H=$VIDEO_A_1011_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_1011_SIZE_W && VIDEO_NAME=$VIDEO_A_1011_NAME
            [ "$video_idx" == "11" ] && VIDEO_SIZE_H=$VIDEO_A_1011_SIZE_H && VIDEO_SIZE_W=$VIDEO_A_1011_SIZE_W && VIDEO_NAME=$VIDEO_A_1011_NAME

            [ -z "$VIDEO_SIZE_H" ] && echo "error, $VIDEO_PATH VIDEO_SIZE_H empty!!!" && exit 1
            [ -z "$VIDEO_SIZE_W" ] && echo "error, $VIDEO_PATH VIDEO_SIZE_W empty!!!" && exit 1
            [ -z "$VIDEO_NAME" ] && echo "error, $VIDEO_PATH VIDEO_NAME empty!!!" && exit 1

            export VIDEO_SIZE_H
            export VIDEO_SIZE_W
            export VIDEO_PATH
            export VIDEO_NAME

            node_name=decode_video_app_${XDMA_IDX}_${video_idx}
            NUMA=$(get_numactl $node_name)
            [ "$?" != "0" ] && echo "$NUMA" && exit 1

            #[ "$video_idx" != "4" ] && continue # only decode video_idx=4 for test

            echo "video_idx:$video_idx, VIDEO_PATH:$VIDEO_PATH, xdma:$XDMA_IDX, node_name:$node_name, NUMA:$NUMA, VIDEO_SIZE_H:$VIDEO_SIZE_H, VIDEO_SIZE_W:$VIDEO_SIZE_W"
            $NUMA stdbuf -oL -eL ./api/decode_video_app $XDMA_IDX $video_idx &> decode_video.$XDMA_IDX.$video_idx.log &
            
            export VIDEO_SUM=$(expr $VIDEO_SUM + 1)
        done
        echo -e "\n\n"
    fi
}
decode_video_app_A



function decode_video_app_B(){
    if [ "$VIDEO_B_XDMA" != "" ]; then
        echo "##### start decode_video_app_B #####"

        XDMA_IDX=$VIDEO_B_XDMA
        for video_idx in $(seq 0 11); do
            var_video_path="VIDEO_B_${video_idx}_PATH"
            VIDEO_PATH=${!var_video_path}
            [ ! -e "$VIDEO_PATH" ] && echo "$var_video_path: $VIDEO_PATH not found, skip..." && continue

            [ "$video_idx" == "0" ] && VIDEO_SIZE_H=$VIDEO_B_01_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_01_SIZE_W && VIDEO_NAME=$VIDEO_B_01_NAME
            [ "$video_idx" == "1" ] && VIDEO_SIZE_H=$VIDEO_B_01_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_01_SIZE_W && VIDEO_NAME=$VIDEO_B_01_NAME
            [ "$video_idx" == "2" ] && VIDEO_SIZE_H=$VIDEO_B_23_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_23_SIZE_W && VIDEO_NAME=$VIDEO_B_23_NAME
            [ "$video_idx" == "3" ] && VIDEO_SIZE_H=$VIDEO_B_23_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_23_SIZE_W && VIDEO_NAME=$VIDEO_B_23_NAME
            [ "$video_idx" == "4" ] && VIDEO_SIZE_H=$VIDEO_B_45_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_45_SIZE_W && VIDEO_NAME=$VIDEO_B_45_NAME
            [ "$video_idx" == "5" ] && VIDEO_SIZE_H=$VIDEO_B_45_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_45_SIZE_W && VIDEO_NAME=$VIDEO_B_45_NAME
            [ "$video_idx" == "6" ] && VIDEO_SIZE_H=$VIDEO_B_67_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_67_SIZE_W && VIDEO_NAME=$VIDEO_B_67_NAME
            [ "$video_idx" == "7" ] && VIDEO_SIZE_H=$VIDEO_B_67_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_67_SIZE_W && VIDEO_NAME=$VIDEO_B_67_NAME
            [ "$video_idx" == "8" ] && VIDEO_SIZE_H=$VIDEO_B_89_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_89_SIZE_W && VIDEO_NAME=$VIDEO_B_89_NAME
            [ "$video_idx" == "9" ] && VIDEO_SIZE_H=$VIDEO_B_89_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_89_SIZE_W && VIDEO_NAME=$VIDEO_B_89_NAME
            [ "$video_idx" == "10" ] && VIDEO_SIZE_H=$VIDEO_B_1011_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_1011_SIZE_W && VIDEO_NAME=$VIDEO_B_1011_NAME
            [ "$video_idx" == "11" ] && VIDEO_SIZE_H=$VIDEO_B_1011_SIZE_H && VIDEO_SIZE_W=$VIDEO_B_1011_SIZE_W && VIDEO_NAME=$VIDEO_B_1011_NAME

            [ -z "$VIDEO_SIZE_H" ] && echo "error, $VIDEO_PATH VIDEO_SIZE_H empty!!!" && exit 1
            [ -z "$VIDEO_SIZE_W" ] && echo "error, $VIDEO_PATH VIDEO_SIZE_W empty!!!" && exit 1
            [ -z "$VIDEO_NAME" ] && echo "error, $VIDEO_PATH VIDEO_NAME empty!!!" && exit 1

            export VIDEO_SIZE_H
            export VIDEO_SIZE_W
            export VIDEO_PATH
            export VIDEO_NAME

            node_name=decode_video_app_${XDMA_IDX}_${video_idx}
            NUMA=$(get_numactl $node_name)
            [ "$?" != "0" ] && echo "$NUMA" && exit 1

            echo "video_idx:$video_idx, VIDEO_PATH:$VIDEO_PATH, xdma:$XDMA_IDX, node_name:$node_name, NUMA:$NUMA, VIDEO_SIZE_H:$VIDEO_SIZE_H, VIDEO_SIZE_W:$VIDEO_SIZE_W"
            $NUMA stdbuf -oL -eL ./api/decode_video_app $XDMA_IDX $video_idx &> decode_video.$XDMA_IDX.$video_idx.log &
            
            export VIDEO_SUM=$(expr $VIDEO_SUM + 1)
        done
    echo -e "\n\n"
    fi
}
decode_video_app_B


function wait_replay_start(){
    echo -e "\n##### wait_replay_start #####"
    while true ; do
        running_sum=$(ps aux | grep './api/decode_video_app' | grep -v grep | wc -l)

        date_str=$(python3 api/hil_api.py hil_ts_get  -q)
        hil_ts_now=$(python3 api/hil_api.py hil_ts_get  -2)
        VIDEO_0_1ST_TS=$(head -n 1 $VIDEO_A_0_PATH.ts)
        wait_ts=$(echo "($VIDEO_0_1ST_TS - $hil_ts_now)/1" | bc)
        if [ "$wait_ts" -gt "0" ]; then
            echo "wait:${wait_ts}s(VIDEO_0_1ST_TS:$VIDEO_0_1ST_TS, hil_ts_now:$hil_ts_now)"
        else
            echo "$date_str VIDEO_SUM:$VIDEO_SUM, running_sum: $running_sum"
            [ "$running_sum" == "0" ] && break
        fi
        sleep 1
    done
    # ffmpeg -i tmp.final.h264 -f null -
    # ffmpeg -y -c:v h264_cuvid -i tmp.final.h264  -pix_fmt yuyv422 -f rawvideo  /dev/null
}
wait_replay_start