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
#include "interdma.h"
#include "util_interdma.h"

/*----------------------------------------------------------------------------
 * Internal Definitons
 *----------------------------------------------------------------------------*/
#define HEAD_SIZE 8

// The size of data to send per transfer, in byte
#define TRANS_NUM 8
#define TRANS_SIZE ((int)(TRANS_NUM * sizeof(char)))

#define MAX_DATA_NUM (LDPC_K * ((2 << 8) - 1) / (8 * TRANS_NUM))
#define MAX_SIZE ((int)(TRANS_NUM * sizeof(char) * MAX_DATA_NUM))

// The pattern that we fill into the buffers
#define TEST_PATTERN(i) ((int)(0x1234ACDE ^ (i)))

// modu:11, mb:9, ldpcnum:15
// bin: 0001 0000, 0010 0000, 0011 0000, 0100 0000, 0101 "11""00, 1001" "0000, 0111 1"000, 1000 0000,
// #define TEST_DATA 0x102030405"8"607080
#define TEST_DATA 0x102030405c907880

// crc:false, ldpc_ok:19, amp:2,  modu:11, mb:9, ldpcnum:15
// bin: 0001 0000, 0010 0000, 0011 0000, "0""000 0100, 11"11" "11""00, 1000" "0000, 0110 0"000, 1000 0000,
// #define TEST_DATA 0x102030405"8"607080
#define TEST_DATA_S2MM 0x01203004fc806880

#define CHANNEL_CORRECT_GROUP 69
#define DATA_EQU_BLOCK 10
#define MODU_CHARACTOR 448
#define DATA_MAX MODU_CHARACTOR *(CHANNEL_CORRECT_GROUP * DATA_EQU_BLOCK - 1)
#define HOLE 512
#define DATA_TR(mb) ((mb + 22) * 256 - HOLE)
#define LDPC_K 5632

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
    *tx_size = MAX_SIZE;
    tx_frame->height = -1;
    tx_frame->width = -1;
    tx_frame->depth = -1;
    *rx_size = MAX_SIZE;
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
        transmit_buffer[i] = -16;
    }

    // To align
    // Fill in any leftover bytes if it's not aligned
    for (i = 0; i < tx_buf_size % sizeof(long); i++)
    {
        transmit_buffer[i] = -16;
    }

    return;
}

/* Initialize the tx buffers with 0, filling buffers with a preset
 * pattern. */
static void clean_tx_data(char *tx_buf, size_t tx_buf_size)
{
    size_t i;
    long *transmit_buffer;
    // two int 8 Byte, means 64bit data

    transmit_buffer = (long *)tx_buf;

    // Fill the buffer with integer patterns
    for (i = 0; i < tx_buf_size / sizeof(long); i++)
    {
        transmit_buffer[i] = 0;
    }

    // To align
    // Fill in any leftover bytes if it's not aligned
    for (i = 0; i < tx_buf_size % sizeof(long); i++)
    {
        transmit_buffer[i] = 0;
    }

    return;
}

