#!/usr/bin/env bash

SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH
source $SCRIPT_PATH/config.sh

if [ $(id -u) == 0 ]; then
    echo "please don't run as root!"
    exit 1
fi 

export DEBIAN_FRONTEND=noninteractive

# disable sudo passwd
echo "$HOST_USER ALL=(ALL:ALL) NOPASSWD: ALL" | sudo tee /etc/sudoers.d/$HOST_USER
sudo chmod 440 /etc/sudoers.d/$HOST_USER

# change ownership
sudo chown -R $HOST_USER:$HOST_USER .


echo $ENABLE_SERVICES | grep "video" &>/dev/null
if [ "$?" == "0" ]; then
    sudo cp -f service/video.service /etc/systemd/system/
    new_line="ExecStart=bash -c 'cd $SCRIPT_PATH/service && ./video_service.sh 2>&1 | tee video_service.log'"
    sudo sed -i "/ExecStart/c$new_line" /etc/systemd/system/video.service
    sudo systemctl daemon-reload
    sudo systemctl enable video
    sudo systemctl restart video
else
    echo "error, ENABLE_SERVICES must contain video!!!"
    exit 1
fi


echo $ENABLE_SERVICES | grep "can" &>/dev/null
if [ "$?" == "0" ]; then
    sudo cp -f service/can.service /etc/systemd/system/
    new_line="ExecStart=bash -c 'cd $SCRIPT_PATH/service && ./can_service.sh 2>&1 | tee can_service.log'"
    sudo sed -i "/ExecStart/c$new_line" /etc/systemd/system/can.service
    sudo systemctl daemon-reload
    sudo systemctl enable can
    sudo systemctl restart can
else
    echo "ENABLE_SERVICES contains no can, skip deploy can.service"
    sudo systemctl disable can
    sudo systemctl stop can
fi


echo $ENABLE_SERVICES | grep "timesync" &>/dev/null
if [ "$?" == "0" ]; then
    sudo cp -f service/timesync.service /etc/systemd/system/
    new_line="ExecStart=bash -c 'cd $SCRIPT_PATH/service && ./timesync_service.sh 2>&1 | tee timesync_service.log'"
    sudo sed -i "/ExecStart/c$new_line" /etc/systemd/system/timesync.service
    new_line="UDPv4    $DUT_LAN_IP"
    sed -i "/UDPv4/c$new_line" $SCRIPT_PATH/service/ptp4l.cfg
    new_line="[$HOST_LAN]"
    sed -i "/eth/c$new_line"   $SCRIPT_PATH/service/ptp4l.cfg
    sudo systemctl daemon-reload
    sudo systemctl enable timesync
    sudo systemctl restart timesync
else
    echo "ENABLE_SERVICES contains no timesync, skip deploy timesync.service"
    sudo systemctl disable timesync
    sudo systemctl stop timesync
fi



BASHRC_PATH=/home/$HOST_USER/.bashrc
echo "add $SCRIPT_PATH/common.sh into $BASHRC_PATH"
sed -i '/common.sh/d' $BASHRC_PATH
echo "source $SCRIPT_PATH/common.sh" >> $BASHRC_PATH


echo "host_deploy ok! plz reopen new terminal"    