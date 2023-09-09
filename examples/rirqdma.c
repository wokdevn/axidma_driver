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
#define TRANS_NUM 6 * 1320
#define TRANS_SIZE ((int)(TRANS_NUM * sizeof(char)))

#define BUFFER_SIZE_MAX TRANS_NUM * 5

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

#define USLEEP 1000000

#define TR_DATA_TEST
#define TEST_MB 7
#define TEST_MODU QAM16
#define MB_MAX 63
#define EVM_COUNT 900
#define PRINT_EVM
#define PRINT_DATA
// #define TEST_EDGE
// #define SKIP
// #define PRINT_MM2S_INIT

typedef struct s2mm_struct
{
    axidma_dev_t axidma_dev;
    char *rx_buf;
} s2mm_struct;

pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t mutex_mm2s = PTHREAD_MUTEX_INITIALIZER; // unlock ed
pthread_cond_t flag_mm2s = PTHREAD_COND_INITIALIZER;    // set ed status
pthread_mutex_t mutex_s2mm = PTHREAD_MUTEX_INITIALIZER; // unlock ed
pthread_cond_t flag_s2mm = PTHREAD_COND_INITIALIZER;    // set ed status

int random_flag = 2;

int sec_flag = 1;

// The DMA context passed to the helper thread, who handles remainder channels
struct udpmm2s
{
    axidma_dev_t axidma_dev;
    int tx_channel;
    char *tx_buf;
    size_t tx_size;
    struct axidma_video_frame *tx_frame;
};

void process_s2mm(void *args);

int txnum = 0;
void txcall(int channelid, char *p)
{
    printf("\ntx channel id:%d\n", channelid);
    pthread_mutex_lock(&mutex_mm2s);
    txnum++;
    printf("enter tx call, tx num:%d\n", txnum);
    pthread_cond_signal(&flag_mm2s);
    pthread_mutex_unlock(&mutex_mm2s);
}

int rxnum = 0;
void rxcall(int channelid, void *p)
{
    printf("rx call\n");
    // pthread_mutex_lock(&mutex_s2mm);
    rxnum++;
    printf("enter rx call, rxnum:%d\n", rxnum);
    // pthread_cond_signal(&flag_s2mm);
    // pthread_mutex_unlock(&mutex_s2mm);

    unsigned char *processdata = (unsigned char *)malloc(TRANS_SIZE * sizeof(char));
    memcpy(processdata, p, TRANS_SIZE);

    // pthread_t tids;
    // int ret = pthread_create(&tids, NULL, (void *)process_s2mm, processdata);
    // if (ret != 0)
    // {
    //     printf("pthread_create error: error_code=%d", ret);
    // }

    // printf data
    int it = 0;
    long *index_l = processdata;
    // index_l++;
    int totalct = 1320 * 4 / 8;
    int ct = totalct + 5;
    // int ct = 10;

    free(processdata);

    s2mm_struct *s = (s2mm_struct *)p;

    axidma_oneway_transfer(s->axidma_dev, channelid, s->rx_buf, TRANS_SIZE, false);
}

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
    *tx_size = BUFFER_SIZE_MAX;
    tx_frame->height = -1;
    tx_frame->width = -1;
    tx_frame->depth = -1;
    *rx_size = BUFFER_SIZE_MAX;
    rx_frame->height = -1;
    rx_frame->width = -1;
    rx_frame->depth = -1;

    return 0;
}

/* Initialize the tx buffers, filling buffers with a preset
 * pattern. */
// size in char
static void init_tx_data(char *tx_buf, size_t tx_buf_size)
{
    size_t i;
    long *transmit_buffer;
    // two int 8 Byte, means 64bit data

    transmit_buffer = (long *)tx_buf;

    // Fill the buffer with integer patterns
    for (i = 0; i < tx_buf_size / sizeof(long); i++)
    {
        transmit_buffer[i] = 0x0102030400000000 | i;
    }

    // To align
    // Fill in any leftover bytes if it's not aligned
    for (i = 0; i < tx_buf_size % sizeof(long); i++)
    {
        transmit_buffer[i + tx_buf_size / sizeof(long)] = 0x0102030400000000 | (i + tx_buf_size / sizeof(long));
    }

    return;
}

