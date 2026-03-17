# TP2 Système d'Exploitation - Outils UDP en C
Projet de TP en C implémentant un client/serveur UDP, une bibliothèque partagée `creme` et un lanceur de commandes `biceps`.
Le dossier contient deux binaires de base (`cliudp`, `servudp`) pour l’échange UDP simple et une version BEUIP (`servbeuip`, `clibeuip`).

Fonctionnalités principales :
- Envoi/réception UDP simple : `cliudp` -> `servudp` avec accusé de réception.
- Bibliothèque `creme` (`libcreme.so`) pour factoriser la logique de trame/protocole.
- Commande `beuip start <pseudo>` / `beuip stop` via `biceps` (fork + signal).
- Commande `mess` via `biceps` : `list`, `to <pseudo> <message>`, `all <message>`.

Compilation :
- Standard : `make` (flags strictes `-Wall -Werror` définies dans le Makefile).
- Debug (ponctuel) : `make clean && make CFLAGS="-Wall -Werror -g"`.

Exécution rapide :
- UDP simple : `./servudp` puis `./cliudp 127.0.0.1 9999 "bonjour"`.
- BEUIP : `./biceps beuip start fernando`, puis `./biceps mess list|to <pseudo> <msg>|all <msg>`, arrêt avec `./biceps beuip stop`.

Remarque : les tests de broadcast dépendent du réseau local (adressage et autorisation broadcast de l’environnement).