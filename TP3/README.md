NOM: ROSIM DE SOUSA,COSTA LASMAR

PRENOM: Flavio,Fernando

# TP3 — BEUIP : Évolutions architecturales

Suite du TP2. Ce TP fait évoluer `biceps` sur trois axes : modèle de concurrence, détection réseau automatique et échange de fichiers.

---

## Compilation

```bash
make              # compile tout et produit ./biceps
make memory-leak  # compile avec -g -O0 pour valgrind -> ./biceps-memory-leaks
make clean        # nettoie tous les exécutables et .o
```

Pour activer les traces de débogage :

```bash
make biceps-trace # compile biceps avec -DTRACE_ON
```

`-DTRACE_ON` active la macro `TRACE` qui affiche sur stderr les événements internes : ajout/suppression de pairs, paquets UDP reçus, connexions TCP, transferts de fichiers.

---
Lancez `./biceps` pour entrer dans le REPL interactif. Les serveurs UDP et TCP doivent d'abord être démarrés avec `beuip start`.

```bash
./biceps
biceps> beuip start <pseudo> [<ip_locale>] [<reppub>]
```

### Paramètres de `beuip start`

- `<pseudo>` : pseudo de l'utilisateur local.
- `<ip_locale>` (optionnel) : adresse locale (utile pour deux instances en loopback : `127.0.0.1` / `127.0.0.2`).
- `<reppub>` (optionnel) : répertoire public exposé via TCP (défaut : `pub`). Doit exister avant le démarrage.

---

## Commandes du REPL

| Commande | Description |
|---|---|
| `beuip start <pseudo> [<ip>] [<rep>]` | Démarrer les serveurs UDP/TCP |
| `beuip list` | Afficher la liste des utilisateurs : `<IP> : <pseudo>` |
| `beuip message <user\|all> <message>` | Envoyer un message à un utilisateur ou à tous |
| `beuip ls <pseudo>` | Lister les fichiers du `reppub` d'un pair |
| `beuip get <pseudo> <nomfic>` | Télécharger un fichier depuis le `reppub` d'un pair |
| `beuip stop` | Arrêter les serveurs UDP et TCP |
| `mess add <pseudo> <ip>` | Enregistrer un pair manuellement (legacy) |
| `mess list` | Lister les pairs connus (legacy) |
| `mess to <pseudo> <message>` | Envoyer un message à un pair (legacy) |
| `mess all <message>` | Diffuser un message à tous les pairs (legacy)reppub` d'un pair |
| `beuip get <pseudo> <nomfic>` | Télécharger un fichier depuis le `reppub` d'un pair |
| `beuip find <nomfic>` | Chercher et télécharger `nomfic` chez tous les pairs |
| `beuip stop` | Arrêter les serveurs UDP et TCP |
| `quit` / `exit` | Quitter |

---

## Ce qui a été implémenté

### Étape 1 — Serveur UDP en thread POSIX

Le serveur BEUIP (`servbeuip`), lancé en processus fils dans TP2, est intégré comme thread POSIX dans `biceps`.

- Liste des pairs partagée via un mutex (`pthread_mutex_t`).
- IPC (tubes, signaux) supprimés.
- `void commande(char, char*, char*)` : point d'entrée unique pour `list`, `mess to`, `mess all`.
- Le serveur UDP rejette les codes internes (`'3'`, `'4'`, `'5'`) reçus du réseau.
- `beuip stop` envoie QUIT à chaque pair puis termine proprement les threads.

### Étape 2 — Détection réseau automatique + liste chaînée

- Broadcast dynamique via `getifaddrs()` + `getnameinfo()` — plus d'adresse codée en dur.
- Tableau statique `peers[]` remplacé par une liste chaînée (`struct elt`) triée alphabétiquement.
- Fonctions : `ajouteElt`, `supprimeElt`, `listeElts`.

### Étape 3 — Échange de fichiers (TCP port 9998)

- **Thread serveur TCP** lancé en parallèle du thread UDP lors du `beuip start`.
- **Protocole** : le client envoie un octet de commande, le serveur répond via `fork` + `exec`.

| Octet | Action serveur |
|---|---|
| `'L'` | `ls -l <reppub>` → socket client |
| `'F'` + nom + `\n` | `cat <reppub>/nomfic` → socket client (EOF si absent) |

- `demandeListe(pseudo)` — implémente `beuip ls`.
- `demandeFichier(pseudo, nomfic)` — implémente `beuip get` ; contrôles : nom invalide, fichier local déjà présent, fichier distant absent.
- `demandeTrouveFichier(nomfic)` — implémente `beuip find` ; essaie chaque pair connu jusqu'au premier succès.

### Étape 3.4 — Traces conditionnelles

Macro `TRACE` activée à la compilation avec `-DTRACE_ON`.
Points tracés : ajout/suppression de pairs, paquets UDP reçus (code, IP), connexions TCP acceptées, transferts de fichiers (connexion, octets reçus).
