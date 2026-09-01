#!/usr/bin/env bash
SCRIPT_DIR=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_DIR

chmod +x dut_ptp4l
chmod +x dut_phc2sys
chmod +x dut_chronyd
cp -f dut.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable dut
systemctl restart dut
echo "install dut service ok"
