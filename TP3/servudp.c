/*****
* Serveur UDP simple en mode non connecté - version thread
*****/

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 9999
#define LBUF 512

static char *addrip(unsigned long host_ip, char *b, size_t b_sz)
{
    snprintf(b, b_sz, "%u.%u.%u.%u",
             (unsigned int)(host_ip >> 24 & 0xFF),
             (unsigned int)(host_ip >> 16 & 0xFF),
             (unsigned int)(host_ip >>  8 & 0xFF),
             (unsigned int)(host_ip       & 0xFF));
    return b;
}

/*
 * serveur_udp - thread serveur UDP simple (écho)
 * p : non utilisé (prévu pour passer un pseudo ou un port)
 */
void *serveur_udp(void *p)
{
    int sid, n;
    struct sockaddr_in sock_conf, from_addr;
    socklen_t from_len;
    char buf[LBUF + 1];
    char ip_buf[16];

    (void)p;

    if ((sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket");
        return NULL;
    }

    memset(&sock_conf, 0, sizeof(sock_conf));
    sock_conf.sin_family = AF_INET;
    sock_conf.sin_port   = htons(PORT);

    if (bind(sid, (struct sockaddr *)&sock_conf, sizeof(sock_conf)) == -1) {
        perror("bind");
        close(sid);
        return NULL;
    }

    printf("Serveur UDP attaché au port %d\n", PORT);

    for (;;) {
        from_len = sizeof(from_addr);
        n = recvfrom(sid, (void *)buf, LBUF, 0,
                     (struct sockaddr *)&from_addr, &from_len);
        if (n == -1) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            perror("recvfrom");
            continue;
        }

        buf[n] = '\0';
        printf("Reçu de %s : <%s>\n",
               addrip(ntohl(from_addr.sin_addr.s_addr), ip_buf, sizeof(ip_buf)),
               buf);

        if (sendto(sid, buf, (size_t)n, 0,
                   (struct sockaddr *)&from_addr, from_len) == -1)
            perror("sendto");
    }

    close(sid);
    return NULL;
}

int main(void)
{
    serveur_udp(NULL);
    return 0;
}
