#!/usr/bin/env bash


SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH
source $SCRIPT_PATH/common.sh


echo "change dut.service"
new_line="ExecStart=/home/$DUT_USER/dut_deploy/dut.sh"
sudo sed -i "/ExecStart/c$new_line" $SCRIPT_PATH/dut_deploy/dut.service


dut_rsync 1 $SCRIPT_PATH/dut_deploy /home/$DUT_USER/
dut_rsync 1 $SCRIPT_PATH/config.sh  /home/$DUT_USER/dut_deploy

echo "remote run deploy.sh on DUT"
dut_exec  0 "echo $DUT_PASS | sudo -S bash /home/$DUT_USER/dut_deploy/deploy.sh"
