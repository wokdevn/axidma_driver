//
// Created by shun on 2023/6/15.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "util.h"

#define SYSFS_GPIO_EXPORT "/sys/class/gpio/export"

#define SECOND_PULSE_ENABLE 439
#define TIME_PULSE_ENALBE 438
#define TRANSCEIVE_RESET 437
#define CONTINUOUSE_TRANSCEIVE_SWITCH 436
#define RECEIVE_RESET 435
#define COASTAS_LOOP 434
#define IMPULSE_REQUEST 432

// #define SYSFS_GPIO_DIR(x) "/sys/class/gpio/gpio" x "/direction"
#define SYSFS_GPIO_DIR_OUT 0
#define SYSFS_GPIO_DIR_IN 1

// #define SYSFS_GPIO_VAL(x) "/sys/class/gpio/gpio" x "/value"
#define SYSFS_GPIO_VAL_H 1
#define SYSFS_GPIO_VAL_L 0

#define true 1
#define false 0

int export_gpio(int port)
{
    int fd;
    fd = open(SYSFS_GPIO_EXPORT, O_WRONLY);

    if (fd == -1)
    {
        printf("ERR: export open error.\n");
        return EXIT_FAILURE; // 1
    }

    char port_s[16] = {0};
    Int2String(port, port_s);

    write(fd, port_s, strlen(port_s));
    close(fd);
    return EXIT_SUCCESS; // 0
}

int setdir(int port, int dir)
{
    int fd;
    // 设置端口方向/sys/class/gpio/gpio48# echo out > direction
    char dir_str[50] = {"/sys/class/gpio/gpio"};
    char port_str[15] = {0};
    Int2String(port, port_str);
    strcat(dir_str, port_str);
    strcat(dir_str, "/direction");

    printf("dir string: %s\n", dir_str);

    fd = open(dir_str, O_WRONLY);

    if (fd == -1)
    {
        printf("ERR: gpio pin direction open error.\n");
        return EXIT_FAILURE;
    }

    switch (dir)
    {
    case SYSFS_GPIO_DIR_OUT:
        write(fd, "out", sizeof("out"));
        printf("out\n");
        break;

    default:
        write(fd, "in", sizeof("in"));
        printf("default\n");
        break;
    }
    close(fd);
    return EXIT_SUCCESS;
}

int setvalue(int port, int value)
{
    int fd;

    char loca_str[50] = {"/sys/class/gpio/gpio"};
    char port_str[15] = {0};
    Int2String(port, port_str);
    strcat(loca_str, port_str);
    strcat(loca_str, "/value");

    fd = open(loca_str, O_RDWR);
    if (fd == -1)
    {
        printf("ERR: pin value open error.\n");
        return EXIT_FAILURE;
    }

    switch (value)
    {
    case SYSFS_GPIO_VAL_H:
        write(fd, "1", sizeof("1"));
        break;

    default:
        write(fd, "0", sizeof("0"));
        break;
    }

    close(fd);
    return EXIT_SUCCESS;
}

int getvalue(int port)
{
    int fd;

    char loca_str[50] = {"/sys/class/gpio/gpio"};
    char port_str[15] = {0};
    Int2String(port, port_str);
    strcat(loca_str, port_str);
    strcat(loca_str, "/value");

    fd = open(loca_str, O_RDONLY);
    if (fd == -1)
    {
        printf("ERR: pin value open error.\n");
        return EXIT_FAILURE;
    }

    char buf[3];
    int rc = read(fd, buf, 1);

    buf[1] = 0;
    printf("get value: %c\n", buf[0]);

    close(fd);
    return EXIT_SUCCESS;
}

int test_all()
{
    for (int i = 432; i <= 439; ++i)
    {
        if (i == 433)
        {
            continue;
        }

        if (export_gpio(i))
        {
            printf("export failure : port %d\n", i);
            return EXIT_FAILURE;
        }

        if (setdir(i, SYSFS_GPIO_DIR_OUT))
        {
            printf("set dir out fail : port %d\n", i);
            return EXIT_FAILURE;
        }

        if (setvalue(i, SYSFS_GPIO_VAL_L))
        {
            printf("set value low fail : port %d\n", i);
            return EXIT_FAILURE;
        }

        if (setvalue(i, SYSFS_GPIO_VAL_H))
        {
            printf("set value high fail : port %d\n", i);
            return EXIT_FAILURE;
        }

        if (setdir(i, SYSFS_GPIO_DIR_IN))
        {
            printf("set dir out fail : port %d\n", i);
            return EXIT_FAILURE;
        }

        if (getvalue(i))
        {
            printf("get value low fail : port %d\n", i);
            return EXIT_FAILURE;
        }
    }

    printf("passed\n");
    return EXIT_SUCCESS;
}

int test_all_export()
{
    for (int i = 432; i <= 439; ++i)
    {
        if (i == 433)
        {
            continue;
        }

        if (export_gpio(i))
        {
            printf("export failure : port %d\n", i);
            return EXIT_FAILURE;
        }
    }

    printf("passed\n");
    return EXIT_SUCCESS;
}

int test_all_out_dir()
{
    for (int i = 432; i <= 439; ++i)
    {
        if (i == 433)
        {
            continue;
        }

        if (setdir(i, SYSFS_GPIO_DIR_OUT))
        {
            printf("set out failure : port %d\n", i);
            return EXIT_FAILURE;
        }
    }

    printf("passed\n");
    return EXIT_SUCCESS;
}

int test_all_high()
{
    for (int i = 432; i <= 439; ++i)
    {
        if (i == 433)
        {
            continue;
        }

        if (setvalue(i, SYSFS_GPIO_VAL_H))
        {
            printf("set high failure : port %d\n", i);
            return EXIT_FAILURE;
        }
    }

    printf("passed\n");
    return EXIT_SUCCESS;
}

