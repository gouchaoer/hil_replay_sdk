#!/usr/bin/env python3
import sys
import os
import cv2
import numpy as np
import matplotlib.pyplot as plt
import random

def gen_edit_env():
    global RES_W 
    global RES_H 
    global FRAME_SUM 
    global COLOR 
    global FPS 
    global SEI_START_TIME_US 
    global res_w 
    global res_h 
    global frame_sum 
    global fps 
    global sei_start_time_us 
    global COLORS 
    global bgr_value 
    global frame_s 
    RES_W = os.environ['RES_W']
    RES_H = os.environ['RES_H']
    FRAME_SUM = os.environ['FRAME_SUM']
    COLOR = os.environ['COLOR']
    FPS = os.environ['FPS']
    SEI_START_TIME_US = os.environ['SEI_START_TIME_US']
    res_w = int(RES_W)
    res_h = int(RES_H)
    frame_sum = int(FRAME_SUM)
    if(frame_sum>=10000):
        print("error, FRAME_SUM should < 10000!!!")
        os.exit(1)
    fps = int(FPS)
    sei_start_time_us = float(SEI_START_TIME_US)
    COLORS = {
        'red': (0, 0, 255),
        'orange': (0, 165, 255),
        'yellow': (0, 255, 255),
        'green': (0, 255, 0),
        'blue': (255, 0, 0),
        'cyan': (255, 255, 0),
        'purple': (128, 0, 128)
    }

    bgr_value = COLORS[COLOR]
    frame_s = 1 /  fps



def gen_digits(gray=False):
    digits = []
    width, height = 50, 100
    font = cv2.FONT_HERSHEY_SIMPLEX
    font_scale = 2
    font_thickness = 3
    for i in range(10):
        image = np.ones((height, width, 3), dtype=np.uint8) * 255  # 白色背景
        text = str(i)
        text_size = cv2.getTextSize(text, font, font_scale, font_thickness)[0]
        text_x = (width - text_size[0]) // 2
        text_y = (height + text_size[1]) // 2
        cv2.putText(image, text, (text_x, text_y), font, font_scale, (0, 0, 0), font_thickness)
        if gray:
            image_gray =  cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
            digits.append(image_gray)
        else:
            digits.append(image)
    return digits

def gen_img():
    gen_edit_env()
    digits = gen_digits()
    for i in range(0, frame_sum):
        frame_no_str = f"{i:04d}"
        image = np.full((res_h, res_w, 3), bgr_value, dtype=np.uint8)
        sei_now_time_us = sei_start_time_us + frame_s * i
        sei_now_time_us_str = f"{sei_now_time_us:.6f}"
        filename = f"./tmp.dir/{frame_no_str}.png"
        for idx in range(0, len(sei_now_time_us_str)):
            c = sei_now_time_us_str[idx]
            if c == '.':
                continue
            c_int = int(c)
            digit = digits[c_int]
            image[0:100, idx*50:idx*50+50] = digit
        for idx in range(0, len(frame_no_str)):
            c = frame_no_str[idx]
            c_int = int(c)
            digit = digits[c_int]
            image[200:300, idx*50:idx*50+50] = digit
        cv2.line(image, (0, res_h - 1), (res_w - 1, 0), (0, 0, 0), 10)
        cv2.imwrite(filename, image)
        print(f"filename:{filename}, frame_no_str:{frame_no_str}, sei_now_time_us_str:{sei_now_time_us_str}")

def ocr_img():
    VIDEO_PATH = os.environ['VIDEO_PATH']
    ocr_ts_path = f"{VIDEO_PATH}.ocr.ts"
    ts_li = []
    digits = gen_digits(True)
    for i in range(1, 10000):
        frame_no_str = f"{i:04d}"
        filename = f"./tmp.dir/{frame_no_str}.jpg"
        if not os.path.exists(filename):
            break
        image_gray = cv2.imread(filename, cv2.IMREAD_GRAYSCALE)
        ts_demo="1735696800.123456"
        ts=""
        for idx in range(0, len(ts_demo)):
            if ts_demo[idx] == '.':
                ts+='.'
                continue
            image_gray_sub = image_gray[0:100, idx*50:idx*50+50]
            score_li = []
            for idx2 in range(0, len(digits)):
                digit = digits[idx2]
                result = cv2.matchTemplate(digit, image_gray_sub, cv2.TM_SQDIFF_NORMED)
                score_li.append(result[0][0])
            min_idx3 = 0
            for idx3 in range(0, len(score_li)):
                score = score_li[idx3]
                if score < score_li[min_idx3]:
                    min_idx3 = idx3
            ts+=f"{min_idx3}"
        ts_li.append(ts)
    ts_li_len = len(ts_li)
    f=open(ocr_ts_path, 'w')
    for ts in ts_li:
        f.write(f"{ts}\n")
    f.close()
    print(f"{ts_li_len} ts write to {ocr_ts_path}...")


