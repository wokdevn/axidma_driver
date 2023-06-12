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

#include "libaxidma.h"  // Interface to the AXI DMA
#include "util.h"       // Miscellaneous utilities
#include "conversion.h" // Miscellaneous conversion utilities

#include "udpserver.h"
#include "udpclient.h"

/*----------------------------------------------------------------------------
 * Internal Definitons
 *----------------------------------------------------------------------------*/

// The size of data to send per transfer, in byte
#define TRANS_NUM 8
#define TRANS_SIZE ((int)(TRANS_NUM * sizeof(char)))

// The pattern that we fill into the buffers
#define TEST_PATTERN(i) ((int)(0x1234ACDE ^ (i)))

// The DMA context passed to the helper thread, who handles remainder channels
struct udpmm2s
{
    axidma_dev_t axidma_dev;
    int tx_channel;
    char *tx_buf;
    size_t tx_size;
    struct axidma_video_frame *tx_frame;
};

/*
 * init arguments
 */
static int init_args(int *tx_channel, int *rx_channel,
                     size_t *tx_size, struct axidma_video_frame *tx_frame, size_t *rx_size,
                     struct axidma_video_frame *rx_frame, bool *use_vdma)
{
    // Set the default data size and number of transfers
    *use_vdma = false;
    *tx_channel = -1;
    *rx_channel = -1;
    *tx_size = TRANS_SIZE;
    tx_frame->height = -1;
    tx_frame->width = -1;
    tx_frame->depth = -1;
    *rx_size = TRANS_SIZE;
    rx_frame->height = -1;
    rx_frame->width = -1;
    rx_frame->depth = -1;

    return 0;
}

/* Initialize the tx buffers, filling buffers with a preset
 * pattern. */
static void init_tx_data(char *tx_buf, size_t tx_buf_size)
{
    size_t i;
    long *transmit_buffer;
    // two int 8 Byte, means 64bit data

    transmit_buffer = (long *)tx_buf;

    // Fill the buffer with integer patterns
    for (i = 0; i < tx_buf_size / sizeof(long); i++)
    {
        transmit_buffer[i] = -1;
    }

    // To align
    // Fill in any leftover bytes if it's not aligned
    for (i = 0; i < tx_buf_size % sizeof(long); i++)
    {
        tx_buf[i] = -1;
    }

    return;
}

static int mm2s_test(axidma_dev_t dev, int tx_channel, void *tx_buf,
                     int tx_size, struct axidma_video_frame *tx_frame)
{
    int rc;

    // Initialize the buffer region we're going to transmit
    init_tx_data(tx_buf, tx_size);

    // printf("One way transfer!\n");
    // Perform the DMA transaction
    rc = axidma_oneway_transfer(dev, tx_channel, tx_buf, tx_size, true);
    if (rc < 0)
    {
        return rc;
    }

    return rc;
}

/* Initialize the rx buffers, filling buffers with a preset
 * pattern. */
static void init_rx_data(char *rx_buf, size_t rx_buf_size)
{
    size_t i;
    long *receive_buffer;

    receive_buffer = (long *)rx_buf;

    // Fill the buffer with integer patterns
    for (i = 0; i < rx_buf_size / sizeof(long); i++)
    {
        receive_buffer[i] = 1;
    }

    // Fill in any leftover bytes if it's not aligned
    for (i = 0; i < rx_buf_size % sizeof(long); i++)
    {
        rx_buf[i] = 1;
    }

    return;
}

static int s2mm_test(axidma_dev_t dev, int rx_channel, void *rx_buf,
                     int rx_size, struct axidma_video_frame *rx_frame)
{
    int rc;

    // Initialize the buffer region we're going to transmit
    init_rx_data(rx_buf, rx_size);

    // Perform the DMA transaction
    rc = axidma_oneway_transfer(dev, rx_channel, rx_buf, rx_size, true);
    if (rc < 0)
    {
        return rc;
    }

    long *index = rx_buf;
    int fixall = 0;
    int fixcount = 0;

    printf("After trans, data in %04d : %016lx   ,count:%d,\n", 1, *index, fixcount);

    printf("udp send start v2.0>>>>>>>>>>>>>>>>>>>>>>>\n");

    if (*index == -1)
    {
        char flag[8];
        for (int i = 0; i < 8; ++i)
        {
            flag[i] = 0xff;
        }
        udp_send(flag, 1320);
    }

    return rc;
}

void *udp_recv(void *args)
{
    struct udpmm2s *arg_thread1;
    arg_thread1 = (struct udpmm2s *)args;
    /* sock_fd --- socket文件描述符 创建udp套接字*/
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        perror("socket");
        exit(1);
    }

    printf("UDP RECV ongoing\n\n");

    /* 将套接字和IP、端口绑定 */
    struct sockaddr_in addr_serv;
    int len;
    memset(&addr_serv, 0, sizeof(struct sockaddr_in)); // 每个字节都用0填充
    addr_serv.sin_family = AF_INET;                    // 使用IPV4地址
    addr_serv.sin_port = htons(LOCAL_PORT);            // 端口
    /* INADDR_ANY表示不管是哪个网卡接收到数据，只要目的端口是SERV_PORT，就会被该应用程序接收到 */
    addr_serv.sin_addr.s_addr = htonl(INADDR_ANY); // 自动获取IP地址
    len = sizeof(addr_serv);

    /* 绑定socket */
    if (bind(sock_fd, (struct sockaddr *)&addr_serv, sizeof(addr_serv)) < 0)
    {
        perror("bind error:");
        exit(1);
    }

    int recv_num;
    int send_num;
    char send_buf[20] = "i am server!";
    char recv_buf[20];
    struct sockaddr_in addr_client;

    while (1)
    {
        printf("rev wait:\n");

        recv_num = recvfrom(sock_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&addr_client, (socklen_t *)&len);

        if (recv_num < 0)
        {
            perror("recvfrom error:");
            exit(1);
        }

        recv_buf[recv_num] = '\0';
        printf("receive %d bytes: \n", recv_num);
        for (int i = 0; i < recv_num; i++)
        {
            printf("%02x ", recv_buf[i]);
        }
        printf("\n");

        if (recv_num > 0)
        {
            int rc = mm2s_test(arg_thread1->axidma_dev, arg_thread1->tx_channel, arg_thread1->tx_buf, arg_thread1->tx_size,
                               arg_thread1->tx_frame);
            if (rc < 0)
            {
                printf("????????????Send MM2S failed\n");
            }
            else
            {
                printf("!!!!!!!!!!!!Send MM2S success\n");
            }
        }
    }

    close(sock_fd);

    return 0;
}

