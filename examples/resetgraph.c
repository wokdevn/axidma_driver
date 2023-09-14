#include <errno.h> // Error codes
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>

int init_reg_dev();
int regdev_read(int reg, int *val);
int regdev_write(int reg, int val);
void gw_WriteReg(unsigned int addr, int dat);
int gw_ReadReg(unsigned int addr);
int printDMAReg();

void *map_base_dma;
void *map_base_apb;
int g_memDev;

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

int main()
{
    init_reg_dev();

    char cmd;
    unsigned int status;

    printf("r:reset,  u:unreset,  d:ready,  b:not ready\n");
    printf("init: ready,not reset\n");

    gw_WriteReg(0xA0010004, 0x1); // ready, not reset
    status = 0x1;

    while (scanf("%c", &cmd))
    {
        switch (cmd)
        {
        case 'r':
            status = status | 0x01;
            gw_WriteReg(0xA0010004, status);
            printf("reset,status:%08x\n", status);
            break;
        case 'd':
            status = status | 0x04;
            gw_WriteReg(0xA0010004, status);
            printf("ready,status:%08x\n", status);
            break;
        case 'u':
            status = status & 0xfffffffe;
            gw_WriteReg(0xA0010004, status);
            printf("un reset,status:%08x\n", status);
            break;

        case 'b':
            status = status & 0xfffffffb;
            gw_WriteReg(0xA0010004, status);
            printf("unready,status:%08x\n", status);
            break;

        default:
            break;
        }
    }
}