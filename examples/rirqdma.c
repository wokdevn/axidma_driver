#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h> // Strlen function

#include <fcntl.h>     // Flags for open()
#include <sys/stat.h>  // Open() system call
#include <sys/types.h> // Types for open()
#include <sys/mman.h>  // Mmap system call
#include <sys/ioctl.h> // IOCTL system call
#include <unistd.h>    // Close() system call
#include <sys/time.h>  // Timing functions and definitions
#include <getopt.h>    // Option parsing
#include <errno.h>     // Error codes
#include <assert.h>
#include <pthread.h>

#include "libaxidma.h"  // Interface to the AXI DMA
#include "util.h"       // Miscellaneous utilities
#include "conversion.h" // Miscellaneous conversion utilities
#include "udpsend.h"
#include "constel_head.h"

// TODO:
// thread exit
// gcc lib not installed
// tcp thread
/*----------------------------------------------------------------------------
 * Internal Definitons
 *----------------------------------------------------------------------------*/

// The size of data to send per transfer, in byte
#define TRANS_SIZE 4096 * 64 / 8
#define BUFFER_SIZE_MAX TRANS_SIZE * 2

typedef struct ippack
{
    void *pack;
    int size;
} ippack;

/*----------------------------------------------------------------------------
 * Function Declaration
 *----------------------------------------------------------------------------*/

static int init_args(int *rx_channel, size_t *rx_size);
static int parse_args(int argc, char **argv);
int init_reg_dev();
int regdev_read(int reg, int *val);
int regdev_write(int reg, int val);
void gw_WriteReg(unsigned int addr, int dat);
int gw_ReadReg(unsigned int addr);
int printDMAReg();
void rece_cb(int channelid, void *data);

/*----------------------------------------------------------------------------
 * Variable Declaration
 *----------------------------------------------------------------------------*/

int waitFlag = 1;
int onceFlag = 0;
int g_memDev;
void *map_base_dma;
void *map_base_apb;

int resetFlag = 0;
int packetflag = 0;
int udp_blank_ctrl = 1;

/*----------------------------------------------------------------------------
 * Function
 *----------------------------------------------------------------------------*/

/*
 * init arguments
 */
static int init_args(int *rx_channel, size_t *rx_size)
{
    // Set the default data size and number of transfers
    *rx_channel = -1;
    *rx_size = BUFFER_SIZE_MAX;
    return 0;
}

static int parse_args(int argc, char **argv)
{
    char option;

    while ((option = getopt(argc, argv, "rabcd")) != (char)-1)
    {
        switch (option)
        {
        case 'r':
            resetFlag = 1;
            break;
        case 'a':
            packetflag = 1;
            break;
        case 'b':
            packetflag = 2;
            break;
        case 'c':
            packetflag = 3;
            break;
        case 'd':
            packetflag = 4;
            break;
        default:
            return 0;
        }
    }

    return 0;
}

int regdev_read(int reg, int *val)
{
    int ret = 0;
    void *virt_addr;

    if (reg > 0xFFFB)
    { // exceed 64KB
        ret = -EIO;
        goto exit;
    }

    virt_addr = (char *)(map_base_dma) + reg;

    *val = *(volatile uint32_t *)(virt_addr);
exit:
    return ret;
}

int regdev_write(int reg, int val)
{
    int ret = 0;

    void *virt_addr;

    if (reg > 0xFFFB)
    { // exceed 64KB
        ret = -EIO;
        goto exit;
    }

    virt_addr = (char *)(map_base_apb) + reg;

    *(volatile uint32_t *)(virt_addr) = val;

exit:
    return ret;
}

void gw_WriteReg(unsigned int addr, int dat)
{
    int rc;
    rc = regdev_write(addr & 0xffff, dat);
    if (rc != 0)
    {
        printf("regdev_write error ! rc = %d \n", rc);
    }
}

int gw_ReadReg(unsigned int addr)
{
    int value, rc;
    rc = regdev_read(addr & 0xffff, &value);
    if (rc != 0)
    {
        printf("regdev_read error ! rc = %d \n", rc);
    }
    return value;
}

int init_reg_dev()
{
    g_memDev = open("/dev/mem", O_RDWR | O_SYNC);
    if (g_memDev < 0)
    {
        printf("ERROR: can not open /dev/mem\n");
        return 0;
    }
    map_base_dma = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, g_memDev, 0xA0000000);
    if (!map_base_dma)
    {
        printf("ERROR: unable to mmap adc registers\n");
        if (g_memDev)
            close(g_memDev);

        return 0;
    }

    map_base_apb = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, g_memDev, 0xA0010000);
    if (!map_base_apb)
    {
        printf("ERROR: unable to mmap adc registers\n");
        if (g_memDev)
            close(g_memDev);

        return 0;
    }

    return 0;
}

