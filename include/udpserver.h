#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define REMOTE_PORT 5001
#define LOCAL_PORT 1234
void* udp_recv(void * args);