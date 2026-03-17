/*****
* Minimal internal command launcher for BEUIP
*****/

#include "creme.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int build_text_from_args(int start, int argc, char *argv[], char *out, size_t out_sz)
{
    int i;

    out[0] = '\0';
    for (i = start; i < argc; ++i) {
        size_t current = strlen(out);
        size_t part = strlen(argv[i]);
        if (current + part + 2 >= out_sz) {
            return 0;
        }
        if (i > start) {
            out[current++] = ' ';
            out[current] = '\0';
        }
        strcat(out, argv[i]);
    }
    return 1;
}

static int pid_file_path(char *out, size_t out_sz)
{
    uid_t uid = getuid();
    int n = snprintf(out, out_sz, "/tmp/beuip-%u.pid", (unsigned int)uid);
    return (n > 0 && (size_t)n < out_sz);
}

static int read_pid_file(const char *path, pid_t *pid_out)
{
    FILE *f;
    long v;

    f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    if (fscanf(f, "%ld", &v) != 1 || v <= 1) {
        fclose(f);
        return 0;
    }
    fclose(f);
    *pid_out = (pid_t)v;
    return 1;
}

static int write_pid_file(const char *path, pid_t pid)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        return 0;
    }
    fprintf(f, "%ld\n", (long)pid);
    fclose(f);
    return 1;
}

static int get_program_dir(char *out, size_t out_sz)
{
    char exe[PATH_MAX];
    ssize_t n;
    char *slash;

    n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0 || (size_t)n >= sizeof(exe)) {
        return 0;
    }
    exe[n] = '\0';
    slash = strrchr(exe, '/');
    if (!slash) {
        return 0;
    }
    *slash = '\0';
    if (strlen(exe) + 1 > out_sz) {
        return 0;
    }
    strcpy(out, exe);
    return 1;
}

static int command_beuip_start(const char *pseudo)
{
    char pid_path[128];
    char dir[PATH_MAX];
    char serv_path[PATH_MAX];
    pid_t pid;

    if (!pid_file_path(pid_path, sizeof(pid_path))) {
        fprintf(stderr, "Cannot build pid path\n");
        return 1;
    }

    if (read_pid_file(pid_path, &pid) && kill(pid, 0) == 0) {
        fprintf(stderr, "BEUIP already running (pid=%ld)\n", (long)pid);
        return 2;
    }

    if (!get_program_dir(dir, sizeof(dir))) {
        fprintf(stderr, "Cannot resolve program directory\n");
        return 3;
    }

    if (snprintf(serv_path, sizeof(serv_path), "%s/servbeuip", dir) >= (int)sizeof(serv_path)) {
        fprintf(stderr, "Server path too long\n");
        return 4;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return 5;
    }

    if (pid == 0) {
        execl(serv_path, "servbeuip", pseudo, (char *)NULL);
        perror("execl(servbeuip)");
        _exit(127);
    }

    if (!write_pid_file(pid_path, pid)) {
        fprintf(stderr, "Cannot write pid file %s\n", pid_path);
        kill(pid, SIGKILL);
        return 6;
    }

    printf("BEUIP started: pid=%ld pseudo=%s\n", (long)pid, pseudo);
    return 0;
}

static int command_beuip_stop(void)
{
    char pid_path[128];
    pid_t pid;
    int i;

    if (!pid_file_path(pid_path, sizeof(pid_path))) {
        fprintf(stderr, "Cannot build pid path\n");
        return 1;
    }

    if (!read_pid_file(pid_path, &pid)) {
        fprintf(stderr, "No BEUIP pid file found\n");
        return 2;
    }

    if (kill(pid, SIGUSR1) == -1) {
        perror("kill(SIGUSR1)");
        unlink(pid_path);
        return 3;
    }

    for (i = 0; i < 50; ++i) {
        if (kill(pid, 0) == -1 && errno == ESRCH) {
            unlink(pid_path);
            printf("BEUIP stopped (pid=%ld)\n", (long)pid);
            return 0;
        }
        usleep(100000);
    }

    printf("Stop signal sent but process is still alive (pid=%ld)\n", (long)pid);
    printf("Keep pid file and retry 'biceps beuip stop'\n");
    return 4;
}