void mudpsend(ippack *udppack)
{
    // struct timeval tv_begin, tv_end, tresult;

    // gettimeofday(&tv_begin, NULL);
    long *data = udppack->pack;
    int len;

    if (packetflag < 4 && packetflag > 0)
    {
        len = 8;
    }
    else if (packetflag == 4)
    {
        len = 6;
    }
    else
    {
        printf("packet flag error\n");
        return;
    }

    long *head[8] = {0};
    int index_define_head = (packetflag - 1) * 4;

    for (int i = 0; i < len; ++i)
    {
        head[i] = data + 1320/8 * i; // point to packet start

        if (i % 2 == 0)
        { // change constellation or speed
            *(head[i]) = HEAD_CONSTEL;
        }
        else
        {
            *(head[i]) = HEAD_SPEED;
        }

        // change index
        *(head[i] + 1) = define_head[index_define_head + i / 2];

        udp_send(head[i], 1320);

        for (int k = 0; k < 3; ++k)
        {
            printf("data in %d,%016lx\n", k, *(head[i] + k));
        }
        printf("\n");
    }
    printf("\n");

    // // pthread_exit(NULL);

    // gettimeofday(&tv_end, NULL);
    // timersub(&tv_end, &tv_begin, &tresult);

    // double timeuse_s = tresult.tv_sec + (1.0 * tresult.tv_usec) / 1000000;      //  精确到秒
    // double timeuse_ms = tresult.tv_sec * 1000 + (1.0 * tresult.tv_usec) / 1000; //  精确到毫秒
    // // printf("timeuse in ms: %f \n", timeuse_ms);
}

long d1 = 0;
void rece_cb(int channelid, void *data)
{
    gw_WriteReg(0xA0010004, 0x0);

    long *rx_buf_tmp = (long *)data;

    if (printDMAReg() < 0)
    {
        printf("callback\n");
    }

    if (!udp_blank_ctrl)
    {
        waitFlag = 0;
        return;
    }
    udp_blank_ctrl = 0;

    ippack udppk;
    udppk.pack = data;
    if (packetflag < 4 && packetflag > 0)
    {
        udppk.size = 1320 * 64 * 8 / 8; //----1320 packet 64 width 8 packet 8 byte
    }
    else if (packetflag == 4)
    {
        udppk.size = 1320 * 64 * 6 / 8;
    }
    pthread_t tids;
    // int ret = pthread_create(&tids, NULL, (void *)mudpsend, &udppk);
    // if (ret != 0)
    // {
    //     printf("pthread_create error: error_code=%d", ret);
    //     waitFlag = 0;
    //     return;
    // }

    // ret = pthread_detach(tids);
    // if (ret != 0)
    // {
    //     fprintf(stderr, "pthread_detach error:%s\n", strerror(ret));
    //     waitFlag = 0;
    //     return;
    // }

    mudpsend(&udppk);

    // //to confirm data between pl and ps

    // if (rx_buf_tmp[0] != d1 + 0x0100)
    // {
    //     printf("d1: %016lx, now: %016lx\n\n", d1, rx_buf_tmp[0]);
    //     d1 = rx_buf_tmp[0];

    //     for (int i = 0; i < 10; ++i)
    //     {
    //         printf("i:%d, data:%016lx\n", i, *((long *)(rx_buf_tmp + i)));
    //         *((long *)(rx_buf_tmp + i)) = 0;
    //     }
    //     printf("\n\n\n");
    // }else{
    //     d1 = rx_buf_tmp[0];
    //     printf("ok:%016lx\n",d1);
    //     printf("data[1]:%016lx\n",rx_buf_tmp[1]);
    // }

    // wirteRingbuffer(rx_buf_tmp, TRANS_SIZE / 8);

    // for (int i = 0; i < 4096+10; ++i)
    // {
    //     printf("i:%d, data:%016lx\n", i, *((long *)(rx_buf_tmp + i)));
    //     *((long *)(rx_buf_tmp + i))=0;
    // }

    // printf("\nINFO: callback func triggerd,channelid: %d \n", channelid);

    waitFlag = 0;
}

// normal:
// 0x0,0x17003,0x1000a,idle complete
int printDMAReg()
{
    int reg58 = gw_ReadReg(0xA0000058);
    int reg30 = gw_ReadReg(0xA0000030);
    int reg34 = gw_ReadReg(0xA0000034);

    if (reg58 != 0x0 || reg30 != 0x17003 || reg34 != 0x1000a)
    {
        printf("58 : 0x%x \n", gw_ReadReg(0xA0000058));
        printf("30 : 0x%x \n", gw_ReadReg(0xA0000030));
        printf("34 : 0x%x \n", gw_ReadReg(0xA0000034));
        return -1;
    }
    else
    {
        // printf("reg ok\n");
        return 0;
    }
}

