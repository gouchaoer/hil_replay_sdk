#!/usr/bin/env bash

SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)

function is_ch2_offline(){
    [ ! -e /dev/xdma0_h2c_0 ] && return 0
    [ ! -e /dev/xdma0_user ] && return 0
    # MY_AXI4_IP0_BASEADDR: 0x380000
    time_us0=$(reg_rw /dev/xdma0_user 0x380000 | awk 'END{print $NF}')
    time_us1=$(reg_rw /dev/xdma0_user 0x380000 | awk 'END{print $NF}')
    if [ "$time_us0" == "$time_us1" ]; then
        echo "CH2 is offline"
        return 0
    else
        echo "CH2 is online"
        return 1
    fi
}

function remove_ch2_device(){
    local xdma_device="/sys/class/xdma/xdma0_user/device"
    local pci_device
    local pci_bdf

    pci_device=$(readlink -f "$xdma_device") || return 1
    pci_bdf=$(basename "$pci_device")
    if [[ ! "$pci_bdf" =~ ^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]$ ]]; then
        echo "invalid CH2 PCI device: $pci_device"
        return 1
    fi

    if [ -L "$pci_device/driver" ]; then
        echo "unbind CH2 $pci_bdf from $(basename "$(readlink -f "$pci_device/driver")")"
        printf '%s' "$pci_bdf" | sudo tee "$pci_device/driver/unbind" >/dev/null || return 1
    fi

    echo "remove CH2 $pci_bdf from PCI bus"
    printf '1' | sudo tee "$pci_device/remove" >/dev/null || return 1
}

is_ch2_offline && echo "CH2 is offline, flash error!" && exit 1

python3 $SCRIPT_PATH/flash.py BOOT.BIN
if [ "$?" != "0" ]; then
    echo "flash failed!" && exit 1
else
    echo "flash success!"
fi

# poweroff: reg_rw /dev/xdma0_user 0x6A0000 w 0x00001FFF
# delayed poweroff: reg_rw /dev/xdma0_user 0x380024 w 0x40
reg_rw /dev/xdma0_user 0x380024 w 0x40

# remove CH2 device to avoid kernel panic
remove_ch2_device

echo "plz restart server by run: sudo reboot"
