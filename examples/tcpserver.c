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

#define SERVER_PORT 8888
#define SERVER_IP "192.168.0.27"

// 调用socket函数返回的文件描述符
int serverSocket;
int clientSocket;

int addr_len;

// 声明两个套接字sockaddr_in结构体变量，分别表示客户端和服务器
struct sockaddr_in server_addr;
struct sockaddr_in clientAddr;

int linkEstablished = 0;

int serverInit();
int establishLink();
int sendTcp(void *data, int length);
int releaseTcp();

int serverInit()
{
    addr_len = sizeof(clientAddr);

    // socket函数，失败返回-1
    // int socket(int domain, int type, int protocol);
    // 第一个参数表示使用的地址类型，一般都是ipv4，AF_INET
    // 第二个参数表示套接字类型：tcp：面向连接的稳定数据传输SOCK_STREAM
    // 第三个参数设置为0
    if ((serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("socket");
        return -1;
    }

    bzero(&server_addr, sizeof(server_addr));

    // 初始化服务器端的套接字，并用htons和htonl将端口和地址转成网络字节序
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    // ip可是是本服务器的ip，也可以用宏INADDR_ANY代替，代表0.0.0.0，表明所有地址
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 对于bind，accept之类的函数，里面套接字参数都是需要强制转换成(struct sockaddr *)
    // bind三个参数：服务器端的套接字的文件描述符，
    if (bind(serverSocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        return -1;
    }

    // 设置服务器上的socket为监听状态
    if (listen(serverSocket, 1) < 0)
    {
        perror("listen");
        return -1;
    }

    printf("监听端口: %d\n", SERVER_PORT);
}

int establishLink()
{
    // 调用accept函数后，会进入阻塞状态
    // accept返回一个套接字的文件描述符，这样服务器端便有两个套接字的文件描述符，
    // serverSocket和client。
    // serverSocket仍然继续在监听状态，client则负责接收和发送数据
    // clientAddr是一个传出参数，accept返回时，传出客户端的地址和端口号
    // addr_len是一个传入-传出参数，传入的是调用者提供的缓冲区的clientAddr的长度，以避免缓冲区溢出。
    // 传出的是客户端地址结构体的实际长度。
    // 出错返回-1
    clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, (socklen_t *)&addr_len);
    if (clientSocket < 0)
    {
        perror("accept");
    }
    printf("Link Established...\n");
    linkEstablished = 1;

    // inet_ntoa ip地址转换函数，将网络字节序IP转换为点分十进制IP
    // 表达式：char *inet_ntoa (struct in_addr);
    printf("IP is %s\n", inet_ntoa(clientAddr.sin_addr));
    printf("Port is %d\n", htons(clientAddr.sin_port));
}

int sendTcp(void *data, int length)
{
    struct timeval tv_begin, tv_end, tresult;

    gettimeofday(&tv_begin, NULL);

    send(clientSocket, data, length, 0);

    gettimeofday(&tv_end, NULL);
    timersub(&tv_end, &tv_begin, &tresult);

    double timeuse_s = tresult.tv_sec + (1.0 * tresult.tv_usec) / 1000000;      //  精确到秒
    double timeuse_ms = tresult.tv_sec * 1000 + (1.0 * tresult.tv_usec) / 1000; //  精确到毫秒
    printf("timeuse in ms: %f \n", timeuse_ms);
}

int releaseTcp()
{
    close(clientSocket);
    close(serverSocket);
    return 0;
}