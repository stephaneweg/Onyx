# TAATU sur Onyx — brief pour le Claude de Lucas

> **À qui s'adresse ce document.** À Claude (ou tout développeur) travaillant pour **Lucas Catale**,
> propriétaire de **taatu.world** (édité par Sofo Solutions). Le but est d'écrire un **client natif
> TAATU pour Onyx**, sans HTML ni navigateur.
>
> **Les rôles.** Stéphane Wegener est l'auteur d'Onyx et de ce dépôt public
> (`github.com/stephaneweg/Onyx`). Lucas est propriétaire du jeu, de son code client/serveur et de
> son protocole. C'est lui qui a demandé ce portage. Les sources et le protocole de TAATU sont chez
> lui : ce document ne contient **rien** de TAATU, seulement ce qu'il faut savoir d'Onyx pour y
> écrire l'app.

---

## 1. Ce qu'on attend

**Le livrable** est une app Onyx `taatu.app`, c'est-à-dire un dossier à copier sur la carte SD du Pi :

```
SD:/apps/taatu.app/
  main        l'ELF AArch64 (sans extension), compilé par Lucas
  app.txt     nom affiché + catégorie (+ taille de pile)
  icon.bmp    icône 40×40, BMP 24 bpp, magenta 0xFF00FF = transparent (optionnel)
  (données)   un pack d'assets optionnel, ou un cache rempli au premier lancement
```

**Ce qui a été convenu avec Stéphane :**

- **Lucas compile et distribue le binaire.** Le code de TAATU reste privé chez lui. Rien de TAATU
  ne va dans ce dépôt public : ni sources, ni sprites, ni décors, ni sons.
- **Les graphismes et les sons ne sont pas commités ici.** D'après les mentions de taatu.world, ils
  appartiennent à leurs créateurs d'origine. Deux options, au choix de Lucas :
  1. **(recommandé)** le client **télécharge les assets depuis taatu.world au lancement**, comme le
     site, et les garde en cache (§6.4). Le binaire reste léger et les assets à jour sans
     recompiler ;
  2. un **pack embarqué**, chiffré si Lucas le souhaite (AES-GCM via mbedTLS, déjà dans Onyx). Ça
     dissuade sans protéger vraiment : la clé est dans le binaire.
- **Le binaire fermé ne doit lier aucun code GPL** : ni NetSurf (`user/netsurf/`), ni Circle (de
  toute façon côté noyau). Ce qu'on peut lier est listé au §9.

**Variante possible.** Si Lucas préfère que Stéphane écrive le client, il suffit de fournir une spec
du protocole et des formats (contenu attendu : §10). Stéphane fait alors le portage côté Onyx.

---

## 2. Onyx en dix lignes

- C'est un OS maison **multi-processus pour Raspberry Pi 4** (AArch64, Cortex-A72), avec
  **Circle** comme couche matérielle. GUI fenêtrée (un « CDE modernisé »), Wi-Fi/Ethernet, son,
  carte SD en FAT32.
- **Une app est un ELF** chargé à `0x200000000` (non PIE), dans son propre espace d'adressage,
  **à EL0** (protégée : une faute ne tue que l'app, jamais le système), préemptée, avec des
  threads disponibles. Elle ne doit lire aucun registre système privilégié (`mpidr_el1`…) :
  `tools/el0scan.sh` vérifie un binaire.
- **Pas de POSIX, pas d'appels système Linux.** L'app parle au noyau uniquement via une **table de
  fonctions à adresse fixe** : la *kapi* (`user/kapi.h` → `kernel/include/kern/kapi_abi.h`) ;
  chaque entrée est un petit stub qui fait l'appel système (`svc`), chaque pointeur passé est
  vérifié par le noyau.
- **La kapi n'accepte que des ajouts** : on ne retire ni ne réordonne jamais un champ. Un ELF
  compilé aujourd'hui continue donc de tourner sur les noyaux futurs. La version actuelle est
  **`KAPI_ABI_VERSION` 74**. L'app peut lire `KT->version` et refuser poliment de démarrer sur un
  noyau trop ancien.
- **newlib** (`printf`, `malloc`, `<string.h>`, `<math.h>`) est disponible pour les apps qui la
  lient (§5). Le C++ s'utilise **sans exceptions ni RTTI**.
- Le **flottant matériel** est disponible si l'app le demande (`-mcpu=cortex-a72`, sans
  `-mgeneral-regs-only`). C'est le cas de toutes les apps newlib.

