#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define DEST_PORT 5001
#define DSET_IP_ADDRESS  "192.168.0.126"
#define BUFFER_SIZE 1320

#define CLIENT_PORT 1234

int udp_send(void *buf, int size);