static int mm2s_all_test(axidma_dev_t dev, int tx_channel, void *tx_buf,
                         int tx_size, struct axidma_video_frame *tx_frame)
{
    int rc = 0;

    char *p = tx_buf;

    // Initialize the buffer region we're going to transmit
    // data: all f
    clean_tx_data(tx_buf, tx_size);

    // printf("One way transfer!\n");
    // Perform the DMA transaction

    mm2s_f msf;
    msf.ldpcNum = 89;
    msf.Mb = 7;
    msf.modulation = QPSK;
    unsigned char pack[HEAD_SIZE] = {0};
    unsigned char pack_r[HEAD_SIZE] = {0};
    constrM2S(&msf, &pack);

    // revert in char, trans will revert in char
    // unknown if caused by LSB/MSB, this machine is LSB
    revert_char(&pack, HEAD_SIZE, &pack_r);

    int index = 0;

    p = p + index;
    memcpy(p, pack_r, HEAD_SIZE);

    index += HEAD_SIZE;
    p = p + index;

    int datalen_inbit = msf.ldpcNum * LDPC_K; // 501284
    // 7832
    int datalen_inbyte = (datalen_inbit % 8 != 0) ? (datalen_inbit / 8) : (datalen_inbit / 8 + 1);
    init_tx_data(p, datalen_inbyte);

    rc = axidma_oneway_transfer(dev, tx_channel, tx_buf, datalen_inbyte + HEAD_SIZE, true);
    if (rc < 0)
    {
        return rc;
    }

    // int bitnum = msf.ldpcNum * LDPC_K;
    // int wordnum = (bitnum % 64 == 0 ? (bitnum / 64) : (bitnum / 64 + 1));

    // int it = 0;
    // init_tx_data(tx_buf, tx_size);
    // while (wordnum)
    // {
    //     int rc = axidma_oneway_transfer(dev, tx_channel, tx_buf, tx_size, true);
    //     if (rc < 0)
    //     {
    //         printf("data tx failed\n");
    //         continue;
    //     }

    //     it++;
    //     --wordnum;

    //     long *index = tx_buf;

    //     printf("tx data %04d : %016lx \n", it, *index);
    // }

    return rc;
}

/* Initialize the rx buffers, filling buffers with a preset
 * pattern. */
static void clean_rx_data(char *rx_buf, size_t rx_buf_size)
{
    size_t i;
    long *receive_buffer;

    receive_buffer = (long *)rx_buf;

    // Fill the buffer with integer patterns
    for (i = 0; i < rx_buf_size / sizeof(long); i++)
    {
        receive_buffer[i] = 0;
    }

    // Fill in any leftover bytes if it's not aligned
    for (i = 0; i < rx_buf_size % sizeof(long); i++)
    {
        rx_buf[i] = 0;
    }

    return;
}

void getInfo(void * rx_buf)
{
    // get first word
    long *p_l = rx_buf;

    printf("After trans, data in first word : %016lx \n", *p_l);

    long data_s = *p_l;
    printf("data_S: 0x%lx\n", data_s);
    print_b(&data_s, sizeof(data_s));

    unsigned char charpack_s[8] = {0};
    long2char(data_s, charpack_s);

    int j = getHeadS2M(charpack_s);
    if (j == 1234)
    {
        unsigned char bitpack_s[64] = {0};
        char2bit(charpack_s, 8, bitpack_s);

        s2mm_f sf;
        getParamS2M(&sf, bitpack_s);

        printf("head:%d\n", j);
        printf("crc:%d\n", sf.crc_r);
        printf("ldpc_t:%d\n", sf.ldpc_tnum);
        printf("amp:%d\n", sf.amp_dresult);
        printf("modula:%d\n", sf.modu);
        printf("mb:%d\n", sf.Mb);
        printf("ldpcnum:%d\n", sf.ldpcnum);

        int bitnum = LDPC_K * sf.ldpcnum;
        int wordnum = (bitnum % 64 == 0 ? (bitnum / 64) : (bitnum / 64 + 1));

        printf("wordnum:%d\n", wordnum);

        // do
        // {
        //     rc = axidma_oneway_transfer(dev, rx_channel, rx_buf + HEAD_SIZE, wordnum * 8, true);
        // } while (rc < 0);

        printf("head:%d\n", j);

        int it = 0;
        long *index_l = rx_buf;
        // index_l++;
        int ct = sf.ldpcnum *LDPC_K/PACK_LEN +1;
        while (ct)
        {
            printf("now data %d: %016lx \n", it, *index_l);
            it++;
            index_l++;
            ct--;
        }
    }
}