/*----------------------------------------------------------------------------
 * Main Function
 *----------------------------------------------------------------------------*/
int main(int argc, char **argv)
{
    int rc;
    int rx_channel;
    size_t rx_size;
    char *rx_buf;
    axidma_dev_t axidma_dev;
    const array_t *rx_chans;
    int trans_count = 0;

    printf("Enter main v8.0 machine box\n");

    if (parse_args(argc, argv) < 0)
    {
        rc = 1;
        goto ret;
    }

    if (packetflag == 0)
    {
        printf("please name the machine\n");
        goto ret;
    }

    init_reg_dev();

    // reset
    //  gw_WriteReg(0xA0010004, 0x4);

    if (resetFlag)
    {
        gw_WriteReg(0xA0010004, 0x4); // reset
    }
    else
    {
        gw_WriteReg(0xA0010004, 0x0);
    }

    if (init_args(&rx_channel, &rx_size) < 0)
    {
        rc = 1;
        goto ret;
    }

    printf("AXI DMA Trans Parameters:\n");
    printf("\tTRANS_SIZE:%d \n", TRANS_SIZE);
    printf("\tReceive Buffer Size: %ld MByte\n", BYTE_TO_MIB(rx_size));

    udp_init();

    // Initialize the AXI DMA device
    axidma_dev = axidma_init();
    if (axidma_dev == NULL)
    {
        fprintf(stderr, "Failed to initialize the AXI DMA device.\n");
        rc = 1;
        goto ret;
    }

    rx_buf = axidma_malloc(axidma_dev, BUFFER_SIZE_MAX);
    if (rx_buf == NULL)
    {
        perror("Unable to allocate receive buffer from the AXI DMA device");
        rc = -1;
        goto free_rx_buf;
    }

    rx_chans = axidma_get_dma_rx(axidma_dev);
    if (rx_chans->len < 1)
    {
        fprintf(stderr, "Error: No receive channels were found.\n");
        rc = -ENODEV;
        goto free_rx_buf;
    }

    /* If the user didn't specify the channels, we assume that the transmit and
     * receive channels are the lowest numbered ones. */
    if (rx_channel == -1)
    {
        rx_channel = rx_chans->data[0];
    }
    printf("Using receive channel %d.\n", rx_channel);

    axidma_set_callback(axidma_dev, rx_channel, rece_cb, (void *)rx_buf);
    if (printDMAReg() < 0)
    {
        printf("after set callback, before first trans\n");
    }

    axidma_oneway_transfer(axidma_dev, rx_channel, rx_buf, BUFFER_SIZE_MAX, false);
    // control ready
    if (resetFlag)
    {
        gw_WriteReg(0xA0010004, 0x5); // reset
    }
    else
    {
        gw_WriteReg(0xA0010004, 0x1);
    }
    printf("Single transfer test successfully completed!\n");

    if (printDMAReg() < 0)
    {
        printf("after submit first trans\n");
    }

    struct timeval tv_begin_s, tv_end_s, tresult_s;
    gettimeofday(&tv_begin_s, NULL);
    double timeuse_ms_s;

    while (1)
    {
        while (waitFlag)
        {
        }

        gettimeofday(&tv_end_s, NULL);
        timersub(&tv_end_s, &tv_begin_s, &tresult_s);
        timeuse_ms_s = tresult_s.tv_sec * 1000 + (1.0 * tresult_s.tv_usec) / 1000; //  精确到毫秒
        if (timeuse_ms_s > 100)
        {
            if (!udp_blank_ctrl)
            {
                udp_blank_ctrl = 1;
            }
            gettimeofday(&tv_begin_s, NULL);
        }

        if (printDMAReg() < 0)
        {
            printf("after wait\n");
        }

        waitFlag = 1;

        trans_count++;
        // 64--big pack, every 100 big pack
        if (trans_count % 6400 == 0)
        {
            printf("trans count:%d\n", trans_count);
        }
        axidma_oneway_transfer(axidma_dev, rx_channel, rx_buf, BUFFER_SIZE_MAX, false);
        gw_WriteReg(0xA0010004, 0x1);
        if (onceFlag)
            break;

        // printf("Next Single transfer !\n");
    }

    printf("Single transfer End!\n");

free_rx_buf:
    axidma_free(axidma_dev, rx_buf, rx_size);
    axidma_destroy(axidma_dev);
ret:
    udp_release();
    return rc;
}