/* Initialize the tx buffers with 0, filling buffers with a preset
 * pattern. */
static void clean_tx_data(char *tx_buf, size_t tx_buf_size)
{
    size_t i;
    char *transmit_buffer;
    // two int 8 Byte, means 64bit data

    transmit_buffer = tx_buf;

    // Fill the buffer with integer patterns
    for (i = 0; i < tx_buf_size; i++)
    {
        transmit_buffer[i] = 0;
    }

    return;
}

static int random_mm2s(mm2s_f *msf)
{
    // mod:BPSK,QPSK,16QAM,64QAM
    // MB:0~63
    // LDPCNUM:0~362

    // int ldpc_num = randBtw(0, 362);
    // int mb = randBtw(0, MB_MAX);
    // int modu = randBtw(1, 3);//bpsk useless, 1, 2, 3 to QPSK 16QAM 64QAM

    int mb, modu;

#ifdef TR_DATA_TEST
    mb = TEST_MB;
    modu = TEST_MODU;
#endif

    int modu_cal;
    switch (modu)
    {
    case BPSK:
        modu_cal = 1;
        break;
    case QPSK:
        modu_cal = 2;
        break;
    case QAM16:
        modu_cal = 4;
        break;
    case QAM64:
        modu_cal = 6;
        break;
    default:
        break;
    }
    int cal_u = DATA_MAX * modu_cal;
    int cal_d = DATA_TR(mb);

    printf("mm2s cal, DATA_MAX : %d, modu_cal : %d, mb : %d, cal_d : %d\n",
           DATA_MAX, modu_cal, mb, cal_d);

    int ldpc_num;

    ldpc_num = cal_u / cal_d;

    msf->ldpcNum = ldpc_num;
    msf->Mb = mb;
    msf->modulation = modu;

    return 0;
}

