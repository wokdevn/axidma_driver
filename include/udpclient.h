#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define DEST_PORT 2800
#define DSET_IP_ADDRESS  "192.168.0.30"
#define BUFFER_SIZE 8000

#define CLIENT_PORT 8001

int udp_send(void *buf, int size);