/*----------------------------------------------------------------------------
 * Main Function
 *----------------------------------------------------------------------------*/
int main(int argc, char **argv)
{
    int rc;
    int tx_channel, rx_channel;
    size_t tx_size, rx_size;
    bool use_vdma;
    char *tx_buf, *rx_buf;
    axidma_dev_t axidma_dev;
    const array_t *tx_chans, *rx_chans;
    struct axidma_video_frame transmit_frame, *tx_frame, receive_frame, *rx_frame;

    printf("Enter main v5.0 two devices connect\n");

    // Check if the user overrided the default transfer size and number
    // just pay attention to size
    if (init_args(&tx_channel, &rx_channel, &tx_size,
                  &transmit_frame, &rx_size, &receive_frame,
                  &use_vdma) < 0)
    {
        rc = 1;
        goto ret;
    }

    printf("AXI DMA Trans Parameters:\n");
    printf("\tTRANS_SIZE:%d \n", TRANS_SIZE);
    printf("\tTx size:%d \n", tx_size);
    if (!use_vdma)
    {
        printf("\tTransmit Buffer Size: %d Byte\n", (tx_size));
        printf("\tReceive Buffer Size: %d Byte\n", (rx_size));
    }

    // Initialize the AXI DMA device
    axidma_dev = axidma_init();
    if (axidma_dev == NULL)
    {
        fprintf(stderr, "Failed to initialize the AXI DMA device.\n");
        rc = 1;
        goto ret;
    }

    // Map memory regions for the transmit and receive buffers
    tx_buf = axidma_malloc(axidma_dev, tx_size);
    if (tx_buf == NULL)
    {
        perror("Unable to allocate transmit buffer from the AXI DMA device.");
        rc = -1;
        goto destroy_axidma;
    }
    rx_buf = axidma_malloc(axidma_dev, rx_size);
    if (rx_buf == NULL)
    {
        perror("Unable to allocate receive buffer from the AXI DMA device");
        rc = -1;
        goto free_tx_buf;
    }

    tx_chans = axidma_get_dma_tx(axidma_dev);
    rx_chans = axidma_get_dma_rx(axidma_dev);
    tx_frame = NULL;
    rx_frame = NULL;

    if (tx_chans->len < 1)
    {
        fprintf(stderr, "Error: No transmit channels were found.\n");
        rc = -ENODEV;
        goto free_rx_buf;
    }
    if (rx_chans->len < 1)
    {
        fprintf(stderr, "Error: No receive channels were found.\n");
        rc = -ENODEV;
        goto free_rx_buf;
    }

    /* If the user didn't specify the channels, we assume that the transmit and
     * receive channels are the lowest numbered ones. */
    if (tx_channel == -1 && rx_channel == -1)
    {
        tx_channel = tx_chans->data[0];
        rx_channel = rx_chans->data[0];
    }
    printf("Using transmit channel %d and receive channel %d.\n", tx_channel,
           rx_channel);

    int cnt = 0;

    struct udpmm2s m_udpmm2s;
    m_udpmm2s.axidma_dev = axidma_dev;
    m_udpmm2s.tx_channel = tx_channel;
    m_udpmm2s.tx_buf = tx_buf;
    m_udpmm2s.tx_size = tx_size;

    pthread_t tids;
    int ret = pthread_create(&tids, NULL, udp_recv, (void *)&m_udpmm2s);
    if (ret != 0)
    {
        printf("pthread_create error: error_code=%d", ret);
    }

    printf("udp send test once\n");
    udp_send("111", 1320);

    int okCount = 0;
    int failCount = 0;
    int totalCount = 0;

    while (1)
    {
        totalCount++;
        rc = s2mm_test(axidma_dev, rx_channel, rx_buf, rx_size, rx_frame);
        if (rc < 0)
        {
            failCount++;
        }
        else
        {
            okCount++;
            printf("!!!!!!!!!!!!!!!!S2MM transfer test completed once! total count: %d\n", cnt);
            cnt++;
        }
        if (totalCount % 500 == 0)
        {
            printf("Total count:%d, Ok count: %d, fail count: %d\n", totalCount, okCount, failCount);
        }
        usleep(2);
    }

    printf("Exiting.\n\n");

free_rx_buf:
    axidma_free(axidma_dev, rx_buf, rx_size);
free_tx_buf:
    axidma_free(axidma_dev, tx_buf, tx_size);
destroy_axidma:
    axidma_destroy(axidma_dev);
ret:
    return rc;
}