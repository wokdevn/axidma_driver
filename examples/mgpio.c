//
// Created by shun on 2023/6/15.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define SYSFS_GPIO_EXPORT                   "/sys/class/gpio/export"
#define SECOND_PULSE_ENABLE      "439"
#define TIME_PULSE_ENALBE        "438"
#define TRANSCEIVE_RESET         "437"
#define CONTINUOUSE_TRANSCEIVE_SWITCH      "436"
#define RECEIVE_RESET            "435"
#define COASTAS_LOOP             "434"
#define IMPULSE_REQUEST          "432"
#define SYSFS_GPIO_DIR(x)         "/sys/class/gpio/gpio" x "/direction"
#define SYSFS_GPIO_DIR_OUT      "OUT"
#define SYSFS_GPIO_DIR_IN      "IN"
#define SYSFS_GPIO_VAL(x)          "/sys/class/gpio/gpio" x "/value"
#define SYSFS_GPIO_VAL_H        "1"
#define SYSFS_GPIO_VAL_L        "0"

int main() {
    int fd;

    for(int i = 0; i<10;++i){
        printf("%d\n",i);
        usleep(1000000);
    }

    //打开端口/sys/class/gpio# echo 48 > export
    fd = open(SYSFS_GPIO_EXPORT, O_WRONLY);
    if (fd == -1) {
        printf("ERR: gpio pin open error.\n");
        return EXIT_FAILURE;
    }
    write(fd, SECOND_PULSE_ENABLE, sizeof(SECOND_PULSE_ENABLE));
    close(fd);

    //设置端口方向/sys/class/gpio/gpio48# echo out > direction
    fd = open(SYSFS_GPIO_DIR(SECOND_PULSE_ENABLE), O_WRONLY);
    if (fd == -1) {
        printf("ERR: gpio pin direction open error.\n");
        return EXIT_FAILURE;
    }
    write(fd, SYSFS_GPIO_DIR_OUT, sizeof(SYSFS_GPIO_DIR_OUT));
    close(fd);

    fd = open(SYSFS_GPIO_VAL(SECOND_PULSE_ENABLE), O_RDWR);
    if (fd == -1) {
        printf("ERR: pin value open error.\n");
        return EXIT_FAILURE;
    }
    while (1) {
        printf("h\n");
        write(fd, SYSFS_GPIO_VAL_H, sizeof(SYSFS_GPIO_VAL_H));
        usleep(1000000);
        printf("l\n");
        write(fd, SYSFS_GPIO_VAL_L, sizeof(SYSFS_GPIO_VAL_L));
        usleep(1000000);
    }

    close(fd);

    printf("INFO: reset pin value open error.\n");
    return 0;

}