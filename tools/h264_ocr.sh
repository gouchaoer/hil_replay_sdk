#!/usr/bin/env bash

SCRIPT_DIR=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_DIR


function envs_init(){
    declare -A ENVS_ENUM=(
        [VIDEO_PATH]='tmp.h264'
        )
    
    echo -n "Usage:"
    for env_name in ${!ENVS_ENUM[@]}; do
        env_enum=${ENVS_ENUM[$env_name]}
        echo -n "  [$env_name=$env_enum]"
    done
    echo -e " $0\n"

    for env_name in ${!ENVS_ENUM[@]}; do
        env_enum=${ENVS_ENUM[$env_name]}
        env_value_default=$(echo $env_enum | awk -F'/' '{print $1}')
        declare -n enum_ref=$env_name
        if [ -z "$enum_ref" ]; then
            enum_ref=$env_value_default
            printf "%24s:%24s%12s\n" "$env_name" "$enum_ref" "(default)"
        else
            printf "%24s:%24s%12s\n" "$env_name" "$enum_ref" ""
        fi
        export $env_name=$enum_ref
    done
    echo ''
}

envs_init

[ ! -e $VIDEO_PATH ] && echo "error, VIDEO_PATH:$VIDEO_PATH not found!!!" && exit 1

rm -rf tmp.*
mkdir tmp.dir

ffmpeg -i $VIDEO_PATH -vsync 0 tmp.dir/%04d.jpg

python3 ../video.py parse_timestamp $VIDEO_PATH
python3 h264.py ocr_img
python3 h264.py ocr_delay
#VIDEO_PATH=../data/video_a0.h264 python3 h264.py ocr_img
