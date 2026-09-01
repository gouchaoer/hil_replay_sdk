#!/usr/bin/env bash

if [ $(id -u) != 0 ]; then
    echo "please run as root!"
    exit 1
fi

SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH

export DEBIAN_FRONTEND=noninteractive
#export http_proxy=http://192.168.31.217:8118
#export https_proxy=http://192.168.31.217:8118

lsb_release -a | grep 'Ubuntu 22.04'
if [ "$?" != "0" ]; then
    echo "only support Ubuntu 22.04!!!"
    exit 1
fi

### install libs and tools ### 
cp  -n /etc/apt/sources.list /etc/apt/sources.list.bak
cat > /etc/apt/sources.list <<EOF
deb https://mirrors.aliyun.com/ubuntu/ jammy main restricted universe multiverse
deb https://mirrors.aliyun.com/ubuntu/ jammy-security main restricted universe multiverse
deb https://mirrors.aliyun.com/ubuntu/ jammy-updates main restricted universe multiverse
deb https://mirrors.aliyun.com/ubuntu/ jammy-proposed main restricted universe multiverse
deb https://mirrors.aliyun.com/ubuntu/ jammy-backports main restricted universe multiverse
deb-src https://mirrors.aliyun.com/ubuntu/ jammy main restricted universe multiverse
deb-src https://mirrors.aliyun.com/ubuntu/ jammy-security main restricted universe multiverse
deb-src https://mirrors.aliyun.com/ubuntu/ jammy-updates main restricted universe multiverse
deb-src https://mirrors.aliyun.com/ubuntu/ jammy-proposed main restricted universe multiverse
deb-src https://mirrors.aliyun.com/ubuntu/ jammy-backports main restricted universe multiverse
EOF

apt update -y
#apt upgrade -y




apt install -y vlan can-utils sshpass wget git python3-pip python3-opencv vim numactl htop net-tools \
        linuxptp jq iftop ethtool build-essential ffmpeg mpv iputils-clockdiff python3-tk git libpopt-dev \
        libboost-all-dev nvtop cutecom chrony lm-sensors

# https://carla.readthedocs.io/en/0.9.16/build_linux/
apt install -y build-essential g++-12 cmake ninja-build libvulkan1 python3 python3-dev python3-pip python3-venv \
        autoconf wget curl rsync unzip git git-lfs libpng-dev libtiff5-dev libjpeg-dev aria2 bzip2

if [ ! -e /opt/google/chrome ]; then
    pushd cache
    rm -f google-chrome-stable_current_amd64.deb
    wget https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb
    dpkg -i google-chrome-stable_current_amd64.deb
    rm google-chrome-stable_current_amd64.deb
    popd
fi

# install python3 packages
python3 -m pip install --upgrade pip
python3 -m pip install bitstruct diskcache textparser GitPython cantools python-can
# carla PythonAPI requirements.txt
python3 -m pip install numpy matplotlib rich opencv-python loguru future networkx Shapely pygame matplotlib open3d Pillow



# allow ipv4 forward
new_line="net.ipv4.ip_forward=1"
sed -i "/ip_forward/c$new_line" /etc/sysctl.conf
sysctl -p
# vlan setting
modprobe 8021q
grep 8021q /etc/modules || echo "8021q" >> /etc/modules
# install gcc-12 as default
sudo apt install -y gcc-12 g++-12
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 12
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 12
# install boost-1.90
pushd $SCRIPT_PATH/data
rm -rf boost_1_90_0*
wget https://archives.boost.io/release/1.90.0/source/boost_1_90_0.tar.gz && tar -xzf boost_1_90_0.tar.gz
pushd boost_1_90_0
./bootstrap.sh --prefix=/opt/boost/1.90.0 && ./b2 -j$(nproc) && ./b2 install
popd
rm -rf boost_1_90_0*
popd
# cpu performance mode
apt install -y linux-tools-common linux-tools-$(uname -r) linux-cloud-tools-$(uname -r)
echo 'GOVERNOR="performance"' | sudo tee /etc/default/cpufrequtils
cpupower frequency-set --governor performance
### install libs and tools ### 