int test_all_low()
{
    for (int i = 432; i <= 439; ++i)
    {
        if (i == 433)
        {
            continue;
        }

        if (setvalue(i, SYSFS_GPIO_VAL_L))
        {
            printf("set low failure : port %d\n", i);
            return EXIT_FAILURE;
        }
    }

    printf("passed\n");
    return EXIT_SUCCESS;
}

int test_all_in_dir()
{
    for (int i = 432; i <= 439; ++i)
    {
        if (i == 433)
        {
            continue;
        }

        if (setdir(i, SYSFS_GPIO_DIR_IN))
        {
            printf("set in failure : port %d\n", i);
            return EXIT_FAILURE;
        }
    }

    printf("passed\n");
    return EXIT_SUCCESS;
}

int test_all_in_value()
{
    for (int i = 432; i <= 439; ++i)
    {
        if (i == 433)
        {
            continue;
        }

        if (getvalue(i))
        {
            printf("get value failure : port %d\n", i);
            return EXIT_FAILURE;
        }
    }

    printf("passed\n");
    return EXIT_SUCCESS;
}

int checkport(int port)
{
    switch (port)
    {
    case SECOND_PULSE_ENABLE:
        return EXIT_SUCCESS;
        break;

    case TIME_PULSE_ENALBE:
        return EXIT_SUCCESS;
        break;
    case TRANSCEIVE_RESET:
        return EXIT_SUCCESS;
        break;
    case CONTINUOUSE_TRANSCEIVE_SWITCH:
        return EXIT_SUCCESS;
        break;
    case RECEIVE_RESET:
        return EXIT_SUCCESS;
        break;
    case COASTAS_LOOP:
        return EXIT_SUCCESS;
        break;
    case IMPULSE_REQUEST:
        return EXIT_SUCCESS;
        break;

    default:
        printf("wrong port\n");
        return EXIT_FAILURE;
        break;
    }
}

int print_usage(int help){
    printf("nothing to recommend\n");
}

int parse_args(int argc, char **argv)
{
    char option;
    int int_arg;

    while ((option = getopt(argc, argv, "e:o:i:h:l:v:")) != (char)-1)
    {
        switch (option)
        {
        case 'e':
            if (parse_int(option, optarg, &int_arg) < 0)
            {
                print_usage(false);
                return -EINVAL;
            }
            if (checkport(int_arg))
            {
                print_usage(false);
                return -ENODEV;
            }
            if (export_gpio(int_arg))
            {
                printf("error when export\n");
            }
            break;
        case 'o':
            if (parse_int(option, optarg, &int_arg) < 0)
            {
                print_usage(false);
                return -EINVAL;
            }
            if (checkport(int_arg))
            {
                print_usage(false);
                return -ENODEV;
            }
            if (setdir(int_arg, SYSFS_GPIO_DIR_OUT))
            {
                printf("set dir out fail\n");
            }
            break;
        case 'i':
            if (parse_int(option, optarg, &int_arg) < 0)
            {
                print_usage(false);
                return -EINVAL;
            }
            if (checkport(int_arg))
            {
                print_usage(false);
                return -ENODEV;
            }
            if (setdir(int_arg, SYSFS_GPIO_DIR_IN))
            {
                printf("set dir out fail\n");
            }
            break;
        case 'h':
            if (parse_int(option, optarg, &int_arg) < 0)
            {
                print_usage(false);
                return -EINVAL;
            }
            if (checkport(int_arg))
            {
                print_usage(false);
                return -ENODEV;
            }
            if (setdir(int_arg, SYSFS_GPIO_DIR_OUT))
            {
                printf("set dir out fail\n");
            }
            if(setvalue(int_arg,SYSFS_GPIO_VAL_H))
            {
                printf("set dir out OK, set value h fail\n");
            }
            break;
        case 'l':
            if (parse_int(option, optarg, &int_arg) < 0)
            {
                print_usage(false);
                return -EINVAL;
            }
            if (checkport(int_arg))
            {
                print_usage(false);
                return -ENODEV;
            }
            if (setdir(int_arg, SYSFS_GPIO_DIR_OUT))
            {
                printf("set dir out fail\n");
            }
            if(setvalue(int_arg,SYSFS_GPIO_VAL_L))
            {
                printf("set dir out OK, set value l fail\n");
            }
            break;
        case 'v':
            if (parse_int(option, optarg, &int_arg) < 0)
            {
                print_usage(false);
                return -EINVAL;
            }
            if (checkport(int_arg))
            {
                print_usage(false);
                return -ENODEV;
            }
            if (setdir(int_arg, SYSFS_GPIO_DIR_IN))
            {
                printf("set dir in fail\n");
            }
            if(getvalue(int_arg))
            {
                printf("set dir in OK, get value fail\n");
            }
            break;

        default:
            print_usage(false);
            return -EINVAL;
        }
    }
}

int main(int argc, char **argv)
{
    // usleep: micro seconds
    // int i;
    // while (scanf("%d", &i))
    // {
    //     switch (i)
    //     {
    //     case 1:
    //         printf("export test\n");
    //         test_all_export();
    //         break;
    //     case 2:
    //         printf("outdir test\n");
    //         test_all_out_dir();
    //         break;
    //     case 3:
    //         printf("out high test\n");
    //         test_all_high();
    //         break;
    //     case 4:
    //         printf("out low test\n");
    //         test_all_low();
    //         break;
    //     case 5:
    //         printf("indir test\n");
    //         test_all_in_dir();
    //         break;
    //     case 6:
    //         printf("get value test\n");
    //         test_all_in_value();
    //         break;

    //     default:
    //         break;
    //     }
    // }

    return parse_args(argc,argv);
}