---

## 3. Démarrer : cloner, lire, construire

### 3.1. Cloner

```sh
git clone https://github.com/stephaneweg/Onyx.git
cd Onyx
# Circle (le noyau) n'est nécessaire que pour reconstruire le NOYAU, pas pour une app :
# git submodule update --init circle
```

> Le dossier s'appelle parfois `Zircon` : c'est un ancien nom, sans rapport avec Fuchsia. Des
> chaînes à l'écran disent encore « Zircon ». La doc dit « Onyx ».

### 3.2. Lire la doc, dans cet ordre

| Fichier | Quoi lire | Pourquoi |
|---|---|---|
| `CLAUDE.md` | tout (court) | règles du dépôt, build |
| `docs/HANDOFF.md` | « Builds », « Trying it on the Pi » | toolchain exacte, déploiement |
| `docs/03-DEVELOPER-GUIDE.md` | §1, §3, §4, **§5 (5.1, 5.2, 5.3)**, **§6**, §8, **§9**, **§13** | modèle d'app, newlib, threads, `RAM:`, GUI, packaging, pièges |
| `docs/02-KERNEL-INTERNALS.md` | la table de l'ABI kapi | toutes les fonctions noyau et leur version |
| `user/kapi.h` | en entier, c'est la vraie référence | les commentaires précisent les codes de retour |
| `docs/04-USER-GUIDE.md` | la carte SD, `system.ini`, le réseau | ce que voit l'utilisateur |
| `user/Apps/2048/main.cpp` | 190 lignes | **squelette** d'une app GUI minimale |
| `user/Apps/courier/` | `net.h` | **modèle** d'app réseau : HTTPS + JSON + thread réseau + newlib + FreeType |
| `user/tls/onyx_tls.hpp`, `user/http.hpp`, `user/json.hpp`, `user/img/imgload.hpp` | les en-têtes | les briques qu'on réutilise (§4) |

### 3.3. La toolchain

- **Arm GNU Toolchain 13.3.rel1, `aarch64-none-elf`** (Linux x86-64, ou WSL sous Windows) :
  `https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf.tar.xz`
  Elle fournit newlib (`libc`, `libm`).
- `export PATH=<toolchain>/bin:$PATH`
- Pour les tests sur PC (§7) : `g++`, `python3` + Pillow + numpy, et en option
  `g++-aarch64-linux-gnu` + `qemu-user`.

### 3.4. Construire les bibliothèques d'appui (une fois)

L'app n'a **pas besoin du noyau** : les apps n'ont aucun lien avec ses adresses. Il suffit de
construire, depuis `user/` :

```sh
cd user
make wtk/libwtk.a ft/libft.a libc/crt0libc.o libc/onyx_syscalls.o
# mbedTLS est déjà construit dans third_party/mbedtls-3.6.3/library/libmbed{tls,x509,crypto}.a
# (sinon : make -C user/tls)
```

Un `make` complet depuis `kernel/` (puis `make stage`) construit tout, noyau et apps, dans
`sdcard/`. C'est utile pour avoir une carte SD Onyx complète à tester (§8).

---

## 4. Ce qu'Onyx fournit déjà

