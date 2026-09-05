#include "ipc_udp.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>

#define IPC_TAG "ipc_udp"

typedef struct {
    int sockfd;
    int port_remote;
    bool stop;
    pthread_t tid;
    bool has_thread;
} ipc_udp_priv_t;

static int ipc_udp_send(ipc_endpoint_t *self, const char *data, int len)
{
    ipc_udp_priv_t *p = (ipc_udp_priv_t *)self->priv;
    if (!p || p->sockfd < 0 || p->port_remote == 0) {
        return -1;
    }
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port = htons(p->port_remote);
    return sendto(p->sockfd, data, len, 0,
                  (struct sockaddr *)&dst, sizeof(dst));
}

static int ipc_udp_recv(ipc_endpoint_t *self, unsigned char *data,
                        int maxlen, int *retlen)
{
    ipc_udp_priv_t *p = (ipc_udp_priv_t *)self->priv;
    if (!p || p->sockfd < 0) {
        return -1;
    }
    struct sockaddr_in src;
    socklen_t slen = sizeof(src);
    int n = recvfrom(p->sockfd, data, maxlen, 0,
                     (struct sockaddr *)&src, &slen);
    if (retlen) {
        *retlen = (n > 0) ? n : 0;
    }
    return n;
}

/* 后台线程：循环 recv，收到数据调 cb */
static void *ipc_recv_thread(void *arg)
{
    ipc_endpoint_t *self = (ipc_endpoint_t *)arg;
    ipc_udp_priv_t *p = (ipc_udp_priv_t *)self->priv;
    unsigned char buf[IPC_MAX_PACKET];

    while (!p->stop) {
        int n = recvfrom(p->sockfd, buf, sizeof(buf), 0, NULL, NULL);
        if (n > 0 && self->cb) {
            self->cb((const char *)buf, n, self->user_data);
        }
        /* n<=0 时 continue，stop 后会因 shutdown 返回 */
    }
    return NULL;
}

p_ipc_endpoint_t ipc_endpoint_create_udp(int port_local, int port_remote,
                                          transfer_callback_t cb, void *user_data)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        printf(IPC_TAG ": socket failed\n");
        return NULL;
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port_local);
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        printf(IPC_TAG ": bind port %d failed\n", port_local);
        close(fd);
        return NULL;
    }

    ipc_udp_priv_t *p = calloc(1, sizeof(*p));
    if (!p) {
        close(fd);
        return NULL;
    }
    p->sockfd = fd;
    p->port_remote = port_remote;
    p->stop = false;
    p->has_thread = false;

    ipc_endpoint_t *ep = calloc(1, sizeof(*ep));
    if (!ep) {
        free(p);
        close(fd);
        return NULL;
    }
    ep->priv = p;
    ep->user_data = user_data;
    ep->cb = cb;
    ep->send = ipc_udp_send;
    ep->recv = ipc_udp_recv;

    if (cb) {
        if (pthread_create(&p->tid, NULL, ipc_recv_thread, ep) == 0) {
            p->has_thread = true;
        } else {
            printf(IPC_TAG ": recv thread create failed\n");
        }
    }
    return ep;
}

void ipc_endpoint_destroy_udp(p_ipc_endpoint_t ep)
{
    if (!ep) {
        return;
    }
    ipc_udp_priv_t *p = (ipc_udp_priv_t *)ep->priv;
    if (p) {
        p->stop = true;
        if (p->sockfd >= 0) {
            shutdown(p->sockfd, 0);
            close(p->sockfd);
        }
        if (p->has_thread) {
            pthread_join(p->tid, NULL);
        }
        free(p);
    }
    free(ep);
}
