#!/usr/bin/env bash
if [ $(id -u) == 0 ]; then
    echo "please do not run as root!"
    exit 1
fi

SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH
source $SCRIPT_PATH/config.sh



if [ "$1" == "query" ] || [ "$1" == "q" ];  then
    python3 monitor.py query
    exit $?
else
    sudo rm -f monitor.log 
    python3 monitor.py
fi