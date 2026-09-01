#!/usr/bin/env bash
SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH

g++ ue4_mock.cpp -L. -I/opt/boost/1.90.0/include -ldl  -pthread -lstdc++ -lrt -lhil_api -g  -O2 -o ue4_mock



