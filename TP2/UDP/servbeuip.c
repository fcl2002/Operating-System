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

static int is_supported_code(char code)
{
    return code == CREME_CODE_QUIT || code == CREME_CODE_DISCOVERY || code == CREME_CODE_ACK ||
           code == CREME_CODE_LIST || code == CREME_CODE_SEND_TO_PSEUDO ||
           code == CREME_CODE_SEND_TO_ALL || code == CREME_CODE_DELIVER;
}

static int extract_peer_pseudo(const char *buf, int msg_len, char *peer_pseudo)
{
    int payload_len;

    payload_len = msg_len - (1 + CREME_MAGIC_LEN);
    if (payload_len <= 0) {
        return 0;
    }
    if (payload_len > CREME_LBUF) {
        payload_len = CREME_LBUF;
    }

    memcpy(peer_pseudo, buf + 1 + CREME_MAGIC_LEN, (size_t)payload_len);
    peer_pseudo[payload_len] = '\0';
    return 1;
}

static int send_quit_broadcast(int sid, const char *my_pseudo, char *buf, size_t buf_sz)
{
    int msg_len;
    struct sockaddr_in quit_bcast;

    memset(&quit_bcast, 0, sizeof(quit_bcast));
    quit_bcast.sin_family = AF_INET;
    quit_bcast.sin_port = htons(CREME_PORT);
    if (inet_aton(CREME_BCAST_ADDR, &quit_bcast.sin_addr) == 0) {
        fprintf(stderr, "Invalid broadcast address: %s\n", CREME_BCAST_ADDR);
        return 0;
    }

    msg_len = creme_build_message(buf, buf_sz, CREME_CODE_QUIT, my_pseudo);
    if (msg_len < 0) {
        return 0;
    }
    if (sendto(sid, buf, (size_t)msg_len, 0, (struct sockaddr *)&quit_bcast,
               sizeof(quit_bcast)) == -1) {
        perror("sendto(quit)");
        return 0;
    }
    return 1;
}

static void handle_quit_packet(int msg_len, const char *buf,
                               const struct sockaddr_in *from, char *peer_pseudo)
{
    if (!extract_peer_pseudo(buf, msg_len, peer_pseudo)) {
        return;
    }
    remove_peer(ntohl(from->sin_addr.s_addr), peer_pseudo);
}

static void handle_send_to_pseudo_packet(int sid, int msg_len, char *buf,
                                         char *target_pseudo, char *text)
{
    int payload_len;
    int used = 0;
    int out_len;
    uint32_t target_ip;
    struct sockaddr_in dst;

    payload_len = msg_len - (1 + CREME_MAGIC_LEN);
    if (!creme_extract_cstr(buf + 1 + CREME_MAGIC_LEN, payload_len, target_pseudo,
                            CREME_LBUF + 1, &used)) {
        return;
    }
    if (!creme_copy_payload_text(buf + 1 + CREME_MAGIC_LEN + used, payload_len - used,
                                 text, CREME_LBUF + 1)) {
        return;
    }
    if (!creme_find_ip_by_pseudo(peers, peer_count, target_pseudo, &target_ip)) {
        printf("[!] unknown pseudo: %s\n", target_pseudo);
        return;
    }

    out_len = creme_build_message(buf, CREME_LBUF + 1, CREME_CODE_DELIVER, text);
    if (out_len < 0) {
        return;
    }

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(CREME_PORT);
    dst.sin_addr.s_addr = htonl(target_ip);
    if (sendto(sid, buf, (size_t)out_len, 0, (struct sockaddr *)&dst, sizeof(dst)) == -1) {
        perror("sendto(deliver)");
    }
}

static void handle_send_to_all_packet(int sid, int msg_len, const char *my_pseudo,
                                      char *buf, char *text)
{
    int payload_len;
    int out_len;
    int i;

    payload_len = msg_len - (1 + CREME_MAGIC_LEN);
    if (!creme_copy_payload_text(buf + 1 + CREME_MAGIC_LEN, payload_len, text, CREME_LBUF + 1)) {
        return;
    }

    out_len = creme_build_message(buf, CREME_LBUF + 1, CREME_CODE_DELIVER, text);
    if (out_len < 0) {
        return;
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
        if (sendto(sid, buf, (size_t)out_len, 0, (struct sockaddr *)&dst, sizeof(dst)) == -1) {
            perror("sendto(all)");
        }
    }
}

static void handle_deliver_packet(int msg_len, const char *buf,
                                  const struct sockaddr_in *from, char *text)
{
    int payload_len;
    const char *sender;
    char ip_text[16];

    payload_len = msg_len - (1 + CREME_MAGIC_LEN);
    if (!creme_copy_payload_text(buf + 1 + CREME_MAGIC_LEN, payload_len, text, CREME_LBUF + 1)) {
        return;
    }

    sender = creme_find_pseudo_by_ip(peers, peer_count, ntohl(from->sin_addr.s_addr));
    if (sender) {
        printf("Message de %s : %s\n", sender, text);
    } else {
        creme_addrip(ntohl(from->sin_addr.s_addr), ip_text, sizeof(ip_text));
        printf("Message de %s : %s\n", ip_text, text);
    }
}

