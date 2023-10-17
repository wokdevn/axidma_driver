#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>

// 调用socket函数返回的文件描述符
int serverSocket;
int clientSocket;

int addr_len;

// 声明两个套接字sockaddr_in结构体变量，分别表示客户端和服务器
struct sockaddr_in server_addr;
struct sockaddr_in clientAddr;

int linkFlag;

int tcpInit(unsigned int server_port, const char* server_ip);
int tcpLink();
int sendTcp(void *data, int length);
int releaseTcp();
void sig_handler(int signo);
void waitlink();