static int mm2s_all_test(axidma_dev_t dev, int tx_channel, void *tx_buf,
                         int tx_size, struct axidma_video_frame *tx_frame)
{
    int rc = 0;

    char *p_tx_buf = tx_buf;

    // Initialize the buffer region we're going to transmit
    // data: all 0
    clean_tx_data(tx_buf, MAX_SIZE);

    // //check if cleaned
    // char *zero_check = tx_buf;
    // int zero_flag = 0;
    // for (int i = 0; i < MAX_SIZE; ++i)
    // {
    //     if (*zero_check != 0)
    //     {
    //         zero_flag = 1;
    //     }
    //     zero_check++;
    // }

    // if (zero_flag)
    // {
    //     printf("tx mm2s error: not clean !!!!\n");
    // }
    // else
    // {
    //     printf("tx mm2s ok: clean\n");
    // }

    // printf("One way transfer!\n");
    // Perform the DMA transaction

    mm2s_f msf;

#ifdef TEST_EDGE
    if (random_flag == 0)
    {
        random_mm2s(&msf);
    }
    else if (random_flag == 1)
    {
        msf.ldpcNum = 16;
        msf.Mb = 63;
        msf.modulation = BPSK;
    }
    else
    {
        msf.ldpcNum = 362;
        msf.Mb = 0;
        msf.modulation = QAM64;
    }
#else
    random_mm2s(&msf);
#endif
    // msf.ldpcNum = 89;
    // msf.Mb = 7;
    // msf.modulation = QPSK;
    printf("mm2s ldpc num:%d\n", msf.ldpcNum);
    printf("mm2s mb:%d\n", msf.Mb);
    printf("mm2s modu:%d", msf.modulation);
    char pack[HEAD_SIZE] = {0};
    char pack_r[HEAD_SIZE] = {0};
    constrM2S(&msf, (char *)(&pack));

    printf("\nmm2s first: 0x");
    for (int i = 0; i < HEAD_SIZE; ++i)
    {
        printf("%02x", pack[i]);
    }
    printf("\n");

    // revert in char, trans will revert in char
    // unknown if caused by LSB/MSB, this machine is LSB
    // TODO:
    revert_char((char *)(&pack), HEAD_SIZE, (char *)(&pack_r));

    int index = 0;

    p_tx_buf = p_tx_buf + index;
    memcpy(p_tx_buf, pack_r, HEAD_SIZE);

    index += HEAD_SIZE;
    p_tx_buf = p_tx_buf + index;

    int datalen_inbit = msf.ldpcNum * LDPC_K; // 501248
    printf("datalen_inbit:%d\n", datalen_inbit);

    // 62656
    int datalen_inbyte = (datalen_inbit % 8 == 0) ? (datalen_inbit / 8) : (datalen_inbit / 8 + 1);
    printf("datalen_inbyte:%d\n", datalen_inbyte);

    printf("size of long:%ld\n", sizeof(long));
    init_tx_data(p_tx_buf, datalen_inbyte);

#ifdef PRINT_MM2S_INIT
    // show inited data
    int it = 0;
    long *index_l = tx_buf;
    // index_l++;
    int totalct = (datalen_inbyte % 8 == 0) ? (datalen_inbyte / 8) : (datalen_inbyte / 8 + 1);
    int ct = totalct;
    // int ct = 10;
    while (ct)
    {
        printf("mm2s now data %d: %016lx \n", it, *index_l);
        it++;
        index_l++;
        ct--;
        // if (it == 5)
        // {
        //     int skip = totalct-30;
        //     it += skip;
        //     index_l += skip;
        //     ct -= skip;
        // }
    }
#endif

    // rc = axidma_oneway_transfer(dev, tx_channel, tx_buf, datalen_inbyte + HEAD_SIZE, false);
    // if (rc < 0)
    // {
    //     perror("mm2s one way error:");
    //     return rc;
    // }

    axidma_oneway_transfer(dev, tx_channel, tx_buf, datalen_inbyte + HEAD_SIZE, false);
    pthread_mutex_lock(&mutex_mm2s);
    pthread_cond_wait(&flag_mm2s, &mutex_mm2s);
    printf("mm2s get flag\n");
    pthread_mutex_unlock(&mutex_mm2s);

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
        rx_buf[i + rx_buf_size / sizeof(long)] = 0;
    }

    return;
}

void getInfo(void *rx_buf, int *lcnum)
{
    printf("S2MM INFO: \n");
    // get first word
    long *p_l = rx_buf;

    printf("s2mm After trans, data in first word : %016lx \n", *p_l);

    long data_s = *p_l;
    printf("data_S: 0x%lx\n", data_s);
    print_b(&data_s, sizeof(data_s));

    char charpack_s[8] = {0};
    long2char(data_s, charpack_s);

    int j = getHeadS2M(charpack_s);

#ifdef PRINT_EVM
    if (j == 1212)
    {
        int it = 0;
        long *index_l = rx_buf;

        int totalct = EVM_COUNT * 2;
        int ct = totalct + 5;
        // int ct = 10;

        pthread_mutex_lock(&print_mutex);

        while (ct)
        {
            printf("s2mm evm data %d: %016lx \n", it, *index_l);
            it++;
            index_l++;
            ct--;
#ifdef SKIP
            if (it == 5)
            {
                int skip = totalct - 30;
                it += skip;
                index_l += skip;
                ct -= skip;
            }
#endif
        }

        pthread_mutex_unlock(&print_mutex);
    }
#endif

    if (j == 1234)
    {
        char bitpack_s[64] = {0};
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

        *lcnum = sf.ldpcnum;

        int bitnum = LDPC_K * sf.ldpcnum;
        int wordnum = (bitnum % 64 == 0 ? (bitnum / 64) : (bitnum / 64 + 1));

        printf("wordnum:%d\n", wordnum);

        // do
        // {
        //     rc = axidma_oneway_transfer(dev, rx_channel, rx_buf + HEAD_SIZE, wordnum * 8, true);
        // } while (rc < 0);

        // printf("head:%d\n", j);

#ifdef PRINT_DATA
        // printf data
        int it = 0;
        long *index_l = rx_buf;
        // index_l++;
        int totalct = sf.ldpcnum * LDPC_K / PACK_LEN + 1;
        int ct = totalct + 5;
        // int ct = 10;

        pthread_mutex_lock(&print_mutex);
        while (ct)
        {
            printf("s2mm now data %d: %016lx \n", it, *index_l);
            it++;
            index_l++;
            ct--;
#ifdef SKIP
            if (it == 5)
            {
                int skip = totalct - 30;
                it += skip;
                index_l += skip;
                ct -= skip;
            }
#endif
        }
        pthread_mutex_unlock(&print_mutex);

        // it = totalct - 10;
        // index_l = rx_buf + it;
        // while (it < totalct + 1000)
        // {
        //     printf("s2mm now data %d: %016lx \n", it, *index_l);
        //     it++;
        //     index_l++;
        // }
        // printf("s2mm now data %d: %016lx \n", it, *index_l);

#endif
    }
}