static int install_stop_handler(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_stop_signal;
    if (sigemptyset(&sa.sa_mask) == -1) {
        perror("sigemptyset");
        return 0;
    }
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        return 0;
    }
    return 1;
}

static int set_recv_timeout(int sid)
{
    struct timeval tv;

    tv.tv_sec = 1;
    tv.tv_usec = 0;
    if (setsockopt(sid, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
        perror("setsockopt(SO_RCVTIMEO)");
        return 0;
    }
    return 1;
}

int main(int argc, char *argv[])
{
    int sid;
    int msg_len;
    int yes = 1;
    struct sockaddr_in sock_conf;
    struct sockaddr_in from;
    struct sockaddr_in bcast;
    socklen_t from_len;
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

    if (!install_stop_handler()) {
        return 2;
    }

    sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sid < 0) {
        perror("socket");
        return 3;
    }

    if (setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) == -1) {
        perror("setsockopt(SO_BROADCAST)");
        close(sid);
        return 4;
    }

    if (!set_recv_timeout(sid)) {
        close(sid);
        return 5;
    }

    memset(&sock_conf, 0, sizeof(sock_conf));
    sock_conf.sin_family = AF_INET;
    sock_conf.sin_addr.s_addr = htonl(INADDR_ANY);
    sock_conf.sin_port = htons(CREME_PORT);

    if (bind(sid, (struct sockaddr *)&sock_conf, sizeof(sock_conf)) == -1) {
        perror("bind");
        close(sid);
        return 6;
    }

    memset(&bcast, 0, sizeof(bcast));
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(CREME_PORT);
    if (inet_aton(CREME_BCAST_ADDR, &bcast.sin_addr) == 0) {
        fprintf(stderr, "Invalid broadcast address: %s\n", CREME_BCAST_ADDR);
        close(sid);
        return 7;
    }

    msg_len = creme_build_message(buf, sizeof(buf), CREME_CODE_DISCOVERY, my_pseudo);
    if (msg_len < 0) {
        fprintf(stderr, "Pseudo too long\n");
        close(sid);
        return 8;
    }

    if (sendto(sid, buf, (size_t)msg_len, 0, (struct sockaddr *)&bcast, sizeof(bcast)) == -1) {
        perror("sendto(broadcast)");
        close(sid);
        return 9;
    }

    printf("BEUIP server on port %d, pseudo=%s\n", CREME_PORT, my_pseudo);
    printf("creme version: %s\n", creme_version());
    printf("Broadcast discovery sent to %s\n", CREME_BCAST_ADDR);

    for (;;) {
        if (stop_requested) {
            if (!send_quit_broadcast(sid, my_pseudo, buf, sizeof(buf))) {
                fprintf(stderr, "Failed to send quit broadcast\n");
            }
            printf("Stopping server after quit broadcast\n");
            break;
        }

        from_len = sizeof(from);
        msg_len = recvfrom(sid, buf, CREME_LBUF, 0, (struct sockaddr *)&from, &from_len);
        if (msg_len < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            perror("recvfrom");
            continue;
        }

        if (msg_len < (1 + CREME_MAGIC_LEN)) {
            continue;
        }

        if (!is_supported_code(buf[0])) {
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
            handle_quit_packet(msg_len, buf, &from, peer_pseudo);
            continue;
        }

        if (buf[0] == CREME_CODE_LIST) {
            print_peer_list();
            continue;
        }

        if (buf[0] == CREME_CODE_SEND_TO_PSEUDO) {
            handle_send_to_pseudo_packet(sid, msg_len, buf, target_pseudo, text);
            continue;
        }

        if (buf[0] == CREME_CODE_SEND_TO_ALL) {
            handle_send_to_all_packet(sid, msg_len, my_pseudo, buf, text);
            continue;
        }

        if (buf[0] == CREME_CODE_DELIVER) {
            handle_deliver_packet(msg_len, buf, &from, text);
            continue;
        }

        if (!extract_peer_pseudo(buf, msg_len, peer_pseudo)) {
            continue;
        }

        add_peer(ntohl(from.sin_addr.s_addr), peer_pseudo);

        if (buf[0] == CREME_CODE_DISCOVERY) {
            msg_len = creme_build_message(buf, sizeof(buf), CREME_CODE_ACK, my_pseudo);
            if (msg_len < 0) {
                continue;
            }
            if (sendto(sid, buf, (size_t)msg_len, 0, (struct sockaddr *)&from, from_len) == -1) {
                perror("sendto(ack)");
            }
        }
    }

    close(sid);
    return 0;
}
