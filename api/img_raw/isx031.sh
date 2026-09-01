gcc -O2 -o isx031 isx031.c
./isx031 isx031_1920x1537.raw # 第一行为嵌入行(0x1E)，后面1536行为图像YUV422(0X1E)，我抓取格式为UYVY422 8bit packed
