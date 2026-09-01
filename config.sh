#!/usr/bin/env bash

CONFIG_SH_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)


######## core ########
export HOST_USER=ubuntu
export ENABLE_SERVICES="video"
#export ENABLE_SERVICES="video,can,timesync"
export HIL_TS_SRC=CLOCK_REALTIME # CLOCK_REALTIME or clockdiff or /dev/ptp0
export REPLAY_OR_SIM=0 # 0:HIL-Replay 1:HIL-Sim
export BIND_CPU=1 # 0/1, use lscpu.txt to bind process to cpu 
######## core ########




####### video service #######
export REPLAY_AHEAD=15 # set HIL_TS early than VIDEO_0_1ST_TS in seconds
export VIDEO_TS_SUBTRACT=0 # compensate video inject timecost in microseconds

export VIDEO_PERIOD=33333 # only rough estimate
export VIDEO_FSYNC_USE=1 # 0: inject video by video timestamp; 1: inject video by SoC fsync-trigger 
export VIDEO_FSYNC_POL=1 # 0: fsync-trigger by falling edge; 1: fsync-trigger by rising edge
export VIDEO_FSYNC_IDX=0 # fsync-trigger camera index

export VIDEO_A_XDMA=0

export VIDEO_A_01_SIZE_H=1+1536+0
export VIDEO_A_01_SIZE_W=1920
export VIDEO_A_01_NAME=isx031
export VIDEO_A_0_PATH=${CONFIG_SH_PATH}/data/video_a0.h264
export VIDEO_A_1_PATH=${CONFIG_SH_PATH}/data/video_a1.h264

export VIDEO_A_23_SIZE_H=1+1536+0
export VIDEO_A_23_SIZE_W=1920
export VIDEO_A_23_NAME=isx031
export VIDEO_A_2_PATH=${CONFIG_SH_PATH}/data/video_a2.h264
export VIDEO_A_3_PATH=${CONFIG_SH_PATH}/data/video_a3.h264

export VIDEO_A_45_SIZE_H=1+2160+0
export VIDEO_A_45_SIZE_W=3840
export VIDEO_A_45_NAME=imx728
export VIDEO_A_4_PATH=${CONFIG_SH_PATH}/data/video_a4.h264
export VIDEO_A_5_PATH=${CONFIG_SH_PATH}/data/video_a5.h264

export VIDEO_A_67_SIZE_H=2+1280+0
export VIDEO_A_67_SIZE_W=1920
export VIDEO_A_67_NAME=ovx3c
export VIDEO_A_6_PATH=${CONFIG_SH_PATH}/data/video_a6.h264
export VIDEO_A_7_PATH= #${CONFIG_SH_PATH}/data/video_a7.h264

export VIDEO_A_89_SIZE_H=2+1280+0
export VIDEO_A_89_SIZE_W=1920
export VIDEO_A_89_NAME=ovx3c
export VIDEO_A_8_PATH=${CONFIG_SH_PATH}/data/video_a8.h264
export VIDEO_A_9_PATH=${CONFIG_SH_PATH}/data/video_a9.h264

export VIDEO_A_1011_SIZE_H=2+1280+0
export VIDEO_A_1011_SIZE_W=1920
export VIDEO_A_1011_NAME=ovx3c
export VIDEO_A_10_PATH=${CONFIG_SH_PATH}/data/video_a10.h264
export VIDEO_A_11_PATH=${CONFIG_SH_PATH}/data/video_a11.h264




export VIDEO_B_XDMA=

export VIDEO_B_01_SIZE_H=
export VIDEO_B_01_SIZE_W=
export VIDEO_B_01_NAME=
export VIDEO_B_0_PATH=
export VIDEO_B_1_PATH=

export VIDEO_B_23_SIZE_H=
export VIDEO_B_23_SIZE_W=
export VIDEO_B_23_NAME=
export VIDEO_B_2_PATH=
export VIDEO_B_3_PATH=

export VIDEO_B_45_SIZE_H=
export VIDEO_B_45_SIZE_W=
export VIDEO_B_45_NAME=
export VIDEO_B_4_PATH=
export VIDEO_B_5_PATH=

export VIDEO_B_67_SIZE_H=
export VIDEO_B_67_SIZE_W=
export VIDEO_B_67_NAME=
export VIDEO_B_6_PATH=
export VIDEO_B_7_PATH=

export VIDEO_B_89_SIZE_H=
export VIDEO_B_89_SIZE_W=
export VIDEO_B_89_NAME=
export VIDEO_B_8_PATH=
export VIDEO_B_9_PATH=

export VIDEO_B_1011_SIZE_H=
export VIDEO_B_1011_SIZE_W=
export VIDEO_B_1011_NAME=
export VIDEO_B_10_PATH=
export VIDEO_B_11_PATH=
####### video service #######




####### can service #######
export CAN_DRIVER=f81601a #f81601a/pcan
export CAN_0='{"dev":"can0", "fd":0}'
export CAN_1='{"dev":"can1", "fd":0}'
export CAN_2='{"dev":"can2", "fd":1}'
export CAN_3='{"dev":"can3", "fd":1}'
export CAN_4=
export CAN_5=
export CAN_6=
export CAN_7=
####### can service #######




####### timesync service #######
export HOST_WAN=enp193s0
export HOST_LAN=enp194s0
export HOST_LAN_IP=10.44.0.100
export HOST_AS_ROUTER=1  # 0/1: DUT access internet by host wan

export DUT_LAN_IP=10.44.0.28 # DUT: device-under-test
export DUT_USER=root
export DUT_PASS=

export DUT_HOST_SYNC=clockdiff # clockdiff/ptp4l: sync DUT and HOST by clockdiff(by chrony) or ptp4l
####### timesync service #######
