#include "tcpclient.h"

int main()
{
    // 客户端只需要一个套接字文件描述符，用于和服务器通信
    int clientSocket;

    // 描述服务器的socket
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr)); // 每个字节都用0填充
    char sendbuf[200];
    long recvbuf[4096];
    int iDataNum;
    if ((clientSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        return 1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);

    // 指定服务器端的ip，本地测试：127.0.0.1
    // inet_addr()函数，将点分十进制IP转换成网络字节序IP
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("connect");
        return 1;
    }

    printf("连接到主机...\n");

    printf("读取消息:");

    while (1)
    {
        iDataNum = recv(clientSocket, recvbuf, sizeof(long) * 4096, 0);
        printf("rev num:%d\n", iDataNum);
        if (iDataNum > 0)
        {
            for (long i = 0; i < 4096; ++i)
            {
                printf("data in %ld: %016ld\n", i, recvbuf[i]);
            }
        }
    }

    close(clientSocket);
    return 0;
}
