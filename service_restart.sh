#!/usr/bin/env bash
SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH

sudo systemctl restart video
sudo systemctl restart can
sudo systemctl restart timesync