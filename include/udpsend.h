#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEST_PORT 5001
#define DSET_IP_ADDRESS  "192.168.0.27"
#define LOCAL_PORT 1234

/* socket文件描述符 */
int sock_fd;
struct sockaddr_in addr_dest, addr_local;

int udp_send(void *buf, int size);
int udp_init();