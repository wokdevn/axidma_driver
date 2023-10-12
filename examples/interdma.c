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

#include "udprecv.h"
#include "udpsend.h"
#include "interdma.h"
#include "util_interdma.h"
#include "mgpio.h"

/*----------------------------------------------------------------------------
 * Internal Definitons
 *----------------------------------------------------------------------------*/
#define HEAD_SIZE 8 // 8byte

#define MAX_DATA_NUM (LDPC_K * ((2 << 8) - 1))            // in bit
#define MAX_SIZE ((int)(sizeof(char) * MAX_DATA_NUM / 8)) // in byte

#define BUF_SIZE MAX_SIZE * 2

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
#define TEST_MODU QAM64
#define MB_MAX 63
#define EVM_COUNT 300
// #define TEST_EDGE
// #define SKIP
// #define PRINT_MM2S_INIT

pthread_mutex_t gpio_mutex = PTHREAD_MUTEX_INITIALIZER;

int random_flag = 2;

int test_mb_p, test_modu_p;
int print_evm_p = 0, print_data_p = 0, gpio_ch = 0;

int evm_flag = 0;
int txnum = 0;
int rxnum = 0;

int tx_wait_flag = 0;
int rx_wait_flag = 1;

// The DMA context passed to the helper thread, who handles remainder channels
struct udpmm2s
{
    axidma_dev_t axidma_dev;
    int tx_channel;
    char *tx_buf;
    size_t tx_size;
    struct axidma_video_frame *tx_frame;
};

void txcall(int id, void *p);
void rxcall(int id, void *data);
static int init_args(int *tx_channel, int *rx_channel, size_t *tx_size, size_t *rx_size);
static void init_tx_data(char *tx_buf, size_t tx_buf_size);
static void clean_tx_data(char *tx_buf, size_t tx_buf_size);
static int random_mm2s(mm2s_f *msf);
static int mm2s_all_test(axidma_dev_t dev, int tx_channel, void *tx_buf, int tx_size);
void getInfo(void *rx_buf, int *lcnum);
void *udp_recv(void *args);
static void print_usage(int help);
static int parse_args(int argc, char **argv);
void *gpiocontrol();

void txcall(int id, void *p)
{
    txnum++;
    tx_wait_flag = 0;
}

void rxcall(int id, void *data)
{
    rxnum++;

    int ldpcnum = 0;
    getInfo(data, &ldpcnum);

    rx_wait_flag = 0;
}

/*
 * init arguments
 */
static int init_args(int *tx_channel, int *rx_channel,
                     size_t *tx_size, size_t *rx_size)
{
    // Set the default data size and number of transfers
    *tx_channel = -1;
    *rx_channel = -1;
    *tx_size = BUF_SIZE;
    *rx_size = BUF_SIZE;

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
    mb = test_mb_p;
    modu = test_modu_p;
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

    // printf("mm2s cal, DATA_MAX : %d, modu_cal : %d, mb : %d, cal_d : %d\n",
    //        DATA_MAX, modu_cal, mb, cal_d);

    int ldpc_num;

    ldpc_num = cal_u / cal_d;

    msf->ldpcNum = ldpc_num;
    msf->Mb = mb;
    msf->modulation = modu;

    return 0;
}