void process_s2mm(void *args)
{
    long *processdata;
    processdata = (long *)args;

    // printf data
    int it = 0;
    long *index_l = processdata;
    // index_l++;
    int totalct = 1320 * 4 / 8;
    int ct = totalct + 5;
    // int ct = 10;

    pthread_mutex_lock(&print_mutex);
    while (ct)
    {
        printf("s2mm now data %d: %016lx \n", it, *index_l);
        it++;
        index_l++;
        ct--;
    }
    pthread_mutex_unlock(&print_mutex);

    free(processdata);

    printf("udp send start v2.0>>>>>>>>>>>>>>>>>>>>>>>\n");

    // char onetwo[1320];
    // char threefour[1320];
    // char fivesix[1320];
    // char seveneight[1320];

    // char *char_ptr = args;

    // int j = 0;
    // for (int i = 0; i < TRANS_SIZE; ++i)
    // {
    //     if (i < 1320)
    //     {
    //         onetwo[j] = *char_ptr;
    //     }
    //     else if (i < 1320 * 2)
    //     {
    //         threefour[j] = *char_ptr;
    //     }
    //     else if (i < 1320 * 3)
    //     {
    //         fivesix[j] = *char_ptr;
    //     }
    //     else if (i < 1320 * 4)
    //     {
    //         seveneight[j] = *char_ptr;
    //     }
    //     j++;
    //     char_ptr++;
    //     if (j == 1320)
    //     {
    //         j = 0;
    //     }
    // }
    // // printf("before send\n");

    // long *it_l = (long *)onetwo;
    // // printf("1:%lx",*it);
    // udp_send(onetwo, 1320);

    // it_l = (long *)threefour;
    // // printf("2:%lx",*it);
    // udp_send(threefour, 1320);

    // it_l = (long *)fivesix;
    // // printf("3:%lx",*it);
    // udp_send(fivesix, 1320);

    // it_l = (long *)seveneight;
    // // printf("4:%lx",*it);
    // udp_send(seveneight, 1320);
}

