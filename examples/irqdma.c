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

/*----------------------------------------------------------------------------
 * Internal Definitons
 *----------------------------------------------------------------------------*/
#define TRANS_NUM 4096
#define DATA_WIDTH 64
#define TRANS_SIZE (int)(TRANS_NUM * DATA_WIDTH / 8 * sizeof(char))

#define BUF_SIZE TRANS_SIZE * 5

void rxcall(int channelid, void *p);
static int init_args(int *rx_channel, size_t *rx_size);

int rxnum = 0;
int waitflag = 1;

void rxcall(int channelid, void *p)
{
    rxnum++;
    printf("enter rx call, rxnum:%d\n", rxnum);

    long *index = (long *)p;
    printf("0:%016lx    4094:%016lx    4095:%016lx    4100:%016lx\n", index[0], index[4094], index[4095], index[4100]);

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
    while (1)
    {
        while (waitflag)
        {
        }
        waitflag = 1;

        axidma_oneway_transfer(axidma_dev, rx_channel, rx_buf, rx_size, false);
    }

    printf("Exiting.\n\n");

free_rx_buf:
    axidma_free(axidma_dev, rx_buf, rx_size);
    axidma_destroy(axidma_dev);
ret:
    return rc;
}