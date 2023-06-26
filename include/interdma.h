//
// Created by shun on 2023/6/13.
//

#ifndef TWODMA_INTERDMA_H
#define TWODMA_INTERDMA_H


#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

#define PACK_LEN 64

typedef enum MOD
{
    BPSK = 0,QPSK,QAM16,QAM64
}m_Mod;

typedef enum
{
    m_true=1, m_false=0
}m_bool;

typedef struct MM2S_First{
    m_Mod modulation;
    int Mb;
    int ldpcNum;
} mm2s_f;

typedef struct S2MM_First{
    m_bool crc_r;
    int ldpc_tnum;
    int amp_dresult;
    m_Mod modu;
    int Mb;
    int ldpcnum;
} s2mm_f;


#endif //TWODMA_INTERDMA_H
