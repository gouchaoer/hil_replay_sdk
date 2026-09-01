#!/usr/bin/env python3
import argparse
import os
import sys

import cv2
import numpy as np


WIDTH = 1920
HEIGHT = 1536
NV12_FILE = "../data/0_0_1_1920x1536.nv12"


def load_nv12_image(
    path: str, width: int, height: int, y_stride: int, uv_stride: int
) -> np.ndarray:
    if width <= 0 or height <= 0 or width % 2 != 0 or height % 2 != 0:
        raise ValueError("NV12 width and height must be positive even numbers")
    if y_stride < width or uv_stride < width:
        raise ValueError("Y and UV strides must be at least the image width")

    expected_size = y_stride * height + uv_stride * (height // 2)
    actual_size = os.path.getsize(path)
    if actual_size < expected_size:
        raise ValueError(
            f"file too small: {path}, expect at least {expected_size} bytes, got {actual_size}"
        )

    with open(path, "rb") as file:
        data = np.frombuffer(file.read(expected_size), dtype=np.uint8)

    y_size = y_stride * height
    y_plane = data[:y_size].reshape(height, y_stride)[:, :width]
    uv_plane = data[y_size:].reshape(height // 2, uv_stride)[:, :width]

    nv12 = np.empty((height * 3 // 2, width), dtype=np.uint8)
    nv12[:height, :] = y_plane
    nv12[height:, :] = uv_plane
    return nv12


def main() -> int:
    parser = argparse.ArgumentParser(description="View an NV12 image")
    parser.add_argument("path", nargs="?", default=NV12_FILE, help="Input NV12 file")
    parser.add_argument("--width", type=int, default=WIDTH, help="Image width")
    parser.add_argument("--height", type=int, default=HEIGHT, help="Image height")
    parser.add_argument(
        "--stride", type=int, default=0, help="Y stride in bytes; 0 means image width"
    )
    parser.add_argument(
        "--uv-stride",
        type=int,
        default=0,
        help="UV stride in bytes; 0 means the Y stride",
    )
    parser.add_argument("--save", default="", help="Optional output image path, e.g. out.png")
    parser.add_argument(
        "--no-display", action="store_true", help="Do not open the preview window"
    )
    args = parser.parse_args()

    if not os.path.exists(args.path):
        print(f"error: {args.path} not found in current directory: {os.getcwd()}")
        return 1

    y_stride = args.stride if args.stride > 0 else args.width
    uv_stride = args.uv_stride if args.uv_stride > 0 else y_stride

    try:
        nv12 = load_nv12_image(
            args.path, args.width, args.height, y_stride, uv_stride
        )
        preview = cv2.cvtColor(nv12, cv2.COLOR_YUV2BGR_NV12)
    except Exception as exc:
        print(f"error: load NV12 failed: {exc}")
        return 1

    print(
        f"loaded {args.path}: {args.width}x{args.height}, "
        f"y_stride={y_stride}, uv_stride={uv_stride}"
    )

    if args.save:
        if not cv2.imwrite(args.save, preview):
            print(f"warning: failed to save {args.save}")
        else:
            print(f"saved preview: {args.save}")

    if not args.no_display:
        window = "NV12 Preview"
        cv2.namedWindow(window, cv2.WINDOW_NORMAL)
        cv2.imshow(window, preview)
        print("press any key in image window to exit")
        cv2.waitKey(0)
        cv2.destroyAllWindows()

    return 0


if __name__ == "__main__":
    sys.exit(main())