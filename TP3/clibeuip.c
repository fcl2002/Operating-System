/*****
* BEUIP client for local test commands
*****/

#include "creme.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int sid;
    struct sockaddr_in dst;
    char text[700];
    int i;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s liste | %s msg <pseudo> <message> | %s all <message>\n",
                argv[0], argv[0], argv[0]);
        return 1;
    }

    sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sid < 0) {
        perror("socket");
        return 2;
    }

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(CREME_PORT);
    if (inet_aton("127.0.0.1", &dst.sin_addr) == 0) {
        fprintf(stderr, "Invalid destination address\n");
        close(sid);
        return 3;
    }

    if (strcmp(argv[1], "liste") == 0) {
        if (argc != 2) {
            fprintf(stderr, "Usage: %s liste\n", argv[0]);
            close(sid);
            return 4;
        }
        if (creme_send_list_cmd(sid, &dst) == -1) {
            perror("sendto");
            close(sid);
            return 5;
        }
        printf("Command 'liste' sent to 127.0.0.1:%d\n", CREME_PORT);
        close(sid);
        return 0;
    }

    if (strcmp(argv[1], "msg") == 0) {
        text[0] = '\0';
        if (argc < 4) {
            fprintf(stderr, "Usage: %s msg <pseudo> <message>\n", argv[0]);
            close(sid);
            return 6;
        }

        for (i = 3; i < argc; ++i) {
            size_t current = strlen(text);
            size_t part = strlen(argv[i]);
            if (current + part + 2 >= sizeof(text)) {
                fprintf(stderr, "Message too long\n");
                close(sid);
                return 7;
            }
            if (i > 3) {
                text[current++] = ' ';
                text[current] = '\0';
            }
            strcat(text, argv[i]);
        }

        i = creme_send_message_cmd(sid, &dst, argv[2], text);
        if (i == -2) {
            fprintf(stderr, "Message too long for protocol frame\n");
            close(sid);
            return 8;
        }
        if (i == -1) {
            perror("sendto");
            close(sid);
            return 9;
        }
        printf("Command 'msg' sent: pseudo=%s text=%s\n", argv[2], text);
        close(sid);
        return 0;
    }

    if (strcmp(argv[1], "all") == 0) {
        text[0] = '\0';
        if (argc < 3) {
            fprintf(stderr, "Usage: %s all <message>\n", argv[0]);
            close(sid);
            return 11;
        }

        for (i = 2; i < argc; ++i) {
            size_t current = strlen(text);
            size_t part = strlen(argv[i]);
            if (current + part + 2 >= sizeof(text)) {
                fprintf(stderr, "Message too long\n");
                close(sid);
                return 12;
            }
            if (i > 2) {
                text[current++] = ' ';
                text[current] = '\0';
            }
            strcat(text, argv[i]);
        }

        i = creme_send_all_cmd(sid, &dst, text);
        if (i == -2) {
            fprintf(stderr, "Message too long for protocol frame\n");
            close(sid);
            return 13;
        }
        if (i == -1) {
            perror("sendto");
            close(sid);
            return 14;
        }
        printf("Command 'all' sent: text=%s\n", text);
        close(sid);
        return 0;
    }

    fprintf(stderr, "Unknown command. Use: liste | msg <pseudo> <message> | all <message>\n");
    close(sid);
    return 10;
}
