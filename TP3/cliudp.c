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
        argv[1] = nom de la machine serveur
        argv[2] = port
        argv[3] = message
*/
int main(int argc, char *argv[])
{
int sid;
struct hostent *server_host;
struct sockaddr_in server_addr;
struct sockaddr_in from_addr;
socklen_t from_len;
char ack[256];
ssize_t ack_len;

    if (argc != 4) {
        fprintf(stderr,"Utilisation : %s nom_serveur port message\n", argv[0]);
        return(1);
    }
    /* creation du socket */
    if ((sid=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)) < 0) {
        perror("socket");
        return(2);
    }
    /* recuperation adresse du serveur */
    if (!(server_host=gethostbyname(argv[1]))) {
        perror(argv[1]);
        return(3);
    }
    bzero(&server_addr,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    bcopy(server_host->h_addr,&server_addr.sin_addr,server_host->h_length);
    server_addr.sin_port = htons(atoi(argv[2]));
    if (sendto(sid,argv[3],strlen(argv[3]),MSG_CONFIRM,(struct sockaddr *)&server_addr,
                           sizeof(server_addr))==-1) {
        perror("sendto");
        return(4);
    }
    printf("Envoi OK !\n");

    /* Wait for server acknowledgement datagram. */
    from_len = sizeof(from_addr);
    ack_len = recvfrom(sid, ack, sizeof(ack) - 1, 0,
                       (struct sockaddr *)&from_addr, &from_len);
    if (ack_len < 0) {
        perror("recvfrom");
        return(5);
    }

    ack[ack_len] = '\0';
    printf("AR recu : %s\n", ack);
    return 0;
}

