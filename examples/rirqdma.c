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

#include "tcpserver.h"

/*----------------------------------------------------------------------------
 * Internal Definitons
 *----------------------------------------------------------------------------*/

// The size of data to send per transfer, in byte
#define TRANS_SIZE 4096 * 64 * 64 / 8
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

    while ((option = getopt(argc, argv, "r")) != (char)-1)
    {
        switch (option)
        {
        case 'r':
            resetFlag = 1;
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

void mtcpsend(ippack *tcppack)
{
    sendTcp(tcppack->pack, tcppack->size);
    // pthread_exit(NULL);
}

void rece_cb(int channelid, void *data)
{
    gw_WriteReg(0xA0010004, 0x0);

    long *rx_buf_tmp = (long *)data;

    if (printDMAReg() < 0)
    {
        printf("callback\n");
    }

    if (linkFlag)
    {
        ippack tcppk;
        tcppk.pack = data;
        tcppk.size = 4096 * 64 / 8;
        pthread_t tids;
        int ret = pthread_create(&tids, NULL, (void *)mtcpsend, &tcppk);
        if (ret != 0)
        {
            printf("pthread_create error: error_code=%d", ret);
            waitFlag = 0;
            return;
        }

        // ret = pthread_detach(tids);
        // if (ret != 0)
        // {
        //     fprintf(stderr, "pthread_detach error:%s\n", strerror(ret));
        //     return;
        // }
    }

    // if (*((long *)(rx_buf_tmp)) != 0x0001000002000100)
    // {
    //     for (int i = 0; i < 4096 + 10; ++i)
    //     {
    //         printf("i:%d, data:%016lx\n", i, *((long *)(rx_buf_tmp + i)));
    //         *((long *)(rx_buf_tmp + i)) = 0;
    //     }
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

    printf("Enter main v7.0 double division graph\n");

    if (parse_args(argc, argv) < 0)
    {
        rc = 1;
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

    initRingbuffer();

    tcpInit();

    pthread_t tcpTids;
    int ret = pthread_create(&tcpTids, NULL, (void *)tcpLink, NULL);
    if (ret != 0)
    {
        printf("tcp link pthread_create error: error_code=%d", ret);
        goto free_rx_buf;
    }

    // ret = pthread_detach(tcpTids);
    // if (ret != 0)
    // {
    //     fprintf(stderr, "pthread_detach error:%s\n", strerror(ret));
    //     goto free_rx_buf;
    // }

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

    while (1)
    {
        while (waitFlag)
        {
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
    releaseTcp();
    return rc;
}