static int s2mm_all_test(axidma_dev_t dev, int rx_channel, void *rx_buf,
                         int rx_size, struct axidma_video_frame *rx_frame)
{
    int rc = 0;

    // Initialize the buffer region we're going to transmit
    // before rx anything, data is 0 in long
    clean_rx_data(rx_buf, rx_size);

    // //check if cleaned
    // char *zero_check = rx_buf;
    // int zero_flag = 0;
    // for (int i = 0; i < rx_size; ++i)
    // {
    //     if (*zero_check != 0)
    //     {
    //         zero_flag = 1;
    //     }
    //     zero_check++;
    // }

    // if (zero_flag)
    // {
    //     printf("rx s2mm error: rx not clean !!!!\n");
    // }
    // else
    // {
    //     printf("rx s2mm ok: rx clean !!!!\n");
    // }

    // // Perform the DMA transaction
    // do
    // {
    //     rc = axidma_oneway_transfer(dev, rx_channel, rx_buf, MAX_SIZE, true);
    // } while (rc < 0);

    // rc = axidma_oneway_transfer(dev, rx_channel, rx_buf, 8*4, true);

    // //no mutex no intr
    // rc = axidma_oneway_transfer(dev, rx_channel, rx_buf, MAX_SIZE, false);

    // if (rc < 0)
    // {
    //     return rc;
    // }

    // pthread_mutex_lock(&mutex_s2mm);
    axidma_oneway_transfer(dev, rx_channel, rx_buf, rx_size, false);
    // pthread_cond_wait(&flag_s2mm, &mutex_s2mm);

    // printf("s2mm get flag\n");
    // unsigned char *processdata = (unsigned char *)malloc(rx_size * sizeof(char));
    // memcpy(processdata, rx_buf, rx_size);

    // pthread_t tids;
    // int ret = pthread_create(&tids, NULL, (void *)process_s2mm, processdata);
    // if (ret != 0)
    // {
    //     printf("pthread_create error: error_code=%d", ret);
    //     return -1;
    // }

    // pthread_mutex_unlock(&mutex_s2mm);

    // int ldpcnum;

    // getInfo(rx_buf, &ldpcnum);

    // double utimedata = 500 / 512;
    // unsigned int sleeptime = ldpcnum * utimedata;
    // usleep(sleeptime);

    return 0;
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
        if (total == 2)
        {
            random_flag--;
        }
        if (total == 3)
        {
            random_flag--;
        }
        int rc = mm2s_all_test(arg_thread1->axidma_dev, arg_thread1->tx_channel, arg_thread1->tx_buf, arg_thread1->tx_size,
                               arg_thread1->tx_frame);
        // // useless cause return value will be 0
        // if (rc < 0)
        // {
        //     fail++;
        //     printf("mm2s send fail\n");
        // }
        // else
        // {

        //     ok++;
        printf("mm2s send count: %d\n", total);
        // }

        // if (total % 500 == 0)
        // {
        //     printf("\n<<<<<<<<<<<<<<<<<<\n\nmm2s total: %d, ok: %d, fail: %d\n<<<<<<<<<<<<<<<<<<<<\n\n", total, ok, fail);
        // }
        // }

        usleep(USLEEP);
        // break;
    }

    // close(sock_fd);

    return 0;
}

/*----------------------------------------------------------------------------
 * Command-line Interface
 *----------------------------------------------------------------------------*/
int waitFlag = 1;
int onceFlag = 0;
int g_memDev;
void *map_base_dma;
void *map_base_apb;

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