static int init_local_destination(struct sockaddr_in *dst)
{
    memset(dst, 0, sizeof(*dst));
    dst->sin_family = AF_INET;
    dst->sin_port = htons(CREME_PORT);
    if (inet_aton("127.0.0.1", &dst->sin_addr) == 0) {
        fprintf(stderr, "Invalid local destination address\n");
        return 0;
    }
    return 1;
}

static int mess_list(int sid, const struct sockaddr_in *dst)
{
    if (creme_send_list_cmd(sid, dst) == -1) {
        perror("sendto(list)");
        return 3;
    }
    printf("mess list sent\n");
    return 0;
}

static int mess_to(int sid, const struct sockaddr_in *dst, int argc, char *argv[])
{
    int rc;
    char text[700];

    if (argc < 5) {
        fprintf(stderr, "Usage: biceps mess to <pseudo> <message>\n");
        return 4;
    }

    if (!build_text_from_args(4, argc, argv, text, sizeof(text))) {
        fprintf(stderr, "Message too long\n");
        return 5;
    }

    rc = creme_send_message_cmd(sid, dst, argv[3], text);
    if (rc == -1) {
        perror("sendto(to)");
        return 6;
    }
    if (rc == -2) {
        fprintf(stderr, "Message too long for protocol frame\n");
        return 7;
    }
    printf("mess to sent: pseudo=%s text=%s\n", argv[3], text);
    return 0;
}

static int mess_all(int sid, const struct sockaddr_in *dst, int argc, char *argv[])
{
    int rc;
    char text[700];

    if (argc < 4) {
        fprintf(stderr, "Usage: biceps mess all <message>\n");
        return 8;
    }

    if (!build_text_from_args(3, argc, argv, text, sizeof(text))) {
        fprintf(stderr, "Message too long\n");
        return 9;
    }

    rc = creme_send_all_cmd(sid, dst, text);
    if (rc == -1) {
        perror("sendto(all)");
        return 10;
    }
    if (rc == -2) {
        fprintf(stderr, "Message too long for protocol frame\n");
        return 11;
    }
    printf("mess all sent: text=%s\n", text);
    return 0;
}

static int command_mess(int argc, char *argv[])
{
    int sid;
    int rc;
    struct sockaddr_in dst;

    if (argc < 3) {
        fprintf(stderr, "Usage: biceps mess list | to <pseudo> <message> | all <message>\n");
        return 1;
    }

    sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sid < 0) {
        perror("socket");
        return 2;
    }

    if (!init_local_destination(&dst)) {
        close(sid);
        return 13;
    }

    if (strcmp(argv[2], "list") == 0) {
        rc = mess_list(sid, &dst);
    } else if (strcmp(argv[2], "to") == 0) {
        rc = mess_to(sid, &dst, argc, argv);
    } else if (strcmp(argv[2], "all") == 0) {
        rc = mess_all(sid, &dst, argc, argv);
    } else {
        fprintf(stderr, "Unknown mess subcommand. Use: list | to | all\n");
        rc = 12;
    }

    close(sid);
    return rc;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: biceps beuip start <pseudo> | beuip stop | mess ...\n");
        return 1;
    }

    if (strcmp(argv[1], "beuip") == 0) {
        if (argc >= 4 && strcmp(argv[2], "start") == 0) {
            return command_beuip_start(argv[3]);
        }
        if (argc == 3 && strcmp(argv[2], "stop") == 0) {
            return command_beuip_stop();
        }
        fprintf(stderr, "Usage: biceps beuip start <pseudo> | biceps beuip stop\n");
        return 2;
    }

    if (strcmp(argv[1], "mess") == 0) {
        return command_mess(argc, argv);
    }

    fprintf(stderr, "Unknown command. Use: beuip | mess\n");
    return 3;
}
