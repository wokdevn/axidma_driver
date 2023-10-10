#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* socket文件描述符 */
int sock_fd;
struct sockaddr_in addr_dest, addr_local;

int udp_send(void *buf, int size);
int udp_send_init(int localport, int destport,const char* destip);
int udp_send_release();