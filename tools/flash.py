#!/usr/bin/env python3

import argparse
import errno
import hashlib
import os
import struct
import sys
import time

IMG_MAX_SZ = 0x2000000

PSPL_FLASH_MD5_ADDR = (0x40000000 + IMG_MAX_SZ)
PSPL_FLASH_LEN_ADDR = PSPL_FLASH_MD5_ADDR + 16
PSPL_FLASH_FILENAME_ADDR = PSPL_FLASH_LEN_ADDR + 4
PSPL_FLASH_CONTENT_ADDR = PSPL_FLASH_FILENAME_ADDR + 256
PSPL_FLASH_MAX_LEN = PSPL_FLASH_MD5_ADDR + IMG_MAX_SZ * 3 - PSPL_FLASH_CONTENT_ADDR

MY_AXI4_IP0_BASEADDR_OFF = 0x380000
REG9_OFFSET = 9 * 4
SET_CONFIG_FLASH_BIT = 1 << 4
SET_CONFIG_FLASH_OK_BIT = 1 << 5


def parse_xdma_idx(text: str) -> int:
    try:
        value = int(text, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid xdma_idx: {text}") from exc
    if value < 0:
        raise argparse.ArgumentTypeError(f"xdma_idx must be >= 0: {text}")
    return value


def resolve_h2c_device(xdma_idx: int) -> str:
    candidates = [
        f"/dev/xdma{xdma_idx}_h2c_0",
    ]
    for dev in candidates:
        if os.path.exists(dev):
            return dev
    return candidates[0]


def resolve_user_device(xdma_idx: int) -> str:
    candidates = [
        f"/dev/xdma{xdma_idx}_user",
    ]
    for dev in candidates:
        if os.path.exists(dev):
            return dev
    return candidates[0]


def write_all_at(fd: int, offset: int, data: bytes) -> None:
    pos = 0
    while pos < len(data):
        os.lseek(fd, offset + pos, os.SEEK_SET)
        n = os.write(fd, data[pos:])
        if n <= 0:
            raise OSError(f"write failed at offset 0x{offset + pos:X}")
        pos += n


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
    parser = argparse.ArgumentParser(
        description="send file to CH2 sd-card via pcie"
    )
    parser.add_argument("file_path", help="file path")
    parser.add_argument(
        "xdma_idx",
        nargs="?",
        type=parse_xdma_idx,
        default=0,
        help="optional xdma index (default: 0)",
    )
    args = parser.parse_args()

    file_abspath = os.path.abspath(args.file_path)
    if not os.path.isfile(file_abspath):
        print(f"file_abspath not found: {file_abspath}", file=sys.stderr)
        return 2

    with open(file_abspath, "rb") as f:
        payload = f.read()

    length = len(payload)
    if length == 0 or length > PSPL_FLASH_MAX_LEN:
        print(
            f"invalid file_abspath length: {length}, allowed range: 1..{PSPL_FLASH_MAX_LEN}",
            file=sys.stderr,
        )
        return 2

    md5_digest = hashlib.md5(payload).digest()
    filename_bytes = os.path.basename(file_abspath).encode("utf-8")
    if len(filename_bytes) == 0 or len(filename_bytes) > 255:
        print("filename must be 1..255 bytes in UTF-8", file=sys.stderr)
        return 2
    filename_cstr = filename_bytes + b"\0"
    filename_cstr = filename_cstr.ljust(256, b"\0")

    h2c_device = resolve_h2c_device(args.xdma_idx)
    user_device = resolve_user_device(args.xdma_idx)

    print("----- flash.py -----")
    print(f"file_abspath={file_abspath}")
    print(f"xdma_idx={args.xdma_idx}")
    print(f"filename={filename_bytes.decode('utf-8', errors='replace')}")
    print(f"len={length}")
    print(f"md5={md5_digest.hex()}")
    print(f"h2c_device={h2c_device}")
    print(f"user_device={user_device}")

    if not os.path.exists(h2c_device):
        print(f"h2c device not found: {h2c_device}", file=sys.stderr)
        return 3
    if not os.path.exists(user_device):
        print(f"user device not found: {user_device}", file=sys.stderr)
        return 3

    h2c_fd = os.open(h2c_device, os.O_RDWR | os.O_SYNC)
    try:
        write_all_at(h2c_fd, PSPL_FLASH_MD5_ADDR, md5_digest)
        write_all_at(h2c_fd, PSPL_FLASH_LEN_ADDR, struct.pack("<I", length))
        write_all_at(h2c_fd, PSPL_FLASH_FILENAME_ADDR, filename_cstr)

        chunk_size = 1024 * 1024
        offset = 0
        while offset < length:
            end = min(offset + chunk_size, length)
            write_all_at(h2c_fd, PSPL_FLASH_CONTENT_ADDR + offset, payload[offset:end])
            offset = end

        try:
            os.fsync(h2c_fd)
        except OSError as exc:
            if exc.errno != errno.EINVAL:
                raise
            #print("warning: fsync is not supported by this xdma h2c device, continue")
    finally:
        os.close(h2c_fd)

    reg9_addr = MY_AXI4_IP0_BASEADDR_OFF + REG9_OFFSET
    user_fd = os.open(user_device, os.O_RDWR | os.O_SYNC)
    try:
        reg9_before = read_u32_at(user_fd, reg9_addr)
        reg9_prepare = reg9_before & ~SET_CONFIG_FLASH_OK_BIT
        write_u32_at(user_fd, reg9_addr, reg9_prepare)

        reg9_trigger = reg9_prepare | SET_CONFIG_FLASH_BIT
        write_u32_at(user_fd, reg9_addr, reg9_trigger)

        deadline = time.monotonic() + 120.0
        while True:
            reg9_now = read_u32_at(user_fd, reg9_addr)
            if (reg9_now & SET_CONFIG_FLASH_BIT) == 0:
                flash_ok = (reg9_now & SET_CONFIG_FLASH_OK_BIT) != 0
                print(
                    f"flash trigger ok: reg9 before=0x{reg9_before:08X}, "
                    f"after_trigger=0x{reg9_trigger:08X}, final=0x{reg9_now:08X}"
                )
                print("--------------------")
                return 0 if flash_ok else 5
            if time.monotonic() > deadline:
                print(
                    f"timeout waiting reg9.bit4 auto-clear, reg9=0x{reg9_now:08X}",
                    file=sys.stderr,
                )
                return 4
            time.sleep(0.2)
    finally:
        os.close(user_fd)


if __name__ == "__main__":
    raise SystemExit(main())
