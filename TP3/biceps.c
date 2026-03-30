/*****
* Interpréteur de commandes BEUIP avec serveur UDP en thread
*****/

#include "creme.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Variables partagées entre l'interpréteur et le thread serveur UDP  */
/* ------------------------------------------------------------------ */

/* Liste chaînée des pairs (remplace le tableau fixe peers[])         */
#define LPSEUDO 23
struct elt {
    char       nom[LPSEUDO + 1]; /* pseudo de l'élément               */
    char       adip[16];         /* adresse IPv4 sous forme de chaîne */
    struct elt *next;            /* prochain élément                  */
};

static struct elt     *head          = NULL;
static pthread_mutex_t peers_mutex   = PTHREAD_MUTEX_INITIALIZER;
static volatile int    stop_requested = 0;

/* Pseudo et IP locaux (écrits une seule fois avant la création du thread) */
static char udp_pseudo[256];
static char local_ip[16]; /* ex: "127.0.0.1" — vide = INADDR_ANY */

/* État du serveur : démarré une seule fois, actif ou non */
static int        beuip_started = 0;
static int        beuip_active  = 0;
static pthread_t  server_tid;

/* ------------------------------------------------------------------ */
/* Gestion de la liste chaînée de pairs                               */
/* ------------------------------------------------------------------ */

/*
 * ajouteElt - insère (pseudo, adip) dans la liste, triée par ordre
 * alphabétique croissant sur le pseudo. Ignoré si pseudo déjà présent.
 * Appelé par le serveur UDP (sous mutex).
 */
void ajouteElt(char *pseudo, char *adip)
{
    struct elt *e, *cur, *prev;

    pthread_mutex_lock(&peers_mutex);

    /* Vérifie l'unicité du pseudo */
    for (cur = head; cur != NULL; cur = cur->next) {
        if (strcmp(cur->nom, pseudo) == 0) {
            pthread_mutex_unlock(&peers_mutex);
            return;
        }
    }

    e = malloc(sizeof(struct elt));
    if (e == NULL) {
        pthread_mutex_unlock(&peers_mutex);
        return;
    }
    strncpy(e->nom,  pseudo, LPSEUDO);
    e->nom[LPSEUDO] = '\0';
    strncpy(e->adip, adip, 15);
    e->adip[15] = '\0';
    e->next = NULL;

    /* Insertion en ordre alphabétique */
    if (head == NULL || strcmp(pseudo, head->nom) < 0) {
        e->next = head;
        head    = e;
    } else {
        prev = head;
        cur  = head->next;
        while (cur != NULL && strcmp(pseudo, cur->nom) > 0) {
            prev = cur;
            cur  = cur->next;
        }
        e->next    = cur;
        prev->next = e;
    }

    pthread_mutex_unlock(&peers_mutex);
    printf("[+] present: %s (%s)\n", pseudo, adip);
}

/*
 * supprimeElt - supprime le pair identifié par son adresse IP.
 * Appelé par le serveur UDP (sous mutex).
 */
void supprimeElt(char *adip)
{
    struct elt *cur, *prev = NULL;

    pthread_mutex_lock(&peers_mutex);

    cur = head;
    while (cur != NULL) {
        if (strcmp(cur->adip, adip) == 0) {
            if (prev == NULL)
                head = cur->next;
            else
                prev->next = cur->next;
            printf("[-] left: %s (%s)\n", cur->nom, cur->adip);
            free(cur);
            pthread_mutex_unlock(&peers_mutex);
            return;
        }
        prev = cur;
        cur  = cur->next;
    }

    pthread_mutex_unlock(&peers_mutex);
}

/*
 * listeElts - affiche tous les pairs connus.
 * Appelée par le thread principal via la commande « mess list ».
 */
void listeElts(void)
{
    struct elt *cur;
    int i = 1;

    pthread_mutex_lock(&peers_mutex);
    printf("---- pairs connus ----\n");
    for (cur = head; cur != NULL; cur = cur->next)
        printf("%3d | %-24s | %s\n", i++, cur->nom, cur->adip);
    printf("----------------------\n");
    pthread_mutex_unlock(&peers_mutex);
}

/*
 * findAdipByNom - retourne l'adresse IP du pair dont le pseudo est nom.
 * Retourne 1 si trouvé (adip_out rempli), 0 sinon.
 */
