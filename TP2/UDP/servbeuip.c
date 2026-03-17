/*****
* BEUIP UDP server for identification tests
*****/

#include "creme.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static creme_peer_t peers[CREME_MAX_PEERS];
static int peer_count = 0;
static volatile sig_atomic_t stop_requested = 0;

static void on_stop_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
}

static int has_peer(uint32_t ip, const char *pseudo)
{
    int i;

    for (i = 0; i < peer_count; ++i) {
        if (peers[i].ip == ip && strcmp(peers[i].pseudo, pseudo) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_peer(uint32_t ip, const char *pseudo)
{
    char ip_text[16];

    if (has_peer(ip, pseudo) || peer_count >= CREME_MAX_PEERS) {
        return;
    }

    peers[peer_count].ip = ip;
    strncpy(peers[peer_count].pseudo, pseudo, sizeof(peers[peer_count].pseudo) - 1);
    peers[peer_count].pseudo[sizeof(peers[peer_count].pseudo) - 1] = '\0';
    ++peer_count;

    creme_addrip(ip, ip_text, sizeof(ip_text));
    printf("[+] present: %s (%s)\n", pseudo, ip_text);
}

static void print_peer_list(void)
{
    int i;
    char ip_text[16];

    printf("---- known peers (%d) ----\n", peer_count);
    for (i = 0; i < peer_count; ++i) {
        creme_addrip(peers[i].ip, ip_text, sizeof(ip_text));
        printf("%3d | %-24s | %s\n", i + 1, peers[i].pseudo, ip_text);
    }
    printf("--------------------------\n");
}

static void remove_peer(uint32_t ip, const char *pseudo)
{
    int i;
    char ip_text[16];

    for (i = 0; i < peer_count; ++i) {
        if (peers[i].ip == ip && strcmp(peers[i].pseudo, pseudo) == 0) {
            int j;

            for (j = i; j < peer_count - 1; ++j) {
                peers[j] = peers[j + 1];
            }
            --peer_count;
            creme_addrip(ip, ip_text, sizeof(ip_text));
            printf("[-] left: %s (%s)\n", pseudo, ip_text);
            return;
        }
    }
}

int main(int argc, char *argv[])
{
    int sid;
    int n;
    int yes = 1;
    struct sockaddr_in sock_conf;
    struct sockaddr_in from;
    struct sockaddr_in bcast;
    struct timeval tv;
    struct sigaction sa;
    socklen_t ls;
    char buf[CREME_LBUF + 1];
    char peer_pseudo[CREME_LBUF + 1];
    char target_pseudo[CREME_LBUF + 1];
    char text[CREME_LBUF + 1];
    const char *my_pseudo;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s pseudo\n", argv[0]);
        return 1;
    }
    my_pseudo = argv[1];

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_stop_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sid < 0) {
        perror("socket");
        return 2;
    }

    if (setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) == -1) {
        perror("setsockopt(SO_BROADCAST)");
        close(sid);
        return 3;
    }

    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sid, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&sock_conf, 0, sizeof(sock_conf));
    sock_conf.sin_family = AF_INET;
    sock_conf.sin_addr.s_addr = htonl(INADDR_ANY);
    sock_conf.sin_port = htons(CREME_PORT);

    if (bind(sid, (struct sockaddr *)&sock_conf, sizeof(sock_conf)) == -1) {
        perror("bind");
        close(sid);
        return 4;
    }

    memset(&bcast, 0, sizeof(bcast));
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(CREME_PORT);
    if (inet_aton(CREME_BCAST_ADDR, &bcast.sin_addr) == 0) {
        fprintf(stderr, "Invalid broadcast address: %s\n", CREME_BCAST_ADDR);
        close(sid);
        return 5;
    }

    n = creme_build_message(buf, sizeof(buf), CREME_CODE_DISCOVERY, my_pseudo);
    if (n < 0) {
        fprintf(stderr, "Pseudo too long\n");
        close(sid);
        return 6;
    }

    if (sendto(sid, buf, (size_t)n, 0, (struct sockaddr *)&bcast, sizeof(bcast)) == -1) {
        perror("sendto(broadcast)");
        close(sid);
        return 7;
    }

    printf("BEUIP server on port %d, pseudo=%s\n", CREME_PORT, my_pseudo);
    printf("creme version: %s\n", creme_version());
    printf("Broadcast discovery sent to %s\n", CREME_BCAST_ADDR);

    for (;;) {
        if (stop_requested) {
            struct sockaddr_in quit_bcast;

            memset(&quit_bcast, 0, sizeof(quit_bcast));
            quit_bcast.sin_family = AF_INET;
            quit_bcast.sin_port = htons(CREME_PORT);
            inet_aton(CREME_BCAST_ADDR, &quit_bcast.sin_addr);

            n = creme_build_message(buf, sizeof(buf), CREME_CODE_QUIT, my_pseudo);
            if (n > 0) {
                sendto(sid, buf, (size_t)n, 0, (struct sockaddr *)&quit_bcast, sizeof(quit_bcast));
            }
            printf("Stopping server after quit broadcast\n");
            break;
        }

        ls = sizeof(from);
        n = recvfrom(sid, buf, CREME_LBUF, 0, (struct sockaddr *)&from, &ls);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            perror("recvfrom");
            continue;
        }

        if (n < (1 + CREME_MAGIC_LEN)) {
            continue;
        }

        if (buf[0] != CREME_CODE_QUIT && buf[0] != CREME_CODE_DISCOVERY && buf[0] != CREME_CODE_ACK &&
            buf[0] != CREME_CODE_LIST && buf[0] != CREME_CODE_SEND_TO_PSEUDO &&
            buf[0] != CREME_CODE_SEND_TO_ALL && buf[0] != CREME_CODE_DELIVER) {
            continue;
        }

        if (memcmp(buf + 1, CREME_MAGIC, CREME_MAGIC_LEN) != 0) {
            continue;
        }

        if ((buf[0] == CREME_CODE_LIST || buf[0] == CREME_CODE_SEND_TO_PSEUDO ||
             buf[0] == CREME_CODE_SEND_TO_ALL) && !creme_is_local_command_source(&from)) {
            continue;
        }

        if (buf[0] == CREME_CODE_QUIT) {
            int payload_len = n - (1 + CREME_MAGIC_LEN);
            if (payload_len <= 0) {
                continue;
            }
            if (payload_len > CREME_LBUF) {
                payload_len = CREME_LBUF;
            }

            memcpy(peer_pseudo, buf + 1 + CREME_MAGIC_LEN, (size_t)payload_len);
            peer_pseudo[payload_len] = '\0';
            remove_peer(ntohl(from.sin_addr.s_addr), peer_pseudo);
            continue;
        }

        if (buf[0] == CREME_CODE_LIST) {
            print_peer_list();
            continue;
        }

        if (buf[0] == CREME_CODE_SEND_TO_PSEUDO) {
            int payload_len = n - (1 + CREME_MAGIC_LEN);
            int used = 0;
            uint32_t target_ip;
            struct sockaddr_in dst;

            if (!creme_extract_cstr(buf + 1 + CREME_MAGIC_LEN, payload_len, target_pseudo,
                                    sizeof(target_pseudo), &used)) {
                continue;
            }

            if (!creme_copy_payload_text(buf + 1 + CREME_MAGIC_LEN + used, payload_len - used,
                                         text, sizeof(text))) {
                continue;
            }

            if (!creme_find_ip_by_pseudo(peers, peer_count, target_pseudo, &target_ip)) {
                printf("[!] unknown pseudo: %s\n", target_pseudo);
                continue;
            }

            n = creme_build_message(buf, sizeof(buf), CREME_CODE_DELIVER, text);
            if (n < 0) {
                continue;
            }

            memset(&dst, 0, sizeof(dst));
            dst.sin_family = AF_INET;
            dst.sin_port = htons(CREME_PORT);
            dst.sin_addr.s_addr = htonl(target_ip);

            if (sendto(sid, buf, (size_t)n, 0, (struct sockaddr *)&dst, sizeof(dst)) == -1) {
                perror("sendto(deliver)");
            }
            continue;
        }

        if (buf[0] == CREME_CODE_SEND_TO_ALL) {
            int payload_len = n - (1 + CREME_MAGIC_LEN);
            int i;

            if (!creme_copy_payload_text(buf + 1 + CREME_MAGIC_LEN, payload_len, text,
                                         sizeof(text))) {
                continue;
            }

            n = creme_build_message(buf, sizeof(buf), CREME_CODE_DELIVER, text);
            if (n < 0) {
                continue;
            }

            for (i = 0; i < peer_count; ++i) {
                struct sockaddr_in dst;

                if (strcmp(peers[i].pseudo, my_pseudo) == 0) {
                    continue;
                }

                memset(&dst, 0, sizeof(dst));
                dst.sin_family = AF_INET;
                dst.sin_port = htons(CREME_PORT);
                dst.sin_addr.s_addr = htonl(peers[i].ip);

                if (sendto(sid, buf, (size_t)n, 0, (struct sockaddr *)&dst, sizeof(dst)) == -1) {
                    perror("sendto(all)");
                }
            }
            continue;
        }

        if (buf[0] == CREME_CODE_DELIVER) {
            int payload_len = n - (1 + CREME_MAGIC_LEN);
            const char *sender;
            char ip_text[16];

            if (!creme_copy_payload_text(buf + 1 + CREME_MAGIC_LEN, payload_len, text,
                                         sizeof(text))) {
                continue;
            }

            sender = creme_find_pseudo_by_ip(peers, peer_count, ntohl(from.sin_addr.s_addr));
            if (sender) {
                printf("Message de %s : %s\n", sender, text);
            } else {
                creme_addrip(ntohl(from.sin_addr.s_addr), ip_text, sizeof(ip_text));
                printf("Message de %s : %s\n", ip_text, text);
            }
            continue;
        }

        {
            int payload_len = n - (1 + CREME_MAGIC_LEN);
            if (payload_len <= 0) {
                continue;
            }
            if (payload_len > CREME_LBUF) {
                payload_len = CREME_LBUF;
            }

            memcpy(peer_pseudo, buf + 1 + CREME_MAGIC_LEN, (size_t)payload_len);
            peer_pseudo[payload_len] = '\0';
        }

        add_peer(ntohl(from.sin_addr.s_addr), peer_pseudo);

        if (buf[0] == CREME_CODE_DISCOVERY) {
            n = creme_build_message(buf, sizeof(buf), CREME_CODE_ACK, my_pseudo);
            if (n < 0) {
                continue;
            }
            if (sendto(sid, buf, (size_t)n, 0, (struct sockaddr *)&from, ls) == -1) {
                perror("sendto(ack)");
            }
        }
    }

    close(sid);
    return 0;
}
