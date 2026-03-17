#ifndef CREME_H
#define CREME_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

#define CREME_PORT 9998
#define CREME_LBUF 512
#define CREME_MAX_PEERS 255

#define CREME_MAGIC "BEUIP"
#define CREME_MAGIC_LEN 5

#define CREME_CODE_DISCOVERY '1'
#define CREME_CODE_ACK '2'
#define CREME_CODE_LIST '3'
#define CREME_CODE_SEND_TO_PSEUDO '4'
#define CREME_CODE_SEND_TO_ALL '5'
#define CREME_CODE_QUIT '0'
#define CREME_CODE_DELIVER '9'

#define CREME_BCAST_ADDR "192.168.88.255"

typedef struct {
    uint32_t ip; /* host byte order */
    char pseudo[256];
} creme_peer_t;

const char *creme_version(void);

char *creme_addrip(uint32_t host_ip, char *out, size_t out_sz);

int creme_build_message(char *out, size_t out_sz, char code, const char *payload);
int creme_extract_cstr(const char *src, int src_len, char *dst, size_t dst_len, int *consumed);
int creme_copy_payload_text(const char *src, int src_len, char *dst, size_t dst_len);

int creme_is_local_command_source(const struct sockaddr_in *from);

int creme_find_ip_by_pseudo(const creme_peer_t *peers, int peer_count,
                            const char *pseudo, uint32_t *ip_out);
const char *creme_find_pseudo_by_ip(const creme_peer_t *peers, int peer_count, uint32_t ip);

int creme_send_list_cmd(int sid, const struct sockaddr_in *dst);
int creme_send_message_cmd(int sid, const struct sockaddr_in *dst,
                           const char *pseudo, const char *text);
int creme_send_all_cmd(int sid, const struct sockaddr_in *dst, const char *text);

#endif
