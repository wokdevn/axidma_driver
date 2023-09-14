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

#define CIRCLE_SIZE_LONG 4096 * 5

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
// ring buffer
void initRingbuffer(void);
int wirteRingbuffer(long *buffer, int addLen);
int readRingbuffer(long *buffer, int len);
int getRingbufferValidLen(void);
void releaseRingbuffer(void);
int mtcpsend(ippack *tcppack);

/*----------------------------------------------------------------------------
 * Variable Declaration
 *----------------------------------------------------------------------------*/

int waitFlag = 1;
int onceFlag = 0;
int g_memDev;
void *map_base_dma;
void *map_base_apb;

int resetFlag = 0;

static int validLen;            // 已使用的数据长度
static long *pHead = NULL;      // 环形存储区的首地址
static long *pTail = NULL;      // 环形存储区的结尾地址
static long *pValid = NULL;     // 已使用的缓冲区的首地址
static long *pValidTail = NULL; // 已使用的缓冲区的尾地址

/*----------------------------------------------------------------------------
 * Function
 *----------------------------------------------------------------------------*/

void initRingbuffer(void)
{
    if (pHead == NULL)
    {
        pHead = (long *)malloc(CIRCLE_SIZE_LONG * sizeof(long));
    }
    pValid = pValidTail = pHead;
    pTail = pHead + CIRCLE_SIZE_LONG;
    validLen = 0;
}

/*
 * function:向缓冲区中写入数据
 * param:@buffer 写入的数据指针
 *       @addLen 写入的数据长度,in long
 * return:-1:写入长度过大
 *        -2:缓冲区没有初始化
 * */
int wirteRingbuffer(long *buffer, int addLen)
{
    if (addLen > CIRCLE_SIZE_LONG)
        return -2;
    if (pHead == NULL)
        return -1;
    assert(buffer);

    // 将要存入的数据copy到pValidTail处
    if (pValidTail + addLen > pTail) // 需要分成两段copy
    {
        int len1 = pTail - pValidTail;
        int len2 = addLen - len1;
        memcpy(pValidTail, buffer, len1);
        memcpy(pHead, buffer + len1, len2);
        pValidTail = pHead + len2; // 新的有效数据区结尾指针
    }
    else
    {
        memcpy(pValidTail, buffer, addLen);
        pValidTail += addLen; // 新的有效数据区结尾指针
    }

    // 需重新计算已使用区的起始位置
    if (validLen + addLen > CIRCLE_SIZE_LONG)
    {
        int moveLen = validLen + addLen - CIRCLE_SIZE_LONG; // 有效指针将要移动的长度
        if (pValid + moveLen > pTail)                       // 需要分成两段计算
        {
            int len1 = pTail - pValid;
            int len2 = moveLen - len1;
            pValid = pHead + len2;
        }
        else
        {
            pValid = pValid + moveLen;
        }
        validLen = CIRCLE_SIZE_LONG;
    }
    else
    {
        validLen += addLen;
    }

    return 0;
}

/*
 * function:从缓冲区内取出数据
 * param   :@buffer:接受读取数据的buffer
 *          @len:将要读取的数据的长度
 * return  :-1:没有初始化
 *          >0:实际读取的长度
 * */
int readRingbuffer(long *buffer, int len)
{
    if (pHead == NULL)
        return -1;

    assert(buffer);

    if (validLen == 0)
        return 0;

    if (len > validLen)
        len = validLen;

    if (pValid + len > pTail) // 需要分成两段copy
    {
        int len1 = pTail - pValid;
        int len2 = len - len1;
        memcpy(buffer, pValid, len1);       // 第一段
        memcpy(buffer + len1, pHead, len2); // 第二段，绕到整个存储区的开头
        pValid = pHead + len2;              // 更新已使用缓冲区的起始
    }
    else
    {
        memcpy(buffer, pValid, len);
        pValid = pValid + len; // 更新已使用缓冲区的起始
    }
    validLen -= len; // 更新已使用缓冲区的长度

    return len;
}

/*
 * function:获取已使用缓冲区的长度
 * return  :已使用的buffer长度
 * */
int getRingbufferValidLen(void)
{
    return validLen;
}

/*
 * function:释放环形缓冲区
 * */
void releaseRingbuffer(void)
{
    if (pHead != NULL)
        free(pHead);
    pHead = NULL;
}

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

int mtcpsend(ippack *tcppack)
{
    return sendTcp(tcppack->pack, tcppack->size);
}

void rece_cb(int channelid, void *data)
{
    gw_WriteReg(0xA0010004, 0x0);

    long *rx_buf_tmp = (long *)data;

    if (printDMAReg() < 0)
    {
        printf("callback\n");
    }

    // if (getRingbufferValidLen() > 0)
    // {
    //     udppack udpk;
    //     udpk.pack = pValid;
    //     udpk.size = TRANS_SIZE;

    //     pthread_t tids;
    //     int ret = pthread_create(&tids, NULL, (void *)mudpsend, &udpk);

    //     if (ret != 0)
    //     {
    //         printf("pthread_create error: error_code=%d", ret);
    //         return;
    //     }
    // }

    // wirteRingbuffer(rx_buf_tmp, TRANS_SIZE / 8);

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

    // printf("58 : 0x%x \n", gw_ReadReg(0xA0000058));
    // printf("30 : 0x%x \n", gw_ReadReg(0xA0000030));
    // printf("34 : 0x%x \n", gw_ReadReg(0xA0000034));
    // printf("48 : 0x%x \n", gw_ReadReg(0xA0000048));
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

    //reset
    // gw_WriteReg(0xA0010004, 0x4);

    if (resetFlag)
    {
        gw_WriteReg(0xA0010004, 0x4);//reset
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
        gw_WriteReg(0xA0010004, 0x5);//reset
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
    releaseRingbuffer();
    releaseTcp();
    return rc;
}