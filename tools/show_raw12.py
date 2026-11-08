#!/usr/bin/env python3
import argparse
import os
import re
import sys

import cv2
import numpy as np


WIDTH = 3840
HEIGHT = 2161
STRIDE = WIDTH * 12 // 8  # RAW12 packed stride in bytes
RAW_FILE = "frame_1.raw"

BAYER_TO_CV = {
    "BGGR": cv2.COLOR_BayerBG2BGR,
    "GBRG": cv2.COLOR_BayerGB2BGR,
    "GRBG": cv2.COLOR_BayerGR2BGR,
    "RGGB": cv2.COLOR_BayerRG2BGR,
}


def unpack_raw12_line(line_bytes: bytes, width: int) -> np.ndarray:
    """Unpack one RAW12 packed line (2 pixels in 3 bytes) into uint16 pixels."""
    groups = width // 2
    arr = np.frombuffer(line_bytes, dtype=np.uint8)
    arr = arr.reshape(groups, 3)

    b0 = arr[:, 0].astype(np.uint16)
    b1 = arr[:, 1].astype(np.uint16)
    b2 = arr[:, 2].astype(np.uint16)

    # RAW12 packing:
    # p0[11:4] -> b0, p1[11:4] -> b1, b2[3:0]=p0[3:0], b2[7:4]=p1[3:0]
    # Left-align to 16-bit output so low 4 bits are zero.
    p0 = ((b0 << 4) | (b2 & 0x0F)) << 4
    p1 = ((b1 << 4) | (b2 >> 4)) << 4

    out = np.empty(width, dtype=np.uint16)
    out[0::2] = p0
    out[1::2] = p1
    return out


def load_raw12_image(path: str, width: int, height: int, stride: int) -> np.ndarray:
    expected_size = height * stride
    actual_size = os.path.getsize(path)
    if actual_size < expected_size:
        raise ValueError(
            f"file too small: {path}, expect at least {expected_size} bytes, got {actual_size}"
        )

    with open(path, "rb") as f:
        data = f.read(expected_size)

    img = np.empty((height, width), dtype=np.uint16)
    for y in range(height):
        row = data[y * stride : y * stride + stride]
        img[y, :] = unpack_raw12_line(row[: (width * 12) // 8], width)

    return img


def to_preview_u8(img12: np.ndarray, black: int, white: int) -> np.ndarray:
    if white <= black:
        white = black + 1
    x = img12.astype(np.int32)
    x = np.clip(x, black, white)
    x = ((x - black) * 255 // (white - black)).astype(np.uint8)
    return x


def auto_white_balance_bgr(img_bgr_u8: np.ndarray) -> np.ndarray:
    """Apply gray-world white balance on 8-bit BGR image."""
    b = img_bgr_u8[:, :, 0].astype(np.float32)
    g = img_bgr_u8[:, :, 1].astype(np.float32)
    r = img_bgr_u8[:, :, 2].astype(np.float32)

    mb = float(np.mean(b)) + 1e-6
    mg = float(np.mean(g)) + 1e-6
    mr = float(np.mean(r)) + 1e-6
    m = (mb + mg + mr) / 3.0

    gb = np.clip(m / mb, 0.5, 4.0)
    gg = np.clip(m / mg, 0.5, 4.0)
    gr = np.clip(m / mr, 0.5, 4.0)

    out = img_bgr_u8.astype(np.float32)
    out[:, :, 0] *= gb
    out[:, :, 1] *= gg
    out[:, :, 2] *= gr
    return np.clip(out, 0, 255).astype(np.uint8)


def default_pad_raw_name(raw_path: str) -> str:
    base = os.path.basename(raw_path)
    m = re.match(r"^(frame_\d+)\.raw$", base)
    if m is not None:
        return m.group(1) + ".pad.raw"
    stem, _ = os.path.splitext(base)
    return stem + ".pad.raw"


def main() -> int:
    parser = argparse.ArgumentParser(description="View frame_1.raw as RAW12")
    parser.add_argument("--black", type=int, default=-1, help="Black level; <0 means auto from p1")
    parser.add_argument("--white", type=int, default=-1, help="White level; <0 means auto from p99")
    parser.add_argument("--bayer", choices=["BGGR", "GBRG", "GRBG", "RGGB"], default="BGGR", help="Bayer pattern")
    parser.add_argument("--no-awb", action="store_true", help="Disable auto white balance")
    parser.add_argument("--save", default="", help="Optional output PNG path, e.g. out.png")
    parser.add_argument("--pad-out", default="", help="Output path for unpacked 16-bit raw; default: frame_N.pad.raw")
    parser.add_argument("--no-save-pad", action="store_true", help="Do not save unpacked 16-bit raw")
    args = parser.parse_args()

    if not os.path.exists(RAW_FILE):
        print(f"error: {RAW_FILE} not found in current directory: {os.getcwd()}")
        return 1

    try:
        img12 = load_raw12_image(RAW_FILE, WIDTH, HEIGHT, STRIDE)
    except Exception as exc:
        print(f"error: load raw failed: {exc}")
        return 1

    if not args.no_save_pad:
        pad_out = args.pad_out if args.pad_out else default_pad_raw_name(RAW_FILE)
        img12.astype("<u2", copy=False).tofile(pad_out)
        print(f"saved unpacked raw16: {pad_out} ({img12.shape[1]}x{img12.shape[0]})")

    p1 = np.percentile(img12, 1)
    p99 = np.percentile(img12, 99)

    black = int(p1) if args.black < 0 else args.black
    white = int(p99) if args.white < 0 else args.white
    if white <= black:
        white = black + 1

    preview_gray = to_preview_u8(img12, black, white)
    bgr = cv2.cvtColor(preview_gray, BAYER_TO_CV[args.bayer])
    preview = auto_white_balance_bgr(bgr) if not args.no_awb else bgr

    print(
        f"loaded {RAW_FILE}: {WIDTH}x{HEIGHT}, stride={STRIDE}, "
        f"min={int(img12.min())}, max={int(img12.max())}, p1={p1:.1f}, p99={p99:.1f}, "
        f"map=[{black},{white}], bayer={args.bayer}, awb={'off' if args.no_awb else 'on'}"
    )

    if args.save:
        if not cv2.imwrite(args.save, preview):
            print(f"warning: failed to save {args.save}")
        else:
            print(f"saved preview: {args.save}")

    win = "RAW12 Preview"
    cv2.namedWindow(win, cv2.WINDOW_NORMAL)
    cv2.imshow(win, preview)
    print("press any key in image window to exit")
    cv2.waitKey(0)
    cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    sys.exit(main())
