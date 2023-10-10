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
#include <pthread.h>

#include "libaxidma.h"  // Interface to the AXI DMA
#include "util.h"       // Miscellaneous utilities
#include "conversion.h" // Miscellaneous conversion utilities

#include "udpsend.h"

/*----------------------------------------------------------------------------
 * Internal Definitons
 *----------------------------------------------------------------------------*/
#define TRANS_NUM 165 * 4
#define DATA_WIDTH 64
#define TRANS_SIZE (int)(TRANS_NUM * DATA_WIDTH / 8 * sizeof(char))

#define BUF_SIZE TRANS_SIZE * 5

#define LOCAL_PORT 1234
#define DEST_PORT 5001
#define DEST_IP "192.168.0.126"

void rxcall(int channelid, void *p);
static int init_args(int *rx_channel, size_t *rx_size);
void cleanbuf(void *buf, int len);

int rxnum = 0;
int waitflag = 1;
int udpflag = 1;

void rxcall(int channelid, void *p)
{
    rxnum++;
    printf("enter rx call, rxnum:%d\n", rxnum);

    if (!udpflag)
    {
        cleanbuf(p, BUF_SIZE);
        waitflag = 0;
        return;
    }

    long *index = (long *)p;
    printf("0:%016lx    165:%016lx    165*2:%016lx    165*3:%016lx\n", index[0], index[165], index[165 * 2], index[165 * 3]);
    for (int j = 0; j < 165 * 7; j += 165)
    {
        for (int i = j; i < j + 5; ++i)
        {
            printf("%04d:%016lx\n", i, index[i]);
        }
    }

    udp_send((void*)(index+165 * 3),TRANS_SIZE/4);
    udpflag = 0;

    cleanbuf(p, BUF_SIZE);
    printf("\n\n");
    waitflag = 0;
}

/*
 * init arguments
 */
static int init_args(int *rx_channel, size_t *rx_size)
{
    // Set the default data size and number of transfers
    *rx_channel = -1;
    *rx_size = BUF_SIZE;

    return 0;
}

void cleanbuf(void *buf, int len)
{
    char *p = (char *)buf;
    memset(p, 0, len);
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

    printf("Enter main v6.0 irq axidma test\n");

    udp_send_init(LOCAL_PORT, DEST_PORT, DEST_IP);

    // Check if the user overrided the default transfer size and number
    // just pay attention to size
    if (init_args(&rx_channel, &rx_size) < 0)
    {
        rc = 1;
        goto ret;
    }

    // Initialize the AXI DMA device
    axidma_dev = axidma_init();
    if (axidma_dev == NULL)
    {
        fprintf(stderr, "Failed to initialize the AXI DMA device.\n");
        rc = 1;
        goto ret;
    }

    rx_buf = axidma_malloc(axidma_dev, rx_size);
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
    axidma_set_callback(axidma_dev, rx_channel, (void *)rxcall, rx_buf);

    printf("s2mm start\n");
    axidma_oneway_transfer(axidma_dev, rx_channel, rx_buf, rx_size, false);

    struct timeval tv_begin_s, tv_end_s, tresult_s;
    gettimeofday(&tv_begin_s, NULL);
    double timeuse_ms_s;

    while (1)
    {
        while (waitflag)
        {
        }
        waitflag = 1;

        gettimeofday(&tv_end_s, NULL);
        timersub(&tv_end_s, &tv_begin_s, &tresult_s);
        timeuse_ms_s = tresult_s.tv_sec * 1000 + (1.0 * tresult_s.tv_usec) / 1000; //  精确到毫秒
        if (timeuse_ms_s > 100)
        {
            if (!udpflag)
            {
                udpflag = 1;
            }
            gettimeofday(&tv_begin_s, NULL);
        }

        axidma_oneway_transfer(axidma_dev, rx_channel, rx_buf, rx_size, false);
    }

    printf("Exiting.\n\n");

free_rx_buf:
    axidma_free(axidma_dev, rx_buf, rx_size);
    axidma_destroy(axidma_dev);
ret:
    return rc;
}