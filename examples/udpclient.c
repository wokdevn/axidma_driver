#include <udpclient.h>
// #include <stdio.h>
// #include <string.h>
// #include <stdlib.h>
// #include <unistd.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>


// #define DEST_PORT 2800
// // #define DSET_IP_ADDRESS  "192.168.0.30"
// #define DSET_IP_ADDRESS  "192.168.10.72"
// #define BUFFER_SIZE 1320

// #define CLIENT_PORT 8001

// int udp_send(int index);


int  udp_send(void *buf, int size){

    // int index = 1;
    /* socket文件描述符 */
    int sock_fd;
    int maxfd, retval;
    // fd_set myrdSet;
    // struct timeval tv;

    /* 建立udp socket */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        exit(1);
    }

    /* 设置address */
    struct sockaddr_in addr_serv, addr_cli;
    int len;
    memset(&addr_serv, 0, sizeof(addr_serv));
    addr_serv.sin_family = AF_INET;
    addr_serv.sin_addr.s_addr = inet_addr(DSET_IP_ADDRESS);
    addr_serv.sin_port = htons(DEST_PORT);
    len = sizeof(addr_serv);

    addr_cli.sin_family = AF_INET;
    addr_cli.sin_port = htons(CLIENT_PORT);
    addr_cli.sin_addr.s_addr = 0;

    int ret = 0;

    if ((ret = bind(sock_fd, (struct sockaddr *) &addr_cli, sizeof(struct sockaddr))) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }


    // while (1) {
    //     index++;
    //     FD_ZERO(&myrdSet);
    //     FD_SET(0, &myrdSet);
    //     maxfd = 0;

    //     FD_SET(sock_fd, &myrdSet);
    //     if (maxfd < sock_fd) {
    //         maxfd = sock_fd;
    //     }

    //     tv.tv_sec = 10;
    //     tv.tv_usec = 0;

//        retval = select(maxfd + 1, &myrdSet, nullptr, nullptr, &tv);
//        if (retval == -1) {
//            printf("select出错，客户端程序退出\n");
//            break;
//        } else if (retval == 0) {
//            printf("客户端没有任何输入信息，并且服务器也没有信息到来，waiting...\n");
//            continue;
//        } else {
//            /*服务器发来了消息*/
//            if (FD_ISSET(sock_fd, &myrdSet)) {
//                char recvbuf[BUFFER_SIZE];
//                int lenrecv;
//                lenrecv = recvfrom(sock_fd, recvbuf, sizeof(recvbuf), 0, (struct sockaddr *) &addr_serv,
//                               (socklen_t *) &len);
//                if (lenrecv < 0) {
//                    perror("recvfrom error:");
//                    exit(1);
//                }
//                printf("%s", recvbuf);
//                memset(recvbuf, 0, sizeof(recvbuf));
//            }
//            /*用户输入信息了,开始处理信息并发送*/
//            if (FD_ISSET(0, &myrdSet)) {
        char sendbuf[BUFFER_SIZE];
        char * buf_ptr = buf;
        // char indata[2];

//                fgets(indata, sizeof(indata), stdin);

        // sendbuf[0] = 1;
        // sendbuf[1] = 0;
        // sendbuf[2] = 2;
        // sendbuf[3] = 0;
        // sendbuf[4] = 1;
        // sendbuf[5] = 0;
        // sendbuf[6] = 2;
        // sendbuf[7] = 0;

        for (int i = 0; i < BUFFER_SIZE; ++i) {
            sendbuf[i] = *buf_ptr;
            buf_ptr++;
        }


        int send_num = sendto(sock_fd, sendbuf, BUFFER_SIZE , 0, (struct sockaddr *) &addr_serv, len);
        if (send_num < 0) {
            perror("sendto error:");
            exit(1);
        }
        memset(sendbuf, 0, sizeof(sendbuf));
    // }
    //    }


    close(sock_fd);

    return 0;
}
