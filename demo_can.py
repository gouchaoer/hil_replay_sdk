#!/usr/bin/env python3

import can
import time

if __name__ == "__main__":
    bus0=can.Bus(channel="can0", interface="socketcan")
    bus1=can.Bus(channel="can1", interface="socketcan")
    bus2=can.Bus(channel="can2", interface="socketcan")
    bus3=can.Bus(channel="can3", interface="socketcan")
    while True:
        msg = can.Message(
            arbitration_id=0xC0FFEE, data=[0, 25, 0, 1, 3, 1, 4, 1], is_extended_id=True
        )
        try:
            bus0.send(msg)
            bus1.send(msg)
            bus2.send(msg)
            bus3.send(msg)
        except can.CanError:
            print("Message NOT sent")
        time.sleep(0.5)