static int s2mm_all_test(axidma_dev_t dev, int rx_channel, void *rx_buf,
                         int rx_size, struct axidma_video_frame *rx_frame)
{
    int rc = 0;

    // Initialize the buffer region we're going to transmit
    // before rx anything, data is 1 in long
    clean_rx_data(rx_buf, rx_size);

    // Perform the DMA transaction
    do
    {
        rc = axidma_oneway_transfer(dev, rx_channel, rx_buf, MAX_SIZE, true);
    } while (rc < 0);

    getInfo(rx_buf);

    return rc;
}

void *udp_recv(void *args)
{
    struct udpmm2s *arg_thread1;
    arg_thread1 = (struct udpmm2s *)args;
    // /* sock_fd --- socket文件描述符 创建udp套接字*/
    // int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    // if (sock_fd < 0)
    // {
    //     perror("socket");
    //     exit(1);
    // }

    printf("MM2S ongoing\n\n");

    // /* 将套接字和IP、端口绑定 */
    // struct sockaddr_in addr_serv;
    // int len;
    // memset(&addr_serv, 0, sizeof(struct sockaddr_in)); // 每个字节都用0填充
    // addr_serv.sin_family = AF_INET;                    // 使用IPV4地址
    // addr_serv.sin_port = htons(LOCAL_PORT);            // 端口
    // /* INADDR_ANY表示不管是哪个网卡接收到数据，只要目的端口是SERV_PORT，就会被该应用程序接收到 */
    // addr_serv.sin_addr.s_addr = htonl(INADDR_ANY); // 自动获取IP地址
    // len = sizeof(addr_serv);

    // /* 绑定socket */
    // if (bind(sock_fd, (struct sockaddr *)&addr_serv, sizeof(addr_serv)) < 0)
    // {
    //     perror("bind error:");
    //     exit(1);
    // }

    // int recv_num;
    // int send_num;
    // char send_buf[20] = "i am server!";
    // char recv_buf[20];
    // struct sockaddr_in addr_client;

    int total = 0;
    int ok = 0;
    int fail = 0;
    while (1)
    {
        // recv_num = recvfrom(sock_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&addr_client, (socklen_t *)&len);

        // if (recv_num < 0)
        // {
        //     perror("recvfrom error:");
        //     exit(1);
        // }

        // recv_buf[recv_num] = '\0';
        // printf("receive %d bytes: \n", recv_num);
        // for (int i = 0; i < recv_num; i++)
        // {
        //     printf("%02x ", recv_buf[i]);
        // }
        // printf("\n");

        // if (recv_num > 0)
        // {
        total += 1;
        int rc = mm2s_all_test(arg_thread1->axidma_dev, arg_thread1->tx_channel, arg_thread1->tx_buf, arg_thread1->tx_size,
                               arg_thread1->tx_frame);
        if (rc < 0)
        {
            fail++;
        }
        else
        {
            ok++;
        }
        if (total % 50 == 0)
        {
            printf("mm2s total: %d, ok: %d, fail: %d\n", total, ok, fail);
        }
        // }
        usleep(10);
    }

    // close(sock_fd);

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

    checkLSB();

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

    /*
    udp receive not used for now, just use the mm2s part to send data
    */

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
        goto free_rx_buf;
    }

    // printf("udp send test once\n");
    // udp_send("111", 1320);

    int okCount = 0;
    int failCount = 0;
    int totalCount = 0;

    printf("s2mm start\n");
    while (1)
    {
        totalCount++;
        printf("s2mm total:%d\n", totalCount);
        rc = s2mm_all_test(axidma_dev, rx_channel, rx_buf, rx_size, rx_frame);
        if (rc < 0)
        {
            failCount++;
        }
        else
        {
            okCount++;
            printf("!!!!!!!!!!!!!!!!S2MM transfer test completed once! total count: %d\n", okCount);
        }
        if (totalCount % 5 == 0)
        {
            printf("Total count:%d, Ok count: %d, fail count: %d\n", totalCount, okCount, failCount);
        }
        usleep(1000 * 10);
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