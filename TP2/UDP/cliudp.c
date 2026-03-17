/*****
* Exemple de client UDP
* socket en mode non connecte
*****/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

/* parametres :
        P[1] = nom de la machine serveur
        P[2] = port
        P[3] = message
*/
int main(int N, char*P[])
{
int sid;
struct hostent *h;
struct sockaddr_in Sock;
struct sockaddr_in From;
socklen_t from_len;
char ack[256];
ssize_t ack_len;

    if (N != 4) {
        fprintf(stderr,"Utilisation : %s nom_serveur port message\n", P[0]);
        return(1);
    }
    /* creation du socket */
    if ((sid=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)) < 0) {
        perror("socket");
        return(2);
    }
    /* recuperation adresse du serveur */
    if (!(h=gethostbyname(P[1]))) {
        perror(P[1]);
        return(3);
    }
    bzero(&Sock,sizeof(Sock));
    Sock.sin_family = AF_INET;
    bcopy(h->h_addr,&Sock.sin_addr,h->h_length);
    Sock.sin_port = htons(atoi(P[2]));
    if (sendto(sid,P[3],strlen(P[3]),MSG_CONFIRM,(struct sockaddr *)&Sock,
                           sizeof(Sock))==-1) {
        perror("sendto");
        return(4);
    }
    printf("Envoi OK !\n");

    /* Wait for server acknowledgement datagram. */
    from_len = sizeof(From);
    ack_len = recvfrom(sid, ack, sizeof(ack) - 1, 0,
                       (struct sockaddr *)&From, &from_len);
    if (ack_len < 0) {
        perror("recvfrom");
        return(5);
    }

    ack[ack_len] = '\0';
    printf("AR recu : %s\n", ack);
    return 0;
}

