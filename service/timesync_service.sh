#!/usr/bin/env bash

if [ $(id -u) != 0 ]; then
    echo "please run as root!"
    exit 1
fi

SCRIPT_PATH=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $SCRIPT_PATH

source $SCRIPT_PATH/../common.sh

prepare_env

ps aux | grep 'timesync_service.sh' |  grep -v 'bash -c' | grep -v grep | wc -l | grep 1 &>/dev/null
[ "$?" != "0" ] && echo "error, another timesync_service.sh running!" && exit 1

# clean env
function clean(){
    sudo pkill -9 -f './phc2sys'
    sudo pkill -9 -f './ptp4l'
    sudo systemctl disable chrony
    sudo systemctl stop chrony
}
clean
trap clean EXIT


# wait HOST_WAN ready
while true ; do
    wan_ip=$(ifconfig $HOST_WAN | grep 'inet ' | awk '{print $2}' | tr '.' 'x')
    [ -n "$wan_ip" ] && break
    echo "wait HOST_WAN:$HOST_WAN 1s to get ip..."
    sleep 1
done


# wait HOST_LAN ready
while true; do
    ifconfig $HOST_LAN | grep "inet $HOST_LAN_IP" &>/dev/null
    [ "$?" == "0" ] && echo "HOST_LAN:$HOST_LAN is ready" && break
    echo "wait 1s for ready:$ready get $HOST_LAN_IP ip..."
	sudo ip route add 239.0.0.0/8 dev $HOST_LAN # enable multicast
    sleep 1
done


# HOST_AS_ROUTER
if [ "$HOST_AS_ROUTER" == "1" ]; then
	echo "HOST_AS_ROUTER=$HOST_AS_ROUTER, changing iptables..." 
    sudo iptables -P INPUT ACCEPT
    sudo iptables -P FORWARD ACCEPT
    sudo iptables -t nat -A POSTROUTING -s 10.42.0.0/24 -o $HOST_WAN -j MASQUERADE
fi





# ptp4l
function check_ptp4l_job(){
    while true; do
        output=$(pmc -u -b 0 "GET PORT_DATA_SET" 2>&1 | grep -q "SLAVE")
        ec=$?
        ts_str=$(get_ts_str)
        echo "output: $output, ec:$ec"
        if [ "$ec" == "0" ]; then
            echo "$ts_str DUT_HOST_SYNC:$DUT_HOST_SYNC, sync ok" > /dev/shm/MONITOR__timesync__OKAY
        else
            echo "$ts_str DUT_HOST_SYNC:$DUT_HOST_SYNC, sync error" > /dev/shm/MONITOR__timesync__ERROR
        fi
        sleep 1
    done
}
if [ "$DUT_HOST_SYNC" == "ptp4l" ]; then
    echo "DUT_HOST_SYNC=$DUT_HOST_SYNC, starting ptp4l..." 
    #echo "run phc2sys..."
    #sudo phc_ctl $HOST_LAN set
    #sudo ./phc2sys  -s $HOST_LAN -c CLOCK_REALTIME -O 0 \
    #    --step_threshold=1 -m --use_syslog=0 &> phc2sys.log &
    cp -f ptp4l.cfg.template ptp4l.cfg
    sed -i "s#DUT_LAN_IP#$DUT_LAN_IP#g" ptp4l.cfg
    sed -i "s#HOST_LAN#$HOST_LAN#g" ptp4l.cfg

    echo "run ptp4l..."
    sudo ./ptp4l -m --clientOnly=1  --step_threshold=1 -f ptp4l.cfg --use_syslog=0 -l 7 &> ptp4l.log &

    check_ptp4l_job &
fi




# start check_clockdiff backgroud job
function update_clockdiff_job(){
    while true; do
        clockdiff=$(echo "10 * 365 * 24 * 3600 * 1000000" | bc) # default 10 years(us) if get failed
        output=$(timeout 1 chronyd -Q "server $DUT_LAN_IP iburst maxsamples 1" 2>&1 | awk '/System clock/ {print $6}')
        ec=$?
        ts_str=$(get_ts_str)
        echo "output: $output, ec:$ec"
        if [ "$ec" == "0" ] && [[ "$output" =~ ^-?[0-9]+(\.[0-9]+)?$ ]]; then
            clockdiff=$(python3 -c "import sys; print(int(float(sys.argv[1]) * 1e6))" "$output")
            echo "$ts_str DUT_HOST_SYNC:$DUT_HOST_SYNC, clockdiff(us):${clockdiff}" > /dev/shm/MONITOR__timesync__OKAY
        else
            echo "$ts_str DUT_HOST_SYNC:$DUT_HOST_SYNC, clockdiff(us):Not Available" > /dev/shm/MONITOR__timesync__ERROR
        fi
        python3 $CONFIG_SH_PATH/api/hil_api.py clockdiff_set $clockdiff
        [ "$?" != "0" ] && echo "clockdiff_set error!!!" && exit 1
        sleep 1
    done
}
if [ "$DUT_HOST_SYNC" == "clockdiff" ]; then
    update_clockdiff_job &
fi





# start correct_route backgroud job
function correct_route_job(){
    while true; do
        # avoid HOST_LAN as default route
        route -n | grep '^0.0.0.0' | grep  $HOST_WAN &>/dev/null && \
        route -n | grep '^0.0.0.0' | grep  $HOST_LAN &>/dev/null && \
        echo "remove $HOST_LAN default route..." && \
        sudo route del default gw $HOST_LAN_IP dev $HOST_LAN

        # enable multicast
        route -n | grep '^239.0.0.0' | grep  $HOST_LAN &>/dev/null
        if [ "$?" != "0" ] ; then
            echo "enable HOST_LAN:$HOST_LAN multicast..."
            sudo ip route add 239.0.0.0/8 dev $HOST_LAN 
            sudo ip link set dev $HOST_LAN multicast on
        fi
        sleep 1
    done
}
correct_route_job &




sleep infinity