| Besoin du client TAATU | Dans Onyx | Où |
|---|---|---|
| Fenêtre + framebuffer 32 bits `0x00RRGGBB` | `kapi_create_window (w, h, titre)` → `unsigned *` | `user/kapi.h`, ex. `Apps/2048` |
| Plein écran | `kapi_fullscreen_begin / kapi_present_fb / kapi_fullscreen_end` (v41), accès direct v55 | docs/03 §6, `Apps/plasma` |
| Widgets (champs texte, boutons, listes, dialogues, menus) | **wtk** (C++) | `user/wtk/*.h`, docs/03 §6 |
| Souris / clavier | `kapi_set_pointer_handler`, `kapi_set_key_handler`, `kapi_get_modifiers` | `user/kapi.h` |
| Texte TrueType anti-aliasé (chat, pseudos) | FreeType « lean » + `ft/fonts.h` (`fnt::get`, `fnt::draw`) | docs/03 §6 « TrueType text » |
| Boucle d'événements | `kapi_pump_events`, `kapi_pump_wait (ms)`, `kapi_should_exit` | docs/03 §5.2 |
| TCP + DNS | `kapi_tcp_connect (host, port)`, `kapi_tcp_send` / `kapi_tcp_recv` (non bloquant) / `kapi_tcp_close`, `kapi_net_status` | `user/kapi.h` |
| **TLS 1.2 / 1.3** | mbedTLS 3.6.3 + `onyx_tls.hpp` (`start`, `send`, `recv`, vérification des certificats en option) | `user/tls/` |
| HTTPS (login, API REST, assets) | `HttpClient` (`ONYX_HTTP_TLS`) ; plus complet : `Apps/courier/net.h` (redirections, cookies, gzip) | `user/http.hpp` |
| **WebSocket** | ⚠ pas de client réutilisable seul : celui de Jet (`user/netsurf/onyx_ws.c`) est lié à NetSurf, donc **GPL**. **Ne pas le copier.** En écrire un petit (RFC 6455 client, ~300 lignes) sur `onyx_tls` : §6.2 | — |
| JSON | `json.hpp` (arène, accès typés tolérants, writer en flux) | `user/json.hpp` |
| PNG / JPEG / GIF animé / WebP / BMP | `img_load (fichier)`, **`img_load_mem (octets, len)`** → `0xAARRGGBB` | `user/img/imgload.hpp` |
| Inflate (zlib / gzip / deflate brut) | `img_inflate` | idem |
| Threads, mutex, events, futex | `kapi_thread_create`, `kapi_post` (renvoyer un résultat au thread GUI) | docs/03 §5.2 |
| Son | PCM **s16 stéréo 44 100 Hz** : `kapi_sound_acquire` + `kapi_sound_write`, plus des voix synthé | `user/kapi.h` (l.~308) |
| Décodage MP3 / OGG | ⚠ **absent**. Pour la radio ou les sons compressés, embarquer un décodeur à licence permissive (p. ex. `minimp3` CC0, `stb_vorbis` domaine public) | — |
| Fichiers | `SD:/...` (FAT32, persistant), **`RAM:/...`** (en mémoire, perdu au redémarrage, rapide : idéal pour un cache) ; newlib `fopen` marche sur les deux | docs/03 §5.3 |
| Aléa matériel | `kapi_random` | `user/kapi.h` |
| Notifications, presse-papiers | `notify.h`, `clipboard.h` | docs/03 §6 |
| Configuration | `app_ini_load` (`config.ini` de l'app) | docs/03 §8–9 |

---

## 5. Squelette de build (app hors dépôt)

L'app de Lucas peut vivre dans **son propre dossier privé**, avec un Makefile qui pointe vers un clone
d'Onyx. Le modèle est la règle `courier.elf` de `user/Makefile` (newlib + wtk + FreeType + mbedTLS) :

```make
# Makefile -- taatu.app pour Onyx (hors du dépôt Onyx)
ONYX    ?= $(HOME)/Onyx
U        = $(ONYX)/user
PREFIX  ?= aarch64-none-elf-
CXX      = $(PREFIX)g++
FT_SRC   = $(ONYX)/third_party/freetype-2.14.3
MBEDTLS  = $(ONYX)/third_party/mbedtls-3.6.3

CXXFLAGS = -mcpu=cortex-a72 -O2 -fno-stack-protector -fno-pic -fno-pie -nostartfiles \
           -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti \
           -fno-threadsafe-statics -fno-use-cxa-atexit \
           -I$(U) -I$(ONYX)/kernel/include -I$(U)/ft -I$(FT_SRC)/include -I$(MBEDTLS)/include
LDFLAGS  = -Wl,-T,$(U)/user.ld -Wl,-z,max-page-size=0x10000 -Wl,--build-id=none -Wl,--gc-sections

SRC = $(wildcard src/*.cpp)

taatu.elf: $(SRC) $(wildcard src/*.h)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(U)/libc/crt0libc.o $(U)/libc/onyx_syscalls.o $(SRC) \
	    $(U)/wtk/libwtk.a $(U)/ft/libft.a \
	    -L$(MBEDTLS)/library -lmbedtls -lmbedx509 -lmbedcrypto -lm -o $@

stage: taatu.elf
	mkdir -p out/taatu.app && cp taatu.elf out/taatu.app/main && cp app.txt out/taatu.app/
```

Le `app.txt` :

```ini
name     = TAATU
category = Games          ; ou Internet
stack    = 2M             ; la pile par défaut est 256 KB : à augmenter si décodage / JSON profond
```

**Points d'attention :**

- **Une seule app « newlib »**, c'est-à-dire `crt0libc.o` (qui appelle `exit(main())`). **Pas
  de `crt0.S`**, qui est réservé aux apps freestanding.
- **Deux tas cohabitent.** Les objets wtk sont alloués par `operator new` sur umm
  (`onyxpp.hpp`), les bibliothèques C par `malloc` (newlib). C'est normal : ne pas lier umm une
  seconde fois. Voir `user/Apps/courier/main.cpp` pour l'enchaînement exact des en-têtes.
- `json.hpp`, `tls/onyx_tls.hpp`, `http.hpp` et `ft/fonts.h` sont **header-only** : à inclure dans
  **une seule** unité de compilation quand c'est indiqué.
- `wtk/libwtk.a` contient les codecs d'image : il suffit d'inclure `img/imgload.hpp`.

---

## 6. Architecture conseillée du client

### 6.1. Les threads

```
thread principal (GUI)         thread réseau ("net")              thread assets ("dl", optionnel)
  boucle : kapi_pump_wait(16)    TLS + WebSocket + Engine.IO        HTTPS GET des sprites/décors
  dessine la scène, présente     lit les trames, répond aux ping    → cache → img_load_mem
  envoie les actions → file      kapi_post(on_msg, ...) vers GUI    kapi_post(on_asset, ...)
```

- **La GUI appartient au thread qui pompe** : il est le seul à dessiner, présenter et ouvrir
  des dialogues. Les autres threads lui passent leurs résultats par `kapi_post`, sans verrou sur
  les données de la GUI.
- Pour le sens GUI → réseau : une file protégée par `kapi_mutex_*`, et un réveil du thread réseau
  par `kapi_wake_word` / `kapi_wait_word` (le futex, v68).

### 6.2. Réseau : TLS → WebSocket → Engine.IO → Socket.IO

Le site utilise **socket.io**. À vérifier dans le code de Lucas : version, path, transport, auth.
Pile à écrire, du bas vers le haut :

1. **TCP + TLS** : `kapi_tcp_connect (host, 443)` puis `onyx_tls::start (s, sock, host,
   START_VERIFY, &vr)`, avec `set_ca_bundle` lu depuis `SD:/res/ca-bundle` (fourni avec Jet).
   **Vérifier le certificat** : le client transporte un mot de passe ou un jeton.
2. **WebSocket (RFC 6455, côté client)** :
   - handshake HTTP `GET /socket.io/?EIO=4&transport=websocket` avec `Upgrade`,
     `Sec-WebSocket-Key` (16 octets de `kapi_random`, en base64) et `Origin` si le serveur le vérifie ;
   - réponse 101, contrôle de `Sec-WebSocket-Accept` (SHA-1 + base64 : mbedTLS a `mbedtls_sha1`) ;
   - trames : **masquées à l'envoi** (obligatoire côté client), réassemblage des fragments, ping →
     pong, close. `permessage-deflate` est inutile si on ne le propose pas.
3. **Engine.IO v4** (dans chaque message texte WS, le 1er caractère donne le type) :
   - `0{json}` *open* : `sid`, `pingInterval`, `pingTimeout` ;
   - `2` *ping* venu du serveur → répondre **`3`** *pong*. En EIO4, c'est le serveur qui pinge ;
   - `4…` *message* : porte un paquet Socket.IO ; `1` *close*.
4. **Socket.IO v5** (dans un `4`) : `40` (connect au namespace `/`, avec un `{auth}` éventuel) →
   `40{"sid":…}`, puis les événements `42["nom",{…}]`, les acks `43<id>[…]`, les erreurs `44{…}`. Les
   paquets binaires (`45…`), s'il y en a, arrivent en trames WS binaires séparées.
5. **Reconnexion** : backoff de 0,5 à 8 s. Sur une perte Wi-Fi, `kapi_tcp_recv` renvoie une erreur
   : on reconnecte et on rejoue l'entrée dans la salle.

> ⚠ `kapi_tcp_recv` est **non bloquant**. `onyx_tls` attend avec `kapi_msleep` entre deux
> tentatives : c'est voulu, dans un thread à part. `kapi_tcp_send` renvoie le nombre d'octets mis
> en file : sur un compte partiel (timeout de 5 s), envoyer le reste (docs/03 §13).

### 6.3. Rendu 2.5D

- **Décor** : l'image de fond de la salle est décodée une fois (`img_load_mem`) dans un
  `wtk::Canvas`, puis copiée à chaque frame. Mieux : ne recopier que les **rectangles salis**.
- **Sprites** (avatars, meubles) : décodés en `0xAARRGGBB`, puis **triés par profondeur** à
  chaque frame selon la règle du jeu (souvent `y` du pied, ou la case iso). Le dessin se fait avec
  l'alpha. Découpage des planches, directions et frames d'animation : selon le format de TAATU.
- **Coordonnées** : conversion écran ↔ monde (iso ou non) selon la règle du client web, au pixel
  près. Le **hit-test** au clic doit être pixel-perfect sur l'alpha du sprite, comme sur le web.
- **Texte** : pseudos et bulles de chat avec `fnt::draw` (FreeType). La saisie du chat passe par
  un champ wtk ou une ligne maison en bas de la fenêtre.
- **Taille de fenêtre** : le simulateur PC n'accepte pas plus de **1024 × 768**. Sur le Pi, la
  fenêtre peut être plus grande, ou passer en plein écran (`kapi_fullscreen_begin`). Si le jeu est
  conçu pour une taille fixe, créer la fenêtre **à la plus grande taille** proposée, à cause du
  piège du pitch (`kapi_resize_window`, docs/03 §13).
- **Cadence** : 30 à 60 images/s avec `kapi_pump_wait (16)`. Le Pi 4 tient sans peine un décor
  plein écran plus quelques dizaines de sprites en C++ entier/flottant ; garder les décodages
  d'images hors de la boucle.

### 6.4. Assets : téléchargement + cache

- **Cache rapide** : `RAM:/taatu/` (perdu au redémarrage, sans usure de la carte ; vérifier
  l'espace libre avec `kapi_vol_info`).
- **Cache persistant** : `SD:/apps/taatu.app/cache/`. Les écritures sur carte figent le cœur 0
  un instant : écrire en lot, pas à chaque frame.
- Invalidation par **ETag / version** fournie par le serveur, ou par un manifeste
  (`{fichier: hash}`) téléchargé au lancement.
- **Chiffrement (option)** : AES-GCM (`mbedtls_gcm_*`), clé dérivée et embarquée, un nonce par
  fichier. **Le Cortex-A72 du Pi 4 n'a pas les extensions crypto ARMv8** : mbedTLS est déjà
  configuré en logiciel pur (`user/tls/Makefile`), donc ne pas activer AESCE.

### 6.5. Compte et identifiants

- Login par HTTPS (ou par l'événement socket.io prévu). **Ne jamais stocker le mot de passe en
  clair.** Garder un **jeton** de session dans `SD:/apps/taatu.app/config.ini`, et seulement si
  l'utilisateur coche « se souvenir ».
- Suivre les CGU de TAATU : pas de multi-compte côté client, etc.

---

## 7. Tester sur le PC (sans Pi)

- **Simulateur de bureau** : `tools/tests/desktop_sim/` compile une app wtk **pour le PC** contre un
  faux noyau (`fakekapi.cpp`) et la pilote par un script d'événements (clics, touches). Il produit
  une capture. Avec **`SIM_REALNET=1`**, les sockets TCP sont celles du PC : on peut donc se
  connecter au vrai serveur TAATU ou à un serveur local. `SIM_SLEEP=1` fait arriver les réponses
  en temps réel. Voir `shots.sh` et la doc docs/03 §9 « Screenshots », puis adapter le principe
  pour une app hors dépôt (mêmes fichiers `fakekapi.cpp`, `imgstub.cpp`, `user/wtk/*.cpp`).
- **Le vrai ELF du Pi sur PC** : `sh tools/tests/desktop_sim/elfrun.sh <app>` exécute un
  `user/<app>.elf` sous `qemu-aarch64` avec la kapi simulée. Le script cherche l'ELF dans
  `user/` : y copier `taatu.elf`, ou adapter le chemin.
- **Tester la couche protocole seule** : la compiler aussi pour le PC (le code WS/Engine.IO/
  Socket.IO ne dépend que d'un « transport » `send`/`recv`) et la confronter à un serveur
  socket.io local. Ce sont les tests les plus rentables.

## 8. Tester sur le Pi

1. Une carte SD Onyx complète : `cd kernel && make && make stage`, puis copier **tout**
   `sdcard/` sur une carte FAT32. Ou partir de la carte de Stéphane.
2. Copier `taatu.app/` dans `SD:/apps/`. L'app apparaît dans le menu **Onyx**, sous sa
   catégorie.
3. Pour déployer sans retirer la carte : telnet (port 23) et FTP. Voir docs/HANDOFF.md « Trying it
   on the Pi ». Bureau à distance : VNC (port 5900).
4. En cas de crash, le rapport donne l'`ELR`. Lancer `aarch64-none-elf-addr2line -e taatu.elf
   <ELR>`, en gardant l'ELF non strippé.

---

## 9. Licences : ce que le binaire fermé peut lier

| Composant | Licence | Utilisable dans un binaire fermé ? |
|---|---|---|
| Code Onyx (`user/*.h`, `wtk`, `json.hpp`, `http.hpp`, `onyx_tls.hpp`, `libc/`, `user.ld`, `crt0*`) | pas de fichier de licence (tous droits réservés) | **oui, avec l'accord de Stéphane** (accordé pour ce portage) |
| newlib (libc/libm de la toolchain) | BSD-like | oui |
| mbedTLS 3.6.3 | Apache-2.0 | oui (garder la mention) |
| FreeType 2.14.3 | FTL | oui, **mention obligatoire** dans la doc ou l'écran « À propos » |
| `stb_image.h` | domaine public / MIT | oui |
| `simplewebp.h` | BSD-3 | oui (mention) |
| NetSurf, `user/netsurf/*` (dont `onyx_ws.c`) | **GPL** | **non**, ne pas lier ni copier |
| Circle | GPL-3 | non concerné (côté noyau) |

---

## 10. Si Lucas fournit plutôt une spec (variante)

Si c'est Stéphane qui écrit le client, la spec (un `.md` privé, ou `docs/taatu-protocol.md` si
Lucas accepte qu'elle soit publique) devrait couvrir :

- **Connexion** : URL, path socket.io, version EIO, transport, en-têtes ou `Origin` exigés, auth
  (`{auth}` du connect, cookie, jeton) et procédure de login.
- **Événements** : pour chaque événement, nom, sens (C→S / S→C), schéma JSON, exemple réel.
  Au minimum : liste des salles, entrer dans une salle, état initial de la salle (décor, objets,
  avatars), un avatar entre / sort / se déplace / parle, ses propres déplacements, le chat, les
  erreurs et les kicks.
- **Monde** : repère de coordonnées (iso ? taille de case ?), règle de tri en profondeur, zones
  marchables, chemin calculé côté serveur ou côté client.
- **Assets** : URLs, format des planches (découpage, directions, frames, offsets d'ancrage),
  composition des avatars (couches de vêtements ?), versionnement et cache.
- **Son** : formats et URLs (radio : flux MP3 ? Icecast ?).
- **Contraintes** : rate limits, anti-triche, ce qu'un client tiers ne doit **pas** faire.

---

## 11. Checklist « prêt »

- [ ] Se connecte en TLS **avec vérification du certificat**.
- [ ] Login, entrée dans une salle, reconnexion automatique après une coupure Wi-Fi.
- [ ] Décor + sprites dans le bon ordre de profondeur ; clic pour se déplacer ; chat.
- [ ] Assets téléchargés et mis en cache ; aucun asset ni code TAATU dans le dépôt Onyx public.
- [ ] Aucune bibliothèque GPL liée ; mentions FreeType / mbedTLS présentes.
- [ ] `app.txt` + `icon.bmp` ; teste `KT->version` (≥ 71 conseillé) et affiche un message clair
      sinon.
- [ ] Fonctionne sur le Pi 4 (fenêtré et plein écran) et dans le simulateur PC.
- [ ] Une ligne pour Stéphane : nom de l'app et dossiers lus/écrits (`SD:/apps/taatu.app/…`,
      `RAM:/taatu/`), pour l'ajouter au catalogue de `docs/04-USER-GUIDE.md`.
