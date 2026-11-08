#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define ISX031_HEAD_SIZE 600


static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0]
         | ((uint16_t)p[1] << 8);
}


static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}


static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}


/*
 * CRC32 IEEE
 */
static uint32_t isx031_head_crc32(
        const uint8_t *data,
        int len)
{
    uint32_t crc = 0xFFFFFFFF;

    for (int i = 0; i < len; i++)
    {
        crc ^= data[i];

        for (int j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }

    return crc ^ 0xFFFFFFFF;
}


/*
 * 读取 emb 文件到外部 buffer
 *
 * return:
 * >=0 : 文件大小
 * <0  : 失败
 */
int isx031_head_init(
        uint8_t *buf,
        int buf_size,
        const char *filename)
{
    FILE *fp = fopen(filename, "rb");

    if (!fp)
    {
        perror("fopen");
        return -1;
    }

    size_t n = fread(buf, 1, (size_t)buf_size, fp);

    fclose(fp);

    return (int)n;
}


/*
 * 校验 ISX031 Embedded Header CRC
 */
int isx031_head_check(
        const uint8_t *buf)
{
    uint32_t header_file =
        rd32(buf + 4);

    uint32_t header_calc =
        isx031_head_crc32(
            buf,
            4);


    uint32_t footer_file =
        rd32(buf + 596);

    uint32_t footer_calc =
        isx031_head_crc32(
            buf + 8,
            588);


    printf("Data Count  : %u\n",
            rd16(buf));

    printf("Frame Count : %u\n",
            rd32(buf + 36));


    printf("Header CRC File : 0x%08X\n",
            header_file);

    printf("Header CRC Calc : 0x%08X\n",
            header_calc);


    printf("Footer CRC File : 0x%08X\n",
            footer_file);

    printf("Footer CRC Calc : 0x%08X\n",
            footer_calc);


    if (header_file != header_calc)
    {
        printf("Header CRC FAIL\n");
        return -1;
    }


    if (footer_file != footer_calc)
    {
        printf("Footer CRC FAIL\n");
        return -1;
    }


    return 0;
}


/*
 * Frame Count +1
 *
 * E37-E40 : Frame Count
 *
 * 更新 Footer CRC
 */
int isx031_head_update(
        uint8_t *buf)
{
    uint32_t frame =
        rd32(buf + 36);


    frame++;


    wr32(
        buf + 36,
        frame);


    uint32_t crc =
        isx031_head_crc32(
            buf + 8,
            588);


    wr32(
        buf + 596,
        crc);


    return 0;
}