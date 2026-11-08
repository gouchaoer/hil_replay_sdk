#!/usr/bin/env python3

import os
import struct
import sys
import time

USER_DEVICE = "/dev/xdma0_user"
MY_AXI4_IP0_BASEADDR_OFF = 0x380000
REG9_OFFSET = 9 * 4
I2C_BIT = 1 << 3
TIMEOUT_S = 5



def read_u32_at(fd: int, offset: int) -> int:
    data = os.pread(fd, 4, offset)
    if len(data) != 4:
        raise OSError(f"short read at 0x{offset:X}, len={len(data)}")
    return struct.unpack("<I", data)[0]


def write_u32_at(fd: int, offset: int, value: int) -> None:
    data = struct.pack("<I", value & 0xFFFFFFFF)
    n = os.pwrite(fd, data, offset)
    if n != 4:
        raise OSError(f"short write at 0x{offset:X}, wrote={n}")


def main() -> int:
    if not os.path.exists(USER_DEVICE):
        print(f"device not found: {USER_DEVICE}", file=sys.stderr)
        return 3

    reg9_addr = MY_AXI4_IP0_BASEADDR_OFF + REG9_OFFSET
    user_fd = os.open(USER_DEVICE, os.O_RDWR | os.O_SYNC)
    try:
        reg9_before = read_u32_at(user_fd, reg9_addr)
        reg9_trigger = reg9_before | I2C_BIT
        write_u32_at(user_fd, reg9_addr, reg9_trigger)

        deadline = time.monotonic() + TIMEOUT_S
        while time.monotonic() < deadline:
            reg9_now = read_u32_at(user_fd, reg9_addr)
            if (reg9_now & I2C_BIT) == 0:
                print(
                    f"I2C check done: REG9 0x{reg9_before:08X} -> "
                    f"0x{reg9_trigger:08X} -> 0x{reg9_now:08X}"
                )
                return 0
            time.sleep(0.1)

        print(f"timeout waiting REG9 bit3 clear", file=sys.stderr)
        return 4
    finally:
        os.close(user_fd)


if __name__ == "__main__":
    raise SystemExit(main())

