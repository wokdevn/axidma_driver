#include "tcpclient.h"

long d1;

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
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("connect");
        return 1;
    }

    printf("connect to server...\n");

    printf("read message:");

    while (1)
    {
        iDataNum = recv(clientSocket, recvbuf, sizeof(long) * 4096, 0);
        printf("rev num:%d\n", iDataNum);
        if (iDataNum > 0)
        {
            // for (long i = 0; i < 4096; ++i)
            // {
            //     printf("%016lx\n",recvbuf[i]);
            // }
            // printf("%016lx\n",(long)0);

            if (recvbuf[0] != d1 + 0x0100)
            {
                printf("d1: %016lx, now: %016lx\n\n", d1, recvbuf[0]);
                d1 = recvbuf[0];

                for (int i = 0; i < 10; ++i)
                {
                    printf("i:%d, data:%016lx\n", i, *((long *)(recvbuf + i)));
                    *((long *)(recvbuf + i)) = 0;
                }
                printf("\n\n\n");
            }
            else
            {
                d1 = recvbuf[0];
                printf("ok\n");
            }

            if (d1 == 0x02000)
            {
                d1 = 0x0;
            }
        }
    }

    close(clientSocket);
    return 0;
}
