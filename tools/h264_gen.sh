#!/usr/bin/env bash

SCRIPT_DIR=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_DIR


function envs_init(){
    declare -A ENVS_ENUM=(
        [RES_W]='1920'
        [RES_H]='1536'
        [FPS]='30'
        [SEI_START_TIME_US]='1735696800.123456' # 20250101T100000.123456
        [FRAME_SUM]='9999' # FRAME_SUM < 9999
        [COLOR]='blue/orange/yellow/green/red/cyan/purple'
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


rm -rf tmp.*
mkdir tmp.dir
python3 h264.py gen_img
ffmpeg -framerate $FPS -i tmp.dir/%04d.png  -c:v libx264  -pix_fmt yuv420p  -intra  tmp.raw.h264
python3 h264.py edit_sei