def ocr_delay():
    VIDEO_PATH = os.environ['VIDEO_PATH']

    ts_path = f"{VIDEO_PATH}.ts"
    f=open(ts_path)
    lines =  f.readlines() 
    f.close()
    ts_li = []
    for line in lines:
        line.strip()
        if line:
            ts = float(line)
            ts_li.append(ts)

    ocr_ts_path = f"{VIDEO_PATH}.ocr.ts"
    f=open(ocr_ts_path)
    lines =  f.readlines() 
    f.close()
    ocr_ts_li = []
    for line in lines:
        line.strip()
        if line:
            ts = float(line)
            ocr_ts_li.append(ts)

    x_li = []
    y_li = []
    for idx in range(0, len(ts_li)):
        if idx >= len(ocr_ts_li):
            break
        diff = ocr_ts_li[idx] - ts_li[idx]
        #diff += random.uniform(0, 0.005)
        diff += np.random.normal(loc=0.0, scale=1.0)
        x_li.append(ts_li[idx])
        y_li.append(diff)
    fig, ax = plt.subplots()
    ax.plot(x_li, y_li)
    plt.title(VIDEO_PATH)
    plt.xlabel("frame timestamp(s)")
    plt.ylabel("frame delay(s)")
    plt.show()  

def edit_sei():
    gen_edit_env()
    f = open('./tmp.raw.h264', 'rb')
    contents = f.read()
    f.close()
    contents=contents.replace(b'\x00\x00\x00\x01', b'\x00\x00\x01')
    nals = contents.split(b'\x00\x00\x01')
    #print(nals)
    nals2 = []
    for idx in range(0, len(nals)):
        if nals[idx] == b'':
            continue
        if nals[idx][0:1] == b'\x06': # raw SEI
            continue
        nals2.append(nals[idx])
    #
    nals2_len = len(nals2)
    print("nals2_len:", nals2_len)
    frame_ct = int(nals2_len / 3)
    if (frame_ct != frame_sum or nals2_len % 3 != 0 ):
        print(f"error, nals2_len:{nals2_len}!!!")
        print(nals2)
        sys.exit(1)
    nals_new = [] 
    for i in range(0, frame_sum):
        AUD = b'\x00\x00\x00\x01\x09\x50'
        SPS = b'\x00\x00\x00\x01' + nals2[i*3]   # sps
        PPS = b'\x00\x00\x00\x01' + nals2[i*3+1] # pps
        frame_no_str = f"{i:04d}"
        sei_now_time_us = sei_start_time_us + frame_s * i
        sei_now_time_us_str = f"{sei_now_time_us:.6f}"
        SEI = bytearray(b'\x00\x00\x00\x01\x06\x05\x15\x33\x31\x30\x34\x20\x31\x37\x34\x31\x35\x37\x36\x32\x30\x30\x38\x32\x36\x31\x35\x35\x80') # SEI template
        SEI[7:11] = frame_no_str.encode()
        SEI[12:29] = sei_now_time_us_str.encode()
        IDR = b'\x00\x00\x00\x01' + nals2[i*3+2] # I-frame
        nals_new.append(AUD)
        nals_new.append(SPS)
        nals_new.append(PPS)
        nals_new.append(SEI)
        nals_new.append(IDR)
    f = open('tmp.final.h264', 'wb')
    for i in range(0, len(nals_new)):
        f.write(nals_new[i])
    f.close()
    print("generate tmp.final.h264 OK")


def parse_timestamp():
    DELI = b'\x00\x00\x00\x01'
    VIDEO_PATH = os.environ['VIDEO_PATH']
    timestamp_path = f"{VIDEO_PATH}.ts"
    if os.path.exists(timestamp_path):
        print(f"timestamp_path:{timestamp_path} exits, skip...")
        return
    print(f"loading VIDEO_PATH:{VIDEO_PATH}")
    f=open(VIDEO_PATH, 'rb')
    contents=f.read()
    f.close()
    nal_all = contents.split(DELI)
    ts_li = []
    frame_no_li = []
    for nal in nal_all:
        if nal[0:1] == b'\x06': # SEI
            frame_no_str = nal[3:7]
            frame_no = int(frame_no_str)
            frame_no_li.append(frame_no)
            sei_now_time_us_str = nal[8:25]
            sei_now_time_us = float(sei_now_time_us_str)
            ts_li.append(sei_now_time_us)
        else:
            continue
    frame_sum = len(frame_no_li)
    if frame_sum == 0:
        print("error, frame_sum is 0!!!")
        sys.exit(1)

    print(f"frame_sum:{frame_sum}, 1st timestamp:{ts_li[0]}, saving timestamp:{timestamp_path}")
    f=open(timestamp_path, 'w')
    for ts in ts_li:
        f.write(f"{ts}\n")
    f.close()


method=sys.argv[1]

if method == 'gen_img':
    gen_img()

if method == 'edit_sei':
    edit_sei()

if method == 'ocr_img':
    ocr_img()

if method == 'ocr_delay':
    ocr_delay()

if method == 'parse_timestamp':
    parse_timestamp()