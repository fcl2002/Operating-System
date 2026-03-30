# TP3 — BEUIP : Évolutions architecturales

Suite du TP2. Ce TP fait évoluer l'implémentation BEUIP sur trois axes : modèle de concurrence, détection réseau automatique et gestion dynamique des pairs.

---

## Étape 1 — Intégration du serveur UDP comme thread POSIX

Le serveur BEUIP (`servbeuip`), lancé en processus fils via `fork` + `execl` dans TP2, est remplacé par un thread POSIX (`pthread_create`) dans `biceps`.

- La liste des pairs est partagée entre le thread serveur et le REPL principal via un mutex (`pthread_mutex_t`).
- Les communications inter-processus (tubes, signaux) sont supprimées.
- La commande `beuip stop` envoie un paquet QUIT à chaque pair connu, puis termine proprement le thread.

### Section 1.2 — Commandes internes et sécurité

- `void commande(char octet1, char *message, char *pseudo)` : point d'entrée unique pour les commandes internes (`list`, `mess to`, `mess all`).
- Le serveur UDP rejette les codes internes (`'3'`, `'4'`, `'5'`) reçus du réseau (tentatives de piratage).
- Garde contre le double démarrage (`beuip_started`).

---

## Étape 2 — Détection automatique des interfaces réseau

### Section 2.1 — Adresse de broadcast dynamique

L'adresse de broadcast codée en dur (`192.168.88.255`) est remplacée par une détection automatique via `getifaddrs()` + `getnameinfo()`.

- Toutes les interfaces IPv4 actives (hors loopback) sont énumérées.
- Le paquet DISCOVERY est diffusé sur chaque broadcast détecté.
- Fonctionne sur n'importe quel réseau sans reconfiguration.

### Section 2.2 — Liste chaînée dynamique des pairs

Le tableau statique `peers[CREME_MAX_PEERS]` est remplacé par une liste chaînée allouée dynamiquement.

```c
struct elt {
    char       nom[LPSEUDO + 1];
    char       adip[16];
    struct elt *next;
};
```

Trois fonctions obligatoires :

| Fonction | Rôle |
|---|---|
| `ajouteElt(pseudo, adip)` | Insertion triée alphabétiquement par pseudo |
| `supprimeElt(adip)` | Suppression par adresse IP |
| `listeElts()` | Affichage de tous les pairs connus |

- Supprime la limite fixe de 255 pairs.
- L'affichage est toujours trié alphabétiquement.

---

## Compilation

```bash
make
```

## Utilisation

```bash
./biceps beuip start <pseudo> [<ip_locale>]
```

Le paramètre optionnel `<ip_locale>` permet de tester en local avec deux instances sur des adresses loopback distinctes (`127.0.0.1` et `127.0.0.2`).
