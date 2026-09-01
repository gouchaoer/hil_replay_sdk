#!/usr/bin/env python3

import ctypes
import datetime
import os
import sys
import time


def _get_hil_lib() -> ctypes.CDLL:
    if hasattr(_get_hil_lib, "_lib"):
        return _get_hil_lib._lib  # type: ignore[attr-defined]

    script_dir = os.path.dirname(os.path.abspath(__file__))
    lib_path = os.path.join(script_dir, "libhil_api.so")
    lib = ctypes.CDLL(lib_path)
    lib.hil_ts_get.argtypes = []
    lib.hil_ts_get.restype = ctypes.c_int64
    lib.clockdiff_get.argtypes = []
    lib.clockdiff_get.restype = ctypes.c_int64
    lib.clockdiff_set.argtypes = [ctypes.c_int64]
    lib.clockdiff_set.restype = None
    lib.fsync_ts.argtypes = [
        ctypes.POINTER(ctypes.c_int64),
        ctypes.POINTER(ctypes.c_int64),
        ctypes.POINTER(ctypes.c_int64),
    ]
    lib.fsync_ts.restype = ctypes.c_bool
    _get_hil_lib._lib = lib  # type: ignore[attr-defined]
    return lib


def hil_ts_get() -> int:
    return int(_get_hil_lib().hil_ts_get())

def clockdiff_get() -> int:
    diff = int(_get_hil_lib().clockdiff_get())
    return diff

def clockdiff_set(diff_us: int):
    _get_hil_lib().clockdiff_set(diff_us)


def fsync_ts() -> tuple[int, int, int]:
    fsync_mono_us = ctypes.c_int64(0)
    fsync_interval_us = ctypes.c_int64(0)
    fsync_ct = ctypes.c_int64(0)

    ok = bool(
        _get_hil_lib().fsync_ts(
            ctypes.byref(fsync_mono_us),
            ctypes.byref(fsync_interval_us),
            ctypes.byref(fsync_ct),
        )
    )
    if not ok:
        print("fsync_ts error!!!")
        sys.exit(1)

    return int(fsync_mono_us.value), int(fsync_interval_us.value), int(fsync_ct.value)


def _format_ns_local(ns: int) -> str:
    sec = ns // 1_000_000_000
    ms = (ns % 1_000_000_000) // 1_000_000
    dt = datetime.datetime.fromtimestamp(sec)
    return f"{dt:%Y-%m-%d %H:%M:%S}.{ms:03d}"


def get_ts_str() -> str:
    kernel_s = time.monotonic_ns() / 1_000_000_000.0
    real_ns = time.time_ns()
    hil_ns = hil_ts_get()
    return f"[{kernel_s:.3f}][{_format_ns_local(real_ns)}][{_format_ns_local(hil_ns)}]"

if __name__ == "__main__":
    method = len(sys.argv) > 1 and sys.argv[1] or "hil_ts_get"
    if method == "hil_ts_get":
        if sys.argv[-1] == "-q":
            print(get_ts_str(), end="")
        elif sys.argv[-1] == "-0":
            print(f"{time.monotonic_ns() / 1_000_000_000:.6f}", end="")
        elif sys.argv[-1] == "-1":
            print(f"{time.time_ns() / 1_000_000_000:.6f}", end="")
        elif sys.argv[-1] == "-2":
            print(f"{hil_ts_get() / 1_000_000_000:.6f}", end="")
        else:
            print(f"[CLOCK_MONOTONIC][CLOCK_REALTIME][HIL_TS={os.getenv('HIL_TS_SRC', '')}]")
            print(get_ts_str())
    elif method == "clockdiff_get":
        print(clockdiff_get(), end="")
    elif method == "clockdiff_set":
        diff_us = int(sys.argv[2])
        clockdiff_set(diff_us)
    elif method == "fsync_ts":
        fsync_mono_us, fsync_interval_us, fsync_ct = fsync_ts()
        if sys.argv[-1] == "-0":
            print(f"{fsync_mono_us}", end="")
        elif sys.argv[-1] == "-1":
            print(f"{fsync_interval_us}", end="")
        elif sys.argv[-1] == "-2":
            print(f"{fsync_ct}", end="")
        elif sys.argv[-1] == "-q":
            print(f"{fsync_mono_us} {fsync_interval_us} {fsync_ct}", end="")
        else:
            print(
                f"fsync_mono_us:{fsync_mono_us}, "
                f"fsync_interval_us:{fsync_interval_us}, "
                f"fsync_ct:{fsync_ct}"
            )
    else:
        print(f"Unknown method: {method}")
        sys.exit(1)