### install lowlatency kernel ###
uname -a | grep lowlatency
if [ "$?" != "0" ]; then
    sudo apt-mark unhold linux-image-lowlatency-hwe-22.04-edge linux-headers-lowlatency-hwe-22.04-edge linux-lowlatency-hwe-22.04-edge

    apt install -y linux-lowlatency-hwe-22.04-edge

    apt-mark hold linux-headers-lowlatency-hwe-22.04-edge
    apt-mark hold linux-image-lowlatency-hwe-22.04-edge
    apt-mark hold linux-lowlatency-hwe-22.04-edge
    echo 'APT::Periodic::Update-Package-Lists "1";' > /etc/apt/apt.conf.d/20auto-upgrades
    echo 'APT::Periodic::Unattended-Upgrade "1";'  >> /etc/apt/apt.conf.d/20auto-upgrades
    systemctl disable --now unattended-upgrades
    systemctl disable --now apt-daily.timer
    systemctl disable --now apt-daily-upgrade.timer
    
    echo "ubuntu need reboot, run this script again after reboot..."
    sync
    exit 1
    #sleep 10
    #reboot
fi
cp -f /boot/System.map-$(uname -r) /lib/modules/$(uname -r)/System.map
### install lowlatency kernel ###




### install drivers ###
REBOOT_NEEDED=0

nvidia-smi --query-gpu=driver_version --format=csv,noheader --id=0 | grep 59
if [ "$?" != "0" ]; then
    echo "install nvidia driver 590"
    sudo apt-get remove -y --purge '^nvidia-.*' '^libnvidia-.*' '^cuda-.*'
    sudo apt install -y nvidia-driver-590
    REBOOT_NEEDED=1
fi


modinfo f81601a | grep f81601a &>/dev/null
if [ "$?" != "0" ]; then
    echo "install f81601a driver..."
    pushd cache
    rm -rf f81601a_driver*
    cp ../data/f81601a_driver.zip .
    unzip f81601a_driver.zip
    pushd f81601a_driver
    make
    make install
    modprobe f81601a
    grep f81601a /etc/modules || echo "f81601a" >> /etc/modules
    popd
    popd
    REBOOT_NEEDED=1
fi

modinfo pcan | grep Release_20260126_n &>/dev/null
if [ "$?" != "0" ]; then
    echo "install pcan driver..."
    pushd cache
    rm -rf peak-linux-driver-9.0*
    cp ../data/peak-linux-driver-9.0.tar.gz .
    tar -xzvf peak-linux-driver-9.0.tar.gz
    pushd peak-linux-driver-9.0
    make netdev
    make install
    modprobe pcan
    new_line="options pcan fdusemsi=1"
    sed -i "/pcan fdusemsi/c$new_line" /etc/modprobe.d/pcan.conf
    new_line="options pcan usemsi=1"
    sed -i "/pcan usemsi/c$new_line" /etc/modprobe.d/pcan.conf
    popd
    popd
    REBOOT_NEEDED=1
fi

old_xdma_ko=/lib/modules/$(uname -r)/kernel/drivers/dma/xilinx/xdma.ko
if [ -f $old_xdma_ko ]; then
    echo "install xdma driver..."
    pushd cache
    rm -rf dma_ip_drivers-master*
    cp ../data/dma_ip_drivers-master.zip .
    unzip dma_ip_drivers-master.zip

    pushd dma_ip_drivers-master/XDMA/linux-kernel/xdma
    # Patch XDMA timeout unit to milliseconds and default to 20ms.
    sed -i 's/unsigned int h2c_timeout = 10;/unsigned int h2c_timeout = 20;/g' cdev_sgdma.c
    sed -i 's/unsigned int c2h_timeout = 10;/unsigned int c2h_timeout = 20;/g' cdev_sgdma.c
    sed -i 's/H2C sgdma timeout in seconds, default is 10 sec\./H2C sgdma timeout in milliseconds, default is 20 ms./g' cdev_sgdma.c
    sed -i 's/C2H sgdma timeout in seconds, default is 10 sec\./C2H sgdma timeout in milliseconds, default is 20 ms./g' cdev_sgdma.c
    sed -i 's/h2c_timeout \* 1000/h2c_timeout/g' cdev_sgdma.c
    sed -i 's/c2h_timeout \* 1000/c2h_timeout/g' cdev_sgdma.c
    make
    make install
    sudo bash -c 'echo "options xdma interrupt_mode=1 h2c_timeout=20 c2h_timeout=20" > /etc/modprobe.d/xdma.conf'
    popd
    
    pushd dma_ip_drivers-master/XDMA/linux-kernel/tools
    make
    cp -f dma_from_device dma_to_device performance reg_rw test_chrdev /usr/local/bin/
    popd
    
    popd
    mv $old_xdma_ko $old_xdma_ko.bak
    pushd /lib/modules/$(uname -r)
    depmod -a
    popd
    REBOOT_NEEDED=1
fi

if [ "$REBOOT_NEEDED" == "1" ]; then
    sync
    echo "ubuntu need reboot, run this script again after reboot..."
    exit 1
fi
### install drivers ###




echo "host_install ok!"
