/*****
* Exemple de serveur UDP
* socket en mode non connecte
*****/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define PORT 9999
#define LBUF 512

char buf[LBUF+1];
struct sockaddr_in SockConf; /* pour les operation du serveur : mise a zero */

char * addrip(unsigned long A)
{
static char b[16];
  sprintf(b,"%u.%u.%u.%u",(unsigned int)(A>>24&0xFF),(unsigned int)(A>>16&0xFF),
         (unsigned int)(A>>8&0xFF),(unsigned int)(A&0xFF));
  return b;
}

int main(int N, char*P[]) {
  int sid, n;
  struct sockaddr_in Sock;
  socklen_t ls;

    /* creation du socket */
    if ((sid=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP)) < 0) {
        perror("socket");
        return(2);
    }
    /* initialisation de SockConf pour le bind */
    SockConf.sin_family = AF_INET;
    SockConf.sin_port = htons(PORT);
    if (bind(sid, (struct sockaddr *) &SockConf, sizeof(SockConf)) == -1) {
        perror("bind");
        return(3);
    }
    printf("Le serveur est attache au port %d !\n",PORT);
    for (;;) {
      ls = sizeof(Sock);
      /* on attend un message */
      if ((n=recvfrom(sid,(void*)buf,LBUF,0,(struct sockaddr *)&Sock,&ls))
           == -1)  perror("recvfrom");
      else {
        buf[n] = '\0';
        printf ("Reçu de %s : <%s>\n",addrip(ntohl(Sock.sin_addr.s_addr)), buf);

        const char *ack = "Bien recu 5/5 !";
        if (sendto(sid, ack, strlen(ack), 0, (struct sockaddr *)&Sock, ls) == -1) {
            perror("sendto");
        }
      }
    }
    return 0;
}