static int mm2s_all_test(axidma_dev_t dev, int tx_channel, void *tx_buf,
                         int tx_size)
{
    int rc = 0;

    char *p_tx_buf = tx_buf;

    // Initialize the buffer region we're going to transmit
    // data: all 0
    clean_tx_data(tx_buf, BUF_SIZE);

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
    msf.ldpcNum = 89;
    msf.Mb = 7;
    msf.modulation = QPSK;
    // printf("mm2s ldpc num:%d\n", msf.ldpcNum);
    // printf("mm2s mb:%d\n", msf.Mb);
    // printf("mm2s modu:%d", msf.modulation);
    char pack[HEAD_SIZE] = {0};
    char pack_r[HEAD_SIZE] = {0};
    constrM2S(&msf, (char *)(&pack));

    // printf("\nmm2s first: 0x");
    // for (int i = 0; i < HEAD_SIZE; ++i)
    // {
    //     printf("%02x", pack[i]);
    // }
    // printf("\n");

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
    // printf("datalen_inbit:%d\n", datalen_inbit);

    // 62656
    int datalen_inbyte = (datalen_inbit % 8 == 0) ? (datalen_inbit / 8) : (datalen_inbit / 8 + 1);
    // printf("datalen_inbyte:%d\n", datalen_inbyte);

    // printf("size of long:%ld\n", sizeof(long));
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

    // usleep(USLEEP * 1);
    // printf("usleep:%d\n", USLEEP);

    axidma_oneway_transfer(dev, tx_channel, tx_buf, datalen_inbyte + HEAD_SIZE, false);

    return rc;
}

void getInfo(void *rx_buf, int *lcnum)
{
    printf("S2MM INFO: \n");
    if (rx_buf == NULL)
    {
        printf("NULL pointer\n");
    }
    else
    {
        printf("not null\n");
    }
    // get first word
    long *p_l = rx_buf;

    printf("s2mm After trans, data in first word : %016lx \n", *p_l);

    long data_s = *p_l;
    printf("data_S: 0x%lx\n", data_s);
    print_b(&data_s, sizeof(data_s));

    char charpack_s[8] = {0};
    long2char(data_s, charpack_s);

    int j = getHeadS2M(charpack_s);

    if (print_evm_p)
    {
        if (j == 1212)
        {
            // gpio rst
            pthread_mutex_lock(&gpio_mutex);
            setdir(EVM_REQ_FLAG, SYSFS_GPIO_DIR_OUT);
            setvalue(EVM_REQ_FLAG, SYSFS_GPIO_VAL_L);
            evm_flag = 0;

            setdir(RECEIVE_RESET, SYSFS_GPIO_DIR_OUT);
            setvalue(RECEIVE_RESET, SYSFS_GPIO_VAL_H);
            setvalue(RECEIVE_RESET, SYSFS_GPIO_VAL_L);
            pthread_mutex_unlock(&gpio_mutex);

            int it = 0;
            long *index_l = rx_buf;

            int totalct = EVM_COUNT;
            int ct = totalct + 5;
            // int ct = 10;

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
        }
    }

    if (j == 1234 && !evm_flag)
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

        if (print_data_p)
        {
            // printf data
            int it = 0;
            long *index_l = rx_buf;
            // index_l++;
            int totalct = sf.ldpcnum * LDPC_K / PACK_LEN + 1;
            int ct = totalct + 5;
            // int ct = 10;

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

            // it = totalct - 10;
            // index_l = rx_buf + it;
            // while (it < totalct + 1000)
            // {
            //     printf("s2mm now data %d: %016lx \n", it, *index_l);
            //     it++;
            //     index_l++;
            // }
            // printf("s2mm now data %d: %016lx \n", it, *index_l);
        }
    }
}

void *udp_recv(void *args)
{
    struct udpmm2s *arg_thread1;
    arg_thread1 = (struct udpmm2s *)args;

    printf("MM2S ongoing\n\n");

    int total = 0;
    while (1)
    {
        while (tx_wait_flag)
        {
        }

        tx_wait_flag = 1;

        total += 1;
        if (total == 2)
        {
            random_flag--;
        }
        if (total == 3)
        {
            random_flag--;
        }
        mm2s_all_test(arg_thread1->axidma_dev, arg_thread1->tx_channel,
                      arg_thread1->tx_buf, arg_thread1->tx_size);

        printf("mm2s send count: %d\n", total);
    }

    return 0;
}

static void print_usage(int help)
{
    printf("Get Parameter Error\n");
}

static int parse_args(int argc, char **argv)
{
    char option;
    int int_arg;

    // b: mb for test, m: modulation for test, e: evm print or not, d: data print or not
    // g,u: evm gpio control
    while ((option = getopt(argc, argv, "b:m:edgu")) != (char)-1)
    {
        switch (option)
        {
        case 'b':
            if (parse_int(option, optarg, &int_arg) < 0)
            {
                print_usage(false);
                return -EINVAL;
            }
            test_mb_p = int_arg;
            break;
        case 'm':
            if (parse_int(option, optarg, &int_arg) < 0)
            {
                print_usage(false);
                return -EINVAL;
            }
            test_modu_p = int_arg;
            break;
        case 'e':
            print_evm_p = 1;
            break;
        case 'd':
            print_data_p = 1;
            break;
        case 'g':
            gpio_ch = 1;
            break;
        case 'u':
            gpio_ch = 0;
            setdir(EVM_REQ_FLAG, SYSFS_GPIO_DIR_OUT);
            setvalue(EVM_REQ_FLAG, SYSFS_GPIO_VAL_L);
            evm_flag = 0;
            break;
        default:
            print_usage(false);
            return -EINVAL;
        }
    }
    return 0;
}

void *gpiocontrol()
{
    while (1)
    {
        usleep(USLEEP * 10);
        if (gpio_ch)
        {
            pthread_mutex_lock(&gpio_mutex);
            setdir(EVM_REQ_FLAG, SYSFS_GPIO_DIR_OUT);
            setvalue(EVM_REQ_FLAG, SYSFS_GPIO_VAL_H);
            evm_flag = 1;
            pthread_mutex_unlock(&gpio_mutex);
        }
    }
}

/*----------------------------------------------------------------------------
 * Main Function
 *----------------------------------------------------------------------------*/
int main(int argc, char **argv)
{
    int rc;
    int tx_channel, rx_channel;
    size_t tx_size, rx_size;
    char *tx_buf, *rx_buf;
    axidma_dev_t axidma_dev;
    const array_t *tx_chans, *rx_chans;

    printf("Enter main v5.0 two devices connect\n");

    // gpio433 export
    export_gpio(EVM_REQ_FLAG);
    export_gpio(RECEIVE_RESET);

    if (parse_args(argc, argv) < 0)
    {
        printf("Wrong param\n");
        rc = 1;
        goto ret;
    }

    // Check if the user overrided the default transfer size and number
    // just pay attention to size
    if (init_args(&tx_channel, &rx_channel, &tx_size, &rx_size) < 0)
    {
        rc = 1;
        goto ret;
    }

    printf("AXI DMA Trans Parameters:\n");
    printf("\tTx size:%ld \n", tx_size);

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

    // test call back
    printf("tx channel:%d>>>>>>>>>>>>>>>>>>>>\n", tx_channel);
    printf("rx channel:%d>>>>>>>>>>>>>>>>>>>>\n", rx_channel);
    axidma_set_callback(axidma_dev, tx_channel, (void *)txcall, (void *)tx_buf);
    axidma_set_callback(axidma_dev, rx_channel, (void *)rxcall, (void *)rx_buf);

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
    ret = pthread_detach(tids);
    if (ret != 0)
    {
        fprintf(stderr, "pthread_detach error:%s\n", strerror(ret));
    }

    pthread_t gpio_cid;
    ret = pthread_create(&gpio_cid, NULL, gpiocontrol, NULL);
    if (ret != 0)
    {
        printf("gpio pthread_create error: error_code=%d", ret);
        goto free_rx_buf;
    }
    ret = pthread_detach(gpio_cid);
    if (ret != 0)
    {
        fprintf(stderr, "pthread_detach error:%s\n", strerror(ret));
    }

    axidma_oneway_transfer(axidma_dev, rx_channel, rx_buf, BUF_SIZE, false);

    printf("s2mm start\n");
    while (1)
    {
        while (rx_wait_flag)
        {
        }
        // int ldpcnum = 0;
        // getInfo(rx_buf, &ldpcnum);
        rx_wait_flag = 1;
        axidma_oneway_transfer(axidma_dev, rx_channel, rx_buf, BUF_SIZE, false);
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