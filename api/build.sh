#!/usr/bin/env bash
SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH

g++ decode_video_app.cpp -L. -ldl  -pthread -lstdc++ -lrt -lboost_system -lhil_api -g  -O2 -o decode_video_app


