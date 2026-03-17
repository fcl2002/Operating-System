/*
 * servtcp.c : exemple de serveur socket mode connecte
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

struct sockaddr_in Sin = {AF_INET}; /* le reste est nul */

struct elt_dns
{
   char nom[30];
   char adip[20];
} annuaire [] = {
    "chaource", "192.168.99.1",
    "roquefort", "192.168.99.2",
    "", ""
};

int readlig(int fd, char *b, int max)
{
int n;
char c;
    for (n=0; n<max; n++) {
        if(read(fd, &c, 1) <= 0) break;
        if (c == '\n') break;
        *b++ = c;
    }
    *b = '\0';
    return(n);
}

#define LBUF 100
void service(int sid)
{
int n, i;
char buf[LBUF];
    n = readlig(sid,buf,LBUF);
    if (n < 0) {
       perror("readlig");
       return;
    }
    i = 0;
    while (strlen(annuaire[i].nom) > 0) {
        if (strcmp(buf,annuaire[i].nom) == 0) {
           write(sid,annuaire[i].adip,20);
           close(sid);
           return;
        }
        i++;
    }
    write(sid,"erreur !! ", 11);
    close(sid);
}

int main(int N, char *P[])
{
int ln, sock, nsock;
    /* creation du socket */
    if((sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) < 0) {
        perror("socket");
        exit(1);
    }
    /* ATTACHEMENT AU PORT */
    if(bind(sock,(struct sockaddr*)&Sin, sizeof(Sin)) < 0) {
        perror("bind");
        exit(2);
    }
    ln = sizeof(Sin);
    if (getsockname(sock,(struct sockaddr*)&Sin,(socklen_t*)&ln) < 0) {
        perror("getsockname");
        exit(3);
    }
    printf("Le serveur est attache au port %u\n",ntohs(Sin.sin_port));
    /* definition du nb d'appels simultanes */
    if (listen(sock,5) < 0) {
        perror("listen");
        exit(4);
    }
    /* boucle d'attente */
    for (;;) {
        if((nsock=accept(sock,(struct sockaddr*)&Sin,(socklen_t*)&ln))<0) {
            perror("accept");
            exit(5);
        }
        service(nsock);
    }
}