static int findAdipByNom(const char *nom, char *adip_out, size_t out_sz)
{
    struct elt *cur;
    int found = 0;

    pthread_mutex_lock(&peers_mutex);
    for (cur = head; cur != NULL; cur = cur->next) {
        if (strcmp(cur->nom, nom) == 0) {
            strncpy(adip_out, cur->adip, out_sz - 1);
            adip_out[out_sz - 1] = '\0';
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&peers_mutex);
    return found;
}

/*
 * findNomByAdip - retourne le pseudo du pair à l'adresse adip.
 * Retourne 1 si trouvé (nom_out rempli), 0 sinon.
 */
static int findNomByAdip(const char *adip, char *nom_out, size_t out_sz)
{
    struct elt *cur;
    int found = 0;

    pthread_mutex_lock(&peers_mutex);
    for (cur = head; cur != NULL; cur = cur->next) {
        if (strcmp(cur->adip, adip) == 0) {
            strncpy(nom_out, cur->nom, out_sz - 1);
            nom_out[out_sz - 1] = '\0';
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&peers_mutex);
    return found;
}

/*
 * collectAdips - copie toutes les adresses IP connues dans le tableau.
 * Retourne le nombre d'entrées copiées.
 */
static int collectAdips(char adips[][16], int max)
{
    struct elt *cur;
    int n = 0;

    pthread_mutex_lock(&peers_mutex);
    for (cur = head; cur != NULL && n < max; cur = cur->next) {
        strncpy(adips[n], cur->adip, 15);
        adips[n][15] = '\0';
        ++n;
    }
    pthread_mutex_unlock(&peers_mutex);
    return n;
}

/* ------------------------------------------------------------------ */
/*                       Thread serveur UDP                            */
/* ------------------------------------------------------------------ */

static int set_recv_timeout(int sid)
{
    struct timeval tv;

    tv.tv_sec  = 1;
    tv.tv_usec = 0;
    if (setsockopt(sid, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
        perror("setsockopt(SO_RCVTIMEO)");
        return 0;
    }
    return 1;
}

/*
 * broadcast_on_all_interfaces - envoie un message BEUIP (code + pseudo)
 * en broadcast sur toutes les interfaces IPv4 actives sauf loopback.
 *
 * Utilise getifaddrs() pour énumérer les interfaces et getnameinfo()
 * avec ifa_broadaddr pour obtenir l'adresse broadcast de chacune.
 */
static void broadcast_on_all_interfaces(int sid, char code, const char *my_pseudo)
{
    struct ifaddrs *ifaddr, *ifa;
    char msg[CREME_LBUF + 1];
    char bcast_str[NI_MAXHOST];
    int msg_len;
    struct sockaddr_in dst;

    msg_len = creme_build_message(msg, sizeof(msg), code, my_pseudo);
    if (msg_len < 0)
        return;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        /* Ignorer les interfaces sans adresse ou sans broadcast */
        if (ifa->ifa_addr == NULL || ifa->ifa_broadaddr == NULL)
            continue;

        /* Ne traiter que les interfaces IPv4 */
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        /* Ignorer le loopback (127.x.x.x) */
        if (((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr ==
            htonl(INADDR_LOOPBACK))
            continue;

        /* Récupérer l'adresse broadcast sous forme de chaîne */
        if (getnameinfo(ifa->ifa_broadaddr, sizeof(struct sockaddr_in),
                        bcast_str, sizeof(bcast_str),
                        NULL, 0, NI_NUMERICHOST) != 0)
            continue;

        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_port   = htons(CREME_PORT);
        if (inet_aton(bcast_str, &dst.sin_addr) == 0)
            continue;

        if (sendto(sid, msg, (size_t)msg_len, 0,
                   (struct sockaddr *)&dst, sizeof(dst)) == -1)
            perror("sendto(broadcast)");
        else
            printf("Broadcast '%c' -> %s (%s)\n", code, bcast_str, ifa->ifa_name);
    }

    freeifaddrs(ifaddr);
}

/*
 * serveur_udp - thread serveur UDP BEUIP
 * p : pointeur vers le pseudo de l'utilisateur local (char *)
 *
 * N'accepte que : '0' (QUIT), '1' (DISCOVERY), '2' (ACK), '9' (DELIVER).
 * Signale une erreur pour '3','4','5' (commandes internes reçues du réseau).
 * QUIT et DELIVER sont sans AR.
 */
void *serveur_udp(void *p)
{
    const char *my_pseudo = (const char *)p;
    int sid, yes, msg_len, payload_len;
    struct sockaddr_in sock_conf, from;
    socklen_t from_len;
    char buf[CREME_LBUF + 1];
    char peer_pseudo[CREME_LBUF + 1];
    char text[CREME_LBUF + 1];
    char ip_text[16];
    int copy_len;

    yes = 1;

    sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sid < 0) {
        perror("socket");
        return NULL;
    }

    if (setsockopt(sid, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) == -1) {
        perror("setsockopt(SO_BROADCAST)");
        close(sid);
        return NULL;
    }

    if (!set_recv_timeout(sid)) {
        close(sid);
        return NULL;
    }

    memset(&sock_conf, 0, sizeof(sock_conf));
    sock_conf.sin_family = AF_INET;
    sock_conf.sin_port   = htons(CREME_PORT);

    if (local_ip[0] != '\0') {
        if (inet_aton(local_ip, &sock_conf.sin_addr) == 0) {
            fprintf(stderr, "IP locale invalide: %s\n", local_ip);
            close(sid);
            return NULL;
        }
    } else {
        sock_conf.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(sid, (struct sockaddr *)&sock_conf, sizeof(sock_conf)) == -1) {
        perror("bind");
        close(sid);
        return NULL;
    }

    /* Envoi du DISCOVERY sur toutes les interfaces actives (détection auto) */
    broadcast_on_all_interfaces(sid, CREME_CODE_DISCOVERY, my_pseudo);

    printf("Serveur UDP démarré, port %d, pseudo=%s\n", CREME_PORT, my_pseudo);

    for (;;) {
        if (stop_requested) {
            broadcast_on_all_interfaces(sid, CREME_CODE_QUIT, my_pseudo);
            printf("Arrêt du serveur UDP\n");
            break;
        }

        from_len = sizeof(from);
        msg_len  = recvfrom(sid, buf, CREME_LBUF, 0,
                            (struct sockaddr *)&from, &from_len);
        if (msg_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            perror("recvfrom");
            continue;
        }

        if (msg_len < (1 + CREME_MAGIC_LEN))
            continue;
        if (memcmp(buf + 1, CREME_MAGIC, CREME_MAGIC_LEN) != 0)
            continue;

        buf[msg_len] = '\0';
        payload_len  = msg_len - (1 + CREME_MAGIC_LEN);
        /* Adresse IP source sous forme de chaîne pour la liste chaînée */
        creme_addrip(ntohl(from.sin_addr.s_addr), ip_text, sizeof(ip_text));

        switch (buf[0]) {

        /* --- Codes réseau légitimes --- */

        case CREME_CODE_QUIT: /* '0' — sans AR */
            supprimeElt(ip_text);
            break;

        case CREME_CODE_DELIVER: /* '9' — sans AR */
            if (creme_copy_payload_text(buf + 1 + CREME_MAGIC_LEN,
                                        payload_len, text, sizeof(text))) {
                if (findNomByAdip(ip_text, peer_pseudo, sizeof(peer_pseudo)))
                    printf("\nMessage de %s : %s\n", peer_pseudo, text);
                else
                    printf("\nMessage de %s : %s\n", ip_text, text);
            }
            break;

        case CREME_CODE_DISCOVERY: /* '1' — avec AR (ACK) */
        case CREME_CODE_ACK:       /* '2' */
            if (payload_len > 0) {
                copy_len = (payload_len < CREME_LBUF) ? payload_len : CREME_LBUF;
                memcpy(peer_pseudo, buf + 1 + CREME_MAGIC_LEN, (size_t)copy_len);
                peer_pseudo[copy_len] = '\0';
                ajouteElt(peer_pseudo, ip_text);
            }
            if (buf[0] == CREME_CODE_DISCOVERY) {
                msg_len = creme_build_message(buf, sizeof(buf),
                                              CREME_CODE_ACK, my_pseudo);
                if (msg_len > 0) {
                    if (sendto(sid, buf, (size_t)msg_len, 0,
                               (struct sockaddr *)&from, from_len) == -1)
                        perror("sendto(ack)");
                }
            }
            break;

        /* --- Codes internes reçus du réseau : tentative de piratage --- */

        case CREME_CODE_LIST:           /* '3' */
        case CREME_CODE_SEND_TO_PSEUDO: /* '4' */
        case CREME_CODE_SEND_TO_ALL:    /* '5' */
            fprintf(stderr,
                    "[!] octet1='%c' rejeté depuis %s — commande interne sur le réseau\n",
                    buf[0], ip_text);
            break;

        default:
            fprintf(stderr,
                    "[!] octet1='%c' inconnu depuis %s\n", buf[0], ip_text);
            break;
        }
    }

    close(sid);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*          commande() — point d'entrée unique pour les 3 opérations  */
/*          internes (lecture de la table partagée sous mutex)        */
/* ------------------------------------------------------------------ */

/*
 * commande - exécute une commande interne BEUIP
 *   octet1  : '3' (liste), '4' (mess to pseudo), '5' (mess all)
 *   message : texte à envoyer (utilisé pour '4' et '5')
 *   pseudo  : destinataire (utilisé uniquement pour '4')
 */
static void build_text(char **args, int start, int argc,
                        char *out, size_t out_sz);
static int  open_send_socket(void);

void commande(char octet1, char *message, char *pseudo)
{
    int i, n, sid, msg_len;
    char adips[CREME_MAX_PEERS][16];
    char target_adip[16];
    char buf[CREME_LBUF + 1];
    struct sockaddr_in dst;

    switch (octet1) {

    case CREME_CODE_LIST: /* '3' — délègue à listeElts */
        listeElts();
        break;

    case CREME_CODE_SEND_TO_PSEUDO: /* '4' — message à un pseudo */
        if (pseudo == NULL || pseudo[0] == '\0' || message == NULL) {
            fprintf(stderr, "commande: pseudo ou message manquant\n");
            return;
        }
        if (!findAdipByNom(pseudo, target_adip, sizeof(target_adip))) {
            printf("[!] pseudo inconnu: %s\n", pseudo);
            return;
        }
        msg_len = creme_build_message(buf, sizeof(buf), CREME_CODE_DELIVER, message);
        if (msg_len < 0) { fprintf(stderr, "Message trop long\n"); return; }

        sid = open_send_socket();
        if (sid < 0) return;
        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_port   = htons(CREME_PORT);
        inet_aton(target_adip, &dst.sin_addr);
        if (sendto(sid, buf, (size_t)msg_len, 0,
                   (struct sockaddr *)&dst, sizeof(dst)) == -1)
            perror("sendto(deliver)");
        else
            printf("Message envoyé à %s\n", pseudo);
        close(sid);
        break;

    case CREME_CODE_SEND_TO_ALL: /* '5' — message à tous */
        if (message == NULL) { fprintf(stderr, "commande: message manquant\n"); return; }
        msg_len = creme_build_message(buf, sizeof(buf), CREME_CODE_DELIVER, message);
        if (msg_len < 0) { fprintf(stderr, "Message trop long\n"); return; }

        n = collectAdips(adips, CREME_MAX_PEERS);
        if (n == 0) { printf("Aucun pair connu\n"); return; }

        sid = open_send_socket();
        if (sid < 0) return;
        for (i = 0; i < n; ++i) {
            memset(&dst, 0, sizeof(dst));
            dst.sin_family = AF_INET;
            dst.sin_port   = htons(CREME_PORT);
            inet_aton(adips[i], &dst.sin_addr);
            if (sendto(sid, buf, (size_t)msg_len, 0,
                       (struct sockaddr *)&dst, sizeof(dst)) == -1)
                perror("sendto(all)");
        }
        close(sid);
        printf("Message diffusé à %d pair(s)\n", n);
        break;

    default:
        fprintf(stderr, "commande: octet1 non géré '%c'\n", octet1);
        break;
    }
}

/* ------------------------------------------------------------------ */
/*            Fonctions utilitaires pour l'interpréteur               */
/* ------------------------------------------------------------------ */

static void build_text(char **args, int start, int argc,
                        char *out, size_t out_sz)
{
    int i;
    size_t cur, part;

    out[0] = '\0';
    for (i = start; i < argc; ++i) {
        cur  = strlen(out);
        part = strlen(args[i]);
        if (cur + part + 2 >= out_sz)
            break;
        if (i > start) {
            out[cur++] = ' ';
            out[cur]   = '\0';
        }
        strcat(out, args[i]);
    }
}

/*
 * open_send_socket - socket UDP bindé sur local_ip pour que le
 * destinataire voie le bon IP source dans les paquets DELIVER.
 */
static int open_send_socket(void)
{
    int sid;
    struct sockaddr_in src;

    sid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sid < 0) { perror("socket"); return -1; }

    if (local_ip[0] != '\0') {
        memset(&src, 0, sizeof(src));
        src.sin_family = AF_INET;
        src.sin_port   = 0;
        if (inet_aton(local_ip, &src.sin_addr) == 0) { close(sid); return -1; }
        if (bind(sid, (struct sockaddr *)&src, sizeof(src)) == -1) {
            perror("bind(send socket)");
            close(sid);
            return -1;
        }
    }
    return sid;
}

/*
 * do_beuip_stop - envoie QUIT (octet1='0', sans AR) à chaque pair
 * connu individuellement, puis signale l'arrêt au thread serveur.
 */
static void do_beuip_stop(void)
{
    int i, n, sid, msg_len;
    char adips[CREME_MAX_PEERS][16];
    char buf[CREME_LBUF + 1];
    struct sockaddr_in dst;

    n = collectAdips(adips, CREME_MAX_PEERS);

    msg_len = creme_build_message(buf, sizeof(buf), CREME_CODE_QUIT, udp_pseudo);
    if (msg_len > 0 && n > 0) {
        sid = open_send_socket();
        if (sid >= 0) {
            for (i = 0; i < n; ++i) {
                memset(&dst, 0, sizeof(dst));
                dst.sin_family = AF_INET;
                dst.sin_port   = htons(CREME_PORT);
                inet_aton(adips[i], &dst.sin_addr);
                /* QUIT sans AR */
                sendto(sid, buf, (size_t)msg_len, 0,
                       (struct sockaddr *)&dst, sizeof(dst));
            }
            close(sid);
            printf("QUIT envoyé à %d pair(s)\n", n);
        }
    }

    stop_requested = 1;
}

static void repl_mess_add(const char *pseudo, const char *ip_str)
{
    /* Vérifie que l'IP est valide avant d'appeler ajouteElt */
    struct in_addr addr;
    char ip_copy[16];

    if (inet_aton(ip_str, &addr) == 0) {
        fprintf(stderr, "Adresse IP invalide: %s\n", ip_str);
        return;
    }
    strncpy(ip_copy, ip_str, 15);
    ip_copy[15] = '\0';
    ajouteElt((char *)pseudo, ip_copy);
}

/* ------------------------------------------------------------------ */
/*                        Boucle REPL                                  */
/* ------------------------------------------------------------------ */

static int parse_line(char *line, char **args, int max_args)
{
    int n = 0;
    char *p = line;
    size_t len = strlen(line);

    if (len > 0 && line[len - 1] == '\n')
        line[len - 1] = '\0';

    while (*p && n < max_args) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;
        args[n++] = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        if (*p) *p++ = '\0';
    }
    return n;
}

static void run_repl(void)
{
    char  line[512];
    char *args[32];
    int   argc;
    char  text[CREME_LBUF + 1];

    printf("BEUIP actif. Tapez 'aide' pour la liste des commandes.\n");

    for (;;) {
        printf("biceps> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
            break;

        argc = parse_line(line, args, 32);
        if (argc == 0) continue;

        /* --- beuip stop / beuip start --- */
        if (strcmp(args[0], "beuip") == 0 && argc >= 2) {
            if (strcmp(args[1], "stop") == 0) {
                if (!beuip_active) {
                    fprintf(stderr, "Serveur non actif\n");
                    continue;
                }
                do_beuip_stop();
                pthread_join(server_tid, NULL);
                beuip_active = 0;
                printf("Serveur arrêté. Tapez 'quit' pour quitter.\n");
                continue;
            }
            if (strcmp(args[1], "start") == 0) {
                fprintf(stderr, "Serveur déjà démarré (une seule fois par session)\n");
                continue;
            }
            fprintf(stderr, "Usage: beuip stop\n");
            continue;
        }

        /* --- quit / exit --- */
        if (strcmp(args[0], "quit") == 0 || strcmp(args[0], "exit") == 0)
            break;

        /* --- commandes mess --- */
        if (strcmp(args[0], "mess") == 0) {
            if (argc < 2) {
                fprintf(stderr,
                    "Usage: mess add|list|to|all ...\n");
                continue;
            }

            if (strcmp(args[1], "add") == 0) {
                if (argc != 4) {
                    fprintf(stderr, "Usage: mess add <pseudo> <ip>\n");
                    continue;
                }
                repl_mess_add(args[2], args[3]);

            } else if (strcmp(args[1], "list") == 0) {
                commande(CREME_CODE_LIST, NULL, NULL);

            } else if (strcmp(args[1], "to") == 0) {
                if (argc < 4) {
                    fprintf(stderr, "Usage: mess to <pseudo> <message>\n");
                    continue;
                }
                build_text(args, 3, argc, text, sizeof(text));
                commande(CREME_CODE_SEND_TO_PSEUDO, text, args[2]);

            } else if (strcmp(args[1], "all") == 0) {
                if (argc < 3) {
                    fprintf(stderr, "Usage: mess all <message>\n");
                    continue;
                }
                build_text(args, 2, argc, text, sizeof(text));
                commande(CREME_CODE_SEND_TO_ALL, text, NULL);

            } else {
                fprintf(stderr, "Sous-commande inconnue: %s\n", args[1]);
            }
            continue;
        }

        /* --- aide --- */
        if (strcmp(args[0], "aide") == 0 || strcmp(args[0], "help") == 0) {
            printf("Commandes disponibles:\n");
            printf("  mess add <pseudo> <ip>     - enregistrer un pair manuellement\n");
            printf("  mess list                  - lister les pairs connus\n");
            printf("  mess to <pseudo> <message> - envoyer à un pair\n");
            printf("  mess all <message>         - diffuser à tous les pairs\n");
            printf("  beuip stop                 - arrêter le serveur UDP\n");
            printf("  quit / exit                - quitter\n");
            continue;
        }

        fprintf(stderr, "Commande inconnue: %s\n", args[0]);
    }

    /* Si on quitte le REPL sans avoir arrêté le serveur, on l'arrête ici */
    if (beuip_active) {
        do_beuip_stop();
        pthread_join(server_tid, NULL);
        beuip_active = 0;
    }
}

/* ------------------------------------------------------------------ */
/*                     Commande beuip start                            */
/* ------------------------------------------------------------------ */

static int command_beuip_start(const char *pseudo, const char *ip)
{
    if (beuip_started) {
        fprintf(stderr, "beuip start ne peut être fait qu'une seule fois\n");
        return 1;
    }

    strncpy(udp_pseudo, pseudo, sizeof(udp_pseudo) - 1);
    udp_pseudo[sizeof(udp_pseudo) - 1] = '\0';

    if (ip != NULL) {
        strncpy(local_ip, ip, sizeof(local_ip) - 1);
        local_ip[sizeof(local_ip) - 1] = '\0';
        printf("IP locale: %s\n", local_ip);
    } else {
        local_ip[0] = '\0';
    }

    if (pthread_create(&server_tid, NULL, serveur_udp, udp_pseudo) != 0) {
        perror("pthread_create");
        return 2;
    }

    beuip_started = 1;
    beuip_active  = 1;

    run_repl();

    printf("BEUIP arrêté\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/*                              main                                   */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: biceps beuip start <pseudo> [<ip_locale>]\n");
        return 1;
    }

    if (strcmp(argv[1], "beuip") == 0) {
        if (argc >= 4 && strcmp(argv[2], "start") == 0) {
            const char *ip = (argc >= 5) ? argv[4] : NULL;
            return command_beuip_start(argv[3], ip);
        }
        fprintf(stderr, "Usage: biceps beuip start <pseudo> [<ip_locale>]\n");
        return 2;
    }

    fprintf(stderr, "Commande inconnue. Usage: biceps beuip start <pseudo> [<ip_locale>]\n");
    return 3;
}
