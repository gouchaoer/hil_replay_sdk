#!/usr/bin/env python3

import socket
import time
import os

if __name__ == "__main__":
    DUT_LAN_IP = os.environ['DUT_LAN_IP']
    port = 8000
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    while True:
        try:
            message = "hello world"
            sock.sendto(message.encode('utf-8'), (DUT_LAN_IP, port))
        except Exception as e:
            print(f"error: {e}")
        time.sleep(1)