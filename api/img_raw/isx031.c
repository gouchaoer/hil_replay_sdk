#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "isx031.h"

#define BUF_SIZE 2880

int main(int argc, char *argv[])
{
    uint8_t buf[BUF_SIZE];

    if (argc != 2)
        return -1;


    if (isx031_head_init(
            buf,
            BUF_SIZE,
            argv[1]) < 0)
    {
        return -1;
    }


    printf("=== original ===\n");

    isx031_head_check(buf);


    isx031_head_update(buf);


    printf("=== updated ===\n");

    isx031_head_check(buf);


    return 0;
}