void rece_cb(int channelid, void *data)
{
    int *rx_buf_tmp = (int *)data;

    printf("58 : 0x%x \n", gw_ReadReg(0xA0000058));
    printf("30 : 0x%x \n", gw_ReadReg(0xA0000030));
    printf("34 : 0x%x \n", gw_ReadReg(0xA0000034));

    // for (int i = 0; i < 40; i++)
    // {
    //     printf("%d ,", rx_buf_tmp[i]);
    // }

    int ct = 1320 * 6 / 8;
    int it = 0;
    long *index_l = data;
    while (ct)
    {
        printf("s2mm now data %d: %016lx \n", it, *index_l);
        it++;
        index_l++;
        ct--;
    }

    long *pack_l = data;

    if (pack_l[0] != 0x0002000100020001 || pack_l[165] != 0x0004000300040003 || pack_l[330] != 0x0006000500060005 || pack_l[495] != 0x0008000700080007 || pack_l[660] != 0x0009000800090008 || pack_l[825] != 0x0009000100090001)
    {
        printf("\nINFO: callback func triggerd,channelid: %d \n", channelid);
        waitFlag = 0;
        return;
    }

    char onetwo[1320];
    char threefour[1320];
    char fivesix[1320];
    char seveneight[1320];

    char eightnine[1320];
    char onenine[1320];
    char twonine[1320];
    char threenine[1320];

    char *char_ptr = data;

    memcpy(onetwo, char_ptr, 1320);
    memcpy(threefour, char_ptr + 1320, 1320);
    memcpy(fivesix, char_ptr + 1320 * 2, 1320);
    memcpy(seveneight, char_ptr + 1320 * 3, 1320);

    memcpy(eightnine, char_ptr + 1320 * 4, 1320);
    memcpy(onenine, char_ptr + 1320 * 5, 1320);
    memcpy(twonine, char_ptr + 1320 * 6, 1320);
    memcpy(threenine, char_ptr + 1320 * 7, 1320);

    // change head
    long *head = onetwo;
    *head = 0x0008000700080007;
    *(head + 1) = 0x000d000d000d000d;

    head = threefour;
    *head = 0x0006000500060005;
    *(head + 1) = 0x000d000d000d000d;

    head = fivesix;
    *head = 0x0008000700080007;
    *(head + 1) = 0x000e000e000e000e;

    head = seveneight;
    *head = 0x0006000500060005;
    *(head + 1) = 0x000e000e000e000e;

    head = eightnine;
    *head = 0x0008000700080007;
    *(head + 1) = 0x000f000f000f000f;

    long stardata_s = *(head + 3);
    int same_count = 0;
    for (int i = 4; i < 50; ++i)
    {
        if ((*(head + i)) == stardata_s)
        {
            same_count++;
        }
    }
    int changeflag = 0;
    if (same_count >= 40)
    {
        changeflag = 1;
    }
    head = onenine;
    *head = 0x0006000500060005;
    *(head + 1) = 0x000f000f000f000f;
    if (changeflag == 1)
    {
        long *change_p = head + 2;
        for (int i = 0; i < 163; ++i)
        {
            *change_p = 0;
            change_p++;
        }
    }

    head = twonine;
    *head = 0x0008000700080007;
    *(head + 1) = 0x000c000c000c000c;

    head = threenine;
    *head = 0x0006000500060005;
    *(head + 1) = 0x000c000c000c000c;

    // printf("before send\n");
    long *checki = onetwo;
    for (int i = 0; i < 1320 / 8; ++i)
    {
        printf("onetwo now data %d: %016lx \n", i, *checki);
        checki++;
    }

    checki = threefour;
    for (int i = 0; i < 1320 / 8; ++i)
    {
        printf("threefour now data %d: %016lx \n", i, *checki);
        checki++;
    }

    checki = fivesix;
    for (int i = 0; i < 1320 / 8; ++i)
    {
        printf("fivesix now data %d: %016lx \n", i, *checki);
        checki++;
    }

    checki = seveneight;
    for (int i = 0; i < 1320 / 8; ++i)
    {
        printf("seveneitht now data %d: %016lx \n", i, *checki);
        checki++;
    }

    checki = eightnine;
    for (int i = 0; i < 1320 / 8; ++i)
    {
        printf("eightnine now data %d: %016lx \n", i, *checki);
        checki++;
    }

    checki = onenine;
    for (int i = 0; i < 1320 / 8; ++i)
    {
        printf("onenine now data %d: %016lx \n", i, *checki);
        checki++;
    }

    checki = twonine;
    for (int i = 0; i < 1320 / 8; ++i)
    {
        printf("twonine now data %d: %016lx \n", i, *checki);
        checki++;
    }

    checki = threenine;
    for (int i = 0; i < 1320 / 8; ++i)
    {
        printf("threenine now data %d: %016lx \n", i, *checki);
        checki++;
    }

    if (!sec_flag)
    {
        waitFlag = 0;
        return;
    }

    sec_flag = 0;

    long *it_l = (long *)onetwo;
    // printf("1:%lx",*it);
    udp_send(onetwo, 1320);

    it_l = (long *)threefour;
    // printf("2:%lx",*it);
    udp_send(threefour, 1320);

    it_l = (long *)fivesix;
    // printf("3:%lx",*it);
    udp_send(fivesix, 1320);

    it_l = (long *)seveneight;
    // printf("4:%lx",*it);
    udp_send(seveneight, 1320);

    it_l = (long *)eightnine;
    // printf("1:%lx",*it);
    udp_send(eightnine, 1320);

    it_l = (long *)onenine;
    // printf("2:%lx",*it);
    udp_send(onenine, 1320);

    it_l = (long *)twonine;
    // printf("3:%lx",*it);
    udp_send(twonine, 1320);

    it_l = (long *)threenine;
    // printf("4:%lx",*it);
    udp_send(threenine, 1320);

    printf("\nINFO: callback func triggerd,channelid: %d \n", channelid);

    waitFlag = 0;
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

    printf("Enter main v6.0 irq axidma test\n");

    init_reg_dev();

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
    printf("\tTx size:%ld \n", tx_size);
    if (!use_vdma)
    {
        printf("\tTransmit Buffer Size: %ld Byte\n", (tx_size));
        printf("\tReceive Buffer Size: %ld Byte\n", (rx_size));
    }

    // Initialize the AXI DMA device
    axidma_dev = axidma_init();
    if (axidma_dev == NULL)
    {
        fprintf(stderr, "Failed to initialize the AXI DMA device.\n");
        rc = 1;
        goto ret;
    }

    // // Map memory regions for the transmit and receive buffers
    // tx_buf = axidma_malloc(axidma_dev, tx_size);
    // if (tx_buf == NULL)
    // {
    //     perror("Unable to allocate transmit buffer from the AXI DMA device.");
    //     rc = -1;
    //     goto destroy_axidma;
    // }
    rx_buf = axidma_malloc(axidma_dev, BUFFER_SIZE_MAX);
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

    axidma_set_callback(axidma_dev, rx_channel, rece_cb, (void *)rx_buf);

    printf("58 : 0x%x \n", gw_ReadReg(0xA0000058));
    printf("30 : 0x%x \n", gw_ReadReg(0xA0000030));
    printf("34 : 0x%x \n", gw_ReadReg(0xA0000034));

    axidma_oneway_transfer(axidma_dev, rx_channel, rx_buf, BUFFER_SIZE_MAX, false);

    gw_WriteReg(0xA0010004, 0x1);
    // Transmit the buffer to DMA a single time
    // rc = single_transfer_test(axidma_dev, tx_channel, tx_buf, tx_size,
    //         rx_channel, rx_buf, rx_size, send_buf);
    // if (rc < 0) {
    //     goto free_rx_buf;
    // }
    printf("Single transfer test successfully completed!\n");

    printf("58 : 0x%x \n", gw_ReadReg(0xA0000058));
    printf("30 : 0x%x \n", gw_ReadReg(0xA0000030));
    printf("34 : 0x%x \n", gw_ReadReg(0xA0000034));

    struct timeval tv_begin, tv_end, tresult;

    struct timeval tv_begin_s, tv_end_s, tresult_s;

    // double timeuse_s = tresult.tv_sec + (1.0 * tresult.tv_usec) / 1000000; //  精确到秒
    gettimeofday(&tv_begin_s, NULL);
    double timeuse_ms_s;

    while (1)
    {
        gettimeofday(&tv_begin, NULL);
        while (waitFlag)
        {
            gettimeofday(&tv_end, NULL);
            timersub(&tv_end, &tv_begin, &tresult);
            double timeuse_ms = tresult.tv_sec * 1000 + (1.0 * tresult.tv_usec) / 1000; //  精确到毫秒
            if (timeuse_ms > 20)
            {
                axidma_stop_transfer(axidma_dev, rx_channel);
                break;
            }
        }
        printf("58 : 0x%x \n", gw_ReadReg(0xA0000058));
        printf("30 : 0x%x \n", gw_ReadReg(0xA0000030));
        printf("34 : 0x%x \n", gw_ReadReg(0xA0000034));

        gettimeofday(&tv_end_s, NULL);
        timersub(&tv_end_s, &tv_begin_s, &tresult_s);
        timeuse_ms_s = tresult_s.tv_sec * 1000 + (1.0 * tresult_s.tv_usec) / 1000; //  精确到毫秒
        if (timeuse_ms_s > 100)
        {
            if (!sec_flag)
            {
                sec_flag = 1;
            }
            gettimeofday(&tv_begin_s, NULL);
        }

        axidma_oneway_transfer(axidma_dev, rx_channel, rx_buf, BUFFER_SIZE_MAX, false);
        waitFlag = 1;
        if (onceFlag)
            break;

        printf("Next Single transfer !\n");
    }

    printf("Single transfer End!\n");

    // printf("tx channel:%d>>>>>>>>>>>>>>>>>>>>\n", tx_channel);
    // printf("rx channel:%d>>>>>>>>>>>>>>>>>>>>\n", rx_channel);
    // axidma_set_callback(axidma_dev, tx_channel, (void *)txcall, NULL);
    // s2mm_struct struct_s2mm;
    // struct_s2mm.axidma_dev = axidma_dev;
    // struct_s2mm.rx_buf = rx_buf;

    // axidma_set_callback(axidma_dev, rx_channel, (void *)rxcall, (void*)(&struct_s2mm));

    // /*
    // udp receive not used for now, just use the mm2s part to send data
    // */

    // struct udpmm2s m_udpmm2s;
    // m_udpmm2s.axidma_dev = axidma_dev;
    // m_udpmm2s.tx_channel = tx_channel;
    // m_udpmm2s.tx_buf = tx_buf;
    // m_udpmm2s.tx_size = tx_size;

    // // pthread_t tids;
    // // int ret = pthread_create(&tids, NULL, udp_recv, (void *)&m_udpmm2s);
    // // if (ret != 0)
    // // {
    // //     printf("pthread_create error: error_code=%d", ret);
    // //     goto free_rx_buf;
    // // }

    // // printf("udp send test once\n");
    // // udp_send("111", 1320);

    // int okCount = 0;
    // int failCount = 0;
    // int totalCount = 0;

    // struct timeval tv_begin, tv_end, tresult;
    // gettimeofday(&tv_begin, NULL);

    // printf("s2mm start\n");
    // // while (1)
    // // {
    // totalCount++;
    // printf(">>>>>>>>>>>>>>>>s2mm count %d\n", totalCount);
    // rc = s2mm_all_test(axidma_dev, rx_channel, rx_buf, rx_size, rx_frame);
    // // //useless when intr mod, cause return value will always be 0 if (rc < 0)
    // // {
    // //     perror("s2mm fail:");
    // //     failCount++;
    // // }
    // // else
    // // {
    // //     printf("s2mm success\n");
    // //     okCount++;
    // // }
    // // if (totalCount % 500 == 0)
    // // {
    // //     printf("s2mm Total count:%d, Ok count: %d, fail count: %d\n", totalCount, okCount, failCount);
    // // }
    // usleep(USLEEP);

    // gettimeofday(&tv_end, NULL);

    // timersub(&tv_end, &tv_begin, &tresult);

    // double timeuse_s = tresult.tv_sec + (1.0 * tresult.tv_usec) / 1000000; //  精确到秒

    // double timeuse_ms = tresult.tv_sec * 1000 + (1.0 * tresult.tv_usec) / 1000; //  精确到毫秒

    // printf("timeuse in ms: %f \n", timeuse_ms);
    // // }

    // while (1)
    // {
    // }

    // printf("Exiting.\n\n");

free_rx_buf:
    axidma_free(axidma_dev, rx_buf, rx_size);
free_tx_buf:
    axidma_free(axidma_dev, tx_buf, tx_size);
destroy_axidma:
    axidma_destroy(axidma_dev);
ret:
    return rc;
}