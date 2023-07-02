//
// Created by shun on 2023/6/13.
//

#include "util_interdma.h"

void print_b(void *pointer, size_t size)
{
    unsigned long data = *((unsigned long *)pointer);
    int length = size * 8;
    int counter = 0;
    printf("bin: ");
    while (length-- > 0)
    {
        printf("%lu", (data >> length) & 0x1);
        counter++;
        if (counter % 8 == 0)
        {
            printf(" ");
        }
    }
    printf("\n");
}

void revert_char(char *data, int len, char *r_data)
{
    char *cp_data = r_data;
    memcpy(cp_data, data, len + 1);

    char *p1 = &cp_data[len - 1];
    char *p = cp_data;
    while (p < p1)
    {
        char temp = *p;
        *p = *p1;
        *p1 = temp;
        p++;
        p1--;
    }
}

char *itobs(long n, char *ps)
{
    /**
     * 数值转二进制
     */
    for (int j = LONG_BIT_COUNT - 1; j >= 0; --j, n >>= 1)
    {
        ps[j] = (1 & n) + '0';
    }
    ps[LONG_BIT_COUNT] = '\0';
    return ps;
}

char *long2char(long num, char *str) // 10进制
{
    int count = sizeof(long) / sizeof(char);
    for (int i = 0; i < count; ++i)
    {
        *(str + i) = (num >> (count - 1 - i) * 8) & 0xff;
    }

    return str; // 返回转换后的值
}

char *char2bit(char *pack, int len, char *bitpack)
{
    int index = 0;
    for (int i = 0; i < len * 8; ++i)
    {
        index = i / 8;
        *(bitpack + i) = (*(pack + index) >> (7 - i % 8)) & 0x01;
    }
    return bitpack;
}

char *bit2char(char *bitpack, int len_inchar, char *pack)
{
    int index = 0;
    for (int i = 0; i < len_inchar * 8; ++i)
    {
        index = i / 8;
        *(pack + index) = (*(pack + index) << 1) | (0x01 & *(bitpack + i));
    }
    return pack;
}

int getHeadM2S(char *data)
{
    char *head = data;

    int first = ((*data) >> 4) & 0xff;
    int second = (*data) & 0x0f;
    int three = ((*(data + 1)) >> 4) & 0xff;
    int four = (*(data + 1)) & 0x0f;

    return first * 1000 + second * 100 + three * 10 + four;
}

void getParamM2S(struct MM2S_First *mm2s_first, char *data)
{
    unsigned char *r_data = (char *)malloc(sizeof(char) * PACK_LEN + 1);
    revert_char(data, PACK_LEN, r_data);

    int temp = 0;
    for (int i = 27; i >= 26; --i)
    {
        temp = temp << 1;
        temp = temp + (r_data[i] & 0x01);
    }
    mm2s_first->modulation = temp;

    temp = 0;
    for (int i = 25; i >= 20; --i)
    {
        temp = temp << 1;
        temp = temp + (r_data[i] & 0x01);
    }
    mm2s_first->Mb = temp;

    temp = 0;
    for (int i = 19; i >= 11; --i)
    {
        temp = temp << 1;
        temp = temp + (r_data[i] & 0x01);
    }

    mm2s_first->ldpcNum = temp;

    free(r_data);
}

int getHeadS2M(char *data)
{
    char *head = data;

    int first = ((*data) >> 4) & 0xff;
    int second = (*data) & 0x0f;
    int three = ((*(data + 1)) >> 4) & 0xff;
    int four = (*(data + 1)) & 0x0f;

    return first * 1000 + second * 100 + three * 10 + four;
}

void getParamS2M(s2mm_f *s2mm_first, char *data)
{
    unsigned char *r_data = (char *)malloc(sizeof(char) * PACK_LEN + 1);
    revert_char(data, PACK_LEN, r_data);

    int temp = 0;
    for (int i = 39; i >= 39; --i)
    {
        temp = temp << 1;
        temp = temp + (r_data[i] & 0x01);
    }
    s2mm_first->crc_r = temp;

    temp = 0;
    for (int i = 38; i >= 30; --i)
    {
        temp = temp << 1;
        temp = temp + (r_data[i] & 0x01);
    }
    s2mm_first->ldpc_tnum = temp;

    temp = 0;
    for (int i = 29; i >= 28; --i)
    {
        temp = temp << 1;
        temp = temp + (r_data[i] & 0x01);
    }
    s2mm_first->amp_dresult = temp;

    temp = 0;
    for (int i = 27; i >= 26; --i)
    {
        temp = temp << 1;
        temp = temp + (r_data[i] & 0x01);
    }
    s2mm_first->modu = temp;

    temp = 0;
    for (int i = 25; i >= 20; --i)
    {
        temp = temp << 1;
        temp = temp + (r_data[i] & 0x01);
    }
    s2mm_first->Mb = temp;

    temp = 0;
    for (int i = 19; i >= 11; --i)
    {
        temp = temp << 1;
        temp = temp + (r_data[i] & 0x01);
    }
    s2mm_first->ldpcnum = temp;

    free(r_data);
}

char *constrM2S(struct MM2S_First *mm2s_first, char *data)
{
    m_Mod modu = mm2s_first->modulation;
    int mb = mm2s_first->Mb;
    int ldpcnum = mm2s_first->ldpcNum;

    unsigned char pack[64] = {0};
    pack[60] = 1;
    pack[57] = 1;
    pack[53] = 1;
    pack[52] = 1;
    pack[50] = 1;

    switch (modu)
    {
    case BPSK:
        pack[27] = 0;
        pack[26] = 0;
        break;
    case QPSK:
        pack[27] = 0;
        pack[26] = 1;
        break;
    case QAM16:
        pack[27] = 1;
        pack[26] = 0;
        break;
    case QAM64:
        pack[27] = 1;
        pack[26] = 1;
        break;
    }

    for (int i = 25; i >= 20; --i)
    {
        pack[i] = (mb >> (i - 20)) & 0x01;
    }

    for (int i = 19; i >= 11; --i)
    {
        pack[i] = (ldpcnum >> (i - 11)) & 0x01;
    }

    unsigned char *r_data = (char *)malloc(sizeof(char) * PACK_LEN + 1);
    revert_char(&pack, sizeof(char) * PACK_LEN, r_data);

    bit2char(r_data, 8, data);

    free(r_data);

    return data;
}

int checkLSB()
{
    int i = 1;
    int flag = (*(char *)&i == 1) ? 1 : 0;
    if(flag){
        printf("LSB\n");
    }else{
        printf("MSB\n");
    }
    return flag;
}