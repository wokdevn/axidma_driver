int export_gpio(int port);
int setdir(int port, int dir);
int setvalue(int port, int value);
int getvalue(int port);
int test_all();
int test_all_export();
int test_all_out_dir();
int test_all_high();
int test_all_low();
int test_all_in_dir();
int test_all_in_value();
int checkport(int port);
int print_usage_g(int help);
int parse_args_g(int argc, char **argv);

#define SYSFS_GPIO_EXPORT "/sys/class/gpio/export"

#define SECOND_PULSE_ENABLE 439
#define TIME_PULSE_ENALBE 438
#define TRANSCEIVE_RESET 437
#define CONTINUOUSE_TRANSCEIVE_SWITCH 436
#define RECEIVE_RESET 435
#define COASTAS_LOOP 434
#define EVM_REQ_FLAG 433
#define IMPULSE_REQUEST 432

// #define SYSFS_GPIO_DIR(x) "/sys/class/gpio/gpio" x "/direction"
#define SYSFS_GPIO_DIR_OUT 0
#define SYSFS_GPIO_DIR_IN 1

// #define SYSFS_GPIO_VAL(x) "/sys/class/gpio/gpio" x "/value"
#define SYSFS_GPIO_VAL_H 1
#define SYSFS_GPIO_VAL_L 0

#define true 1
#define false 0