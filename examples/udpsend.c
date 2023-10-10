#include "udpsend.h"

int udp_send_init(int localport, int destport,const char* destip)
{
    /* 建立udp socket */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        perror("socket");
        return -1;
    }

    memset(&addr_dest, 0, sizeof(addr_dest));
    addr_dest.sin_family = AF_INET;
    addr_dest.sin_addr.s_addr = inet_addr((const char*)destip);
    addr_dest.sin_port = htons(destport);

    addr_local.sin_family = AF_INET;
    addr_local.sin_port = htons(localport);
    addr_local.sin_addr.s_addr = 0;

    int ret = 0;
    if ((ret = bind(sock_fd, (struct sockaddr *)&addr_local, sizeof(struct sockaddr))) < 0)
    {
        perror("socket");
        return -1;
    }
    return 0;
}

int udp_send(void *buf, int size)
{
    int send_num = sendto(sock_fd, buf, size, 0,
                          (struct sockaddr *)&addr_dest, sizeof(addr_dest));
    if (send_num < 0)
    {
        perror("sendto error:");
        return -1;
    }
    return 0;
}

int udp_release()
{
    close(sock_fd);
    return 0;
}