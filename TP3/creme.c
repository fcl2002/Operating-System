#include "creme.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define CREME_FRAME_MAX 1024

const char *creme_version(void)
{
    return "1.0.0";
}

char *creme_addrip(uint32_t host_ip, char *out, size_t out_sz)
{
    if (out == NULL || out_sz < 16) {
        return NULL;
    }

    snprintf(out, out_sz, "%u.%u.%u.%u", (host_ip >> 24) & 0xFF,
             (host_ip >> 16) & 0xFF, (host_ip >> 8) & 0xFF, host_ip & 0xFF);
    return out;
}

int creme_build_message(char *out, size_t out_sz, char code, const char *payload)
{
    size_t plen;
    size_t total;

    if (out == NULL || payload == NULL) {
        return -1;
    }

    plen = strlen(payload);
    total = 1 + CREME_MAGIC_LEN + plen + 1;
    if (total > out_sz) {
        return -1;
    }

    out[0] = code;
    memcpy(out + 1, CREME_MAGIC, CREME_MAGIC_LEN);
    memcpy(out + 1 + CREME_MAGIC_LEN, payload, plen + 1);
    return (int)total;
}

int creme_extract_cstr(const char *src, int src_len, char *dst, size_t dst_len, int *consumed)
{
    int i;

    if (src == NULL || dst == NULL || consumed == NULL || src_len <= 0 || dst_len == 0) {
        return 0;
    }

    for (i = 0; i < src_len; ++i) {
        if (src[i] == '\0') {
            if ((size_t)i >= dst_len) {
                return 0;
            }
            memcpy(dst, src, (size_t)i + 1);
            *consumed = i + 1;
            return 1;
        }
    }

    return 0;
}

int creme_copy_payload_text(const char *src, int src_len, char *dst, size_t dst_len)
{
    int len;

    if (src == NULL || dst == NULL || src_len <= 0 || dst_len == 0) {
        return 0;
    }

    len = src_len;
    if (src_len > 0 && src[src_len - 1] == '\0') {
        len = src_len - 1;
    }
    if ((size_t)len >= dst_len) {
        len = (int)dst_len - 1;
    }

    memcpy(dst, src, (size_t)len);
    dst[len] = '\0';
    return 1;
}

int creme_is_local_command_source(const struct sockaddr_in *from)
{
    if (from == NULL) {
        return 0;
    }
    return from->sin_addr.s_addr == htonl(INADDR_LOOPBACK);
}

int creme_find_ip_by_pseudo(const creme_peer_t *peers, int peer_count,
                            const char *pseudo, uint32_t *ip_out)
{
    int i;

    if (peers == NULL || pseudo == NULL || ip_out == NULL) {
        return 0;
    }

    for (i = 0; i < peer_count; ++i) {
        if (strcmp(peers[i].pseudo, pseudo) == 0) {
            *ip_out = peers[i].ip;
            return 1;
        }
    }
    return 0;
}

const char *creme_find_pseudo_by_ip(const creme_peer_t *peers, int peer_count, uint32_t ip)
{
    int i;

    if (peers == NULL) {
        return NULL;
    }

    for (i = 0; i < peer_count; ++i) {
        if (peers[i].ip == ip) {
            return peers[i].pseudo;
        }
    }
    return NULL;
}

int creme_send_list_cmd(int sid, const struct sockaddr_in *dst)
{
    char msg[1 + CREME_MAGIC_LEN];

    msg[0] = CREME_CODE_LIST;
    memcpy(msg + 1, CREME_MAGIC, CREME_MAGIC_LEN);

    if (sendto(sid, msg, sizeof(msg), 0, (const struct sockaddr *)dst, sizeof(*dst)) == -1) {
        return -1;
    }
    return 0;
}

int creme_send_message_cmd(int sid, const struct sockaddr_in *dst,
                           const char *pseudo, const char *text)
{
    char msg[CREME_FRAME_MAX];
    size_t p_len;
    size_t t_len;
    size_t total;

    if (pseudo == NULL || text == NULL) {
        return -2;
    }

    p_len = strlen(pseudo);
    t_len = strlen(text);
    total = 1 + CREME_MAGIC_LEN + p_len + 1 + t_len + 1;

    if (total > sizeof(msg)) {
        return -2;
    }

    msg[0] = CREME_CODE_SEND_TO_PSEUDO;
    memcpy(msg + 1, CREME_MAGIC, CREME_MAGIC_LEN);
    memcpy(msg + 1 + CREME_MAGIC_LEN, pseudo, p_len + 1);
    memcpy(msg + 1 + CREME_MAGIC_LEN + p_len + 1, text, t_len + 1);

    if (sendto(sid, msg, total, 0, (const struct sockaddr *)dst, sizeof(*dst)) == -1) {
        return -1;
    }
    return 0;
}

int creme_send_all_cmd(int sid, const struct sockaddr_in *dst, const char *text)
{
    char msg[CREME_FRAME_MAX];
    size_t t_len;
    size_t total;

    if (text == NULL) {
        return -2;
    }

    t_len = strlen(text);
    total = 1 + CREME_MAGIC_LEN + t_len + 1;

    if (total > sizeof(msg)) {
        return -2;
    }

    msg[0] = CREME_CODE_SEND_TO_ALL;
    memcpy(msg + 1, CREME_MAGIC, CREME_MAGIC_LEN);
    memcpy(msg + 1 + CREME_MAGIC_LEN, text, t_len + 1);

    if (sendto(sid, msg, total, 0, (const struct sockaddr *)dst, sizeof(*dst)) == -1) {
        return -1;
    }
    return 0;
}
