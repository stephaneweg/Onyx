# Idées — Onyx

Liste vivante des idées de fonctionnalités et d'applications pour Onyx (OS custom sur Circle).

Statuts : 💡 idée · 🔨 en cours · ✅ fait · ❄️ reporté

---

## Applications

| Idée | Statut | Notes |
|------|--------|-------|
| Cardfile — fiches/formulaires (façon Cardfile Win 3.11) | 💡 | Mode **édition** : définir le formulaire (liste de champs modifiable). Mode **visualisation** : CRUD sur les enregistrements (créer/modifier/lire/supprimer). Stockage des schémas + données sur SD. Interopère avec la *mini-suite bureautique* (cf. ci-dessous). |
| Mini-suite bureautique (texte + tableur) | 💡 | Un **traitement de texte** + un **tableur** légers, avec **interopérabilité** entre eux et avec le **Cardfile** : ex. publipostage (fiches Cardfile → document texte), import/export de plages tableur ↔ enregistrements Cardfile, copier des données d'une app à l'autre. Formats de fichiers simples et communs sur SD. |
| Serveur HTTP simple | 💡 | App graphique : on indique le **home directory** à servir, le reste tourne en tâche de fond. Dépend de la pile réseau exposée. **Réf. (meilleure) :** `circle/sample/21-webserver` (à déporter en app) ; aussi `temp/https.asm` (x86, MenuetOS). |
| Client HTTP | ✅ | Fetch via `/bin/wget <url>` (corps → stdout) sur la lib `user/httpc.h`. HTTP/1.0 + `Connection: close`, pas de chunked, **pas de HTTPS** (TLS absent). Le rendu HTML reste à faire (cf. *Navigateur web*). |
| Interpréteur BASIC (façon QuickBasic) | 💡 | Éditeur : fenêtre où on écrit le code ; le bloc **sans nom = `main`** (comme en QuickBasic) ; un **dialogue pour sélectionner la SUB / FUNCTION à éditer** (chaque procédure s'édite séparément). Le code est **compilé en bytecode en mémoire** puis exécuté par une **VM**. Langage pas forcément BASIC mais similaire → doit couvrir les fonctions de QBasic. |
| Visualiseur Télétexte (façon Ceefax/Minitel) | 💡 | **Pas de réception hertzienne** (VBI analogique = éteint partout ; hors de portée du RPi4 seul). À la place : **moteur de rendu télétexte** — grille 40×25, 7 couleurs, mosaïques **G1** (blocs 2×3), double-hauteur/flash. **Sources de données :** (a) **vraies pages de chaînes via API JSON** — NOS Teletekst NL (`teletekst-data.nos.nl/json/{page}`, 100–999), SVT/texttv.nu SE (`api.texttv.nu/api/get/{page}`), aussi Yle/ARD/ZDF/ORF ; (b) `.tti`/JSON sur SD ; (c) **RSS→télétexte** via `HttpClient` (cf. Ceefax Simulator / CEETEX). ⚠️ Les API des chaînes sont en **HTTPS** → besoin TLS (circle-stdlib, différé), d'un endpoint HTTP, ou d'un **proxy HTTP sur le LAN**. Recoupe l'esthétique CP437/BBS ; le Minitel/Videotex interactif passe souvent par telnet (cf. *Client BBS*). |
| Breakout (jeu) | 💡 | Casse-briques classique (raquette, balle, briques, niveaux). |
| Sokoban : niveaux supplémentaires | 💡 | Ajouter ≥ 10 niveaux pour commencer. App existante : `user/sokoban.c`. |
| Émulateurs de consoles | 💡 | Porter NES, SNES, GBA, NDS, 3DS… Réalisme **décroissant** sur RPi4 bare-metal : NES/Game Boy très faisable, SNES jouable, GBA correct, **NDS lourd**, **3DS hors de portée** (GPU/JIT). Beaucoup de cœurs open-source réutilisables (style libretro). FP matériel dispo. |
| Moteurs Doom & Quake (id Tech) | 💡 | Sources **GPL** d'id Software. **Doom** (id Tech 1) : rendu logiciel → framebuffer, très portable, candidat idéal. **Quake** (id Tech 2, float, soft-render) : jouable. **Quake III Arena** (id Tech 3) : ⚠️ exige **OpenGL** → pas de stack GL sur Onyx aujourd'hui (gros prérequis). |
| Portages depuis itch.io — jeux open-source | 💡 | Sélection de jeux livrés **avec code source** sur itch.io (grosse scène rétro). Trier par portabilité : C/C++/SDL sans gros runtime. |
| Portages depuis itch.io — outils open-source | 💡 | Idem pour des **outils** (création de jeux, musique, pixel-art) dispo en source sur itch.io. |
| Pixel Vision 8 (fantasy console) | 💡 | Partir du **code source de PV8** : d'abord son **éditeur de musique**, puis viser le **runtime complet** (éditeurs de ressources/jeu, scripting **Lua**). ⚠️ Le moteur PV8 est **C#/MonoGame** → portage .NET bare-metal irréaliste ; voie réaliste = **réimplémenter le concept nativement** en se servant des sources PV8 comme référence de design. **Lua déjà porté sur Circle** (réutilisable pour le scripting). |
| Navigateur web — **port de NetSurf** | 🔨 | **En cours — on assemble les briques.** Décision : porter **NetSurf** (moteur de rendu léger, pensé pour les systèmes contraints) plutôt qu'écrire un rendu maison. Fetch déjà OK via `HttpClient`/`wget`. À prévoir : prérequis du port (libs, FPU, alloc, fonts), front-end framebuffer maison branché sur uikit. ⚠️ HTTP seul au début (pas de HTTPS natif). |
| Client BBS (telnet) | 💡 | Comme l'IRC côté réseau (socket TCP, ABI v21) — les BBS existent encore via **Telnet** (port 23). Le vrai travail = **émulation terminal** : séquences **ANSI** (couleurs, curseur) + charset **CP437** (art ASCII / box-drawing). Réutiliser l'app terminal existante. Option ultérieure : transferts X/Y/ZMODEM. **Favoris par défaut** (vérifiés actifs, 2025) : `vert.synchro.net:23` (Vertrauen — QG de Synchronet), `xibalba.l33t.codes:44510` (Xibalba — QG d'ENiGMA½), `ciaamiga.org:6400` (CIA Amiga), `digitalx.ddns.net:7411` (Digital-X). Annuaire : telnetbbsguide.com. |
| Client IRC | ✅ | App GUI `user/irc.c` → `apps/irc.app` : connexion serveur/canal (config.ini), scrollback + saisie, PING/PONG, PRIVMSG/NOTICE/CTCP, commandes `/join /part /nick /msg /me /raw /quit`. Sur les sockets TCP de l'ABI v21. |

## Fonctionnalités (système / kernel)

| Idée | Statut | Notes |
|------|--------|-------|
| IPC : mécanisme de communication inter-processus | 💡 | Brique de base. À terme : déporter le serveur graphique (compositeur, gestion fenêtres, souris, clavier) dans un exécutable séparé. |
| Réseau : exposer la pile TCP/IP de Circle | ✅ | Fait (phase 1, cœur principal) : WLAN `bcm4343` + `wpa_supplicant` + `CNetSubSystem` (DHCP) montés dans un `CNetBringupTask` non bloquant ; sockets TCP via l'ABI v21 (`net_status`/`tcp_connect`/`tcp_send`/`tcp_recv`/`tcp_close`), backend `kernel/sys/net.cpp`. **Reste :** faire tourner la pile sur un **cœur dédié** (phase 2 : `CMultiCoreSupport` + files inter-cœurs). |
| Réseau : résolveurs ARP / DNS, etc. | ✅ | Fournis par la pile Circle : ARP/ICMP internes ; DNS via `CDNSClient` (utilisé par `tcp_connect` quand l'hôte est un nom). |
| Réseau : synchro date/heure via NTP | ✅ | `CNTPDaemon` démarré dès que le lien est up ; `system.ini` `timezone=` (minutes UTC) + `ntp=`. **Réf. :** `circle/sample/18-ntptime`. |
| Classe `HttpClient` (lib) | ✅ | `user/httpc.h` : header-only, sans allocation (buffer fourni par l'appelant), sur les sockets TCP v21. `http_request(method,url,headers,body,…)` (GET/POST/PUT/… via le verbe) + raccourcis `http_get`/`http_post`. HTTP/1.0, pas de HTTPS. Socle du futur navigateur web. |
| Floating point pour les tâches | 💡 | Explorer l'activation du **FPU/SIMD (NEON)** côté user. Implique d'**adapter la sauvegarde/restauration du contexte des tâches** (registres `V0–V31` + `FPCR`/`FPSR`) lors des changements de contexte. À évaluer : coût perf vs lazy-save. |
| Compositor : dirty-rect + flush async | 💡 | Aujourd'hui : redraw plein écran chaque frame. Optimisation perf différée. |
| Stack OpenGL / GLES (VideoCore) | 💡 | Exposer une **accélération 3D** via le GPU **VideoCore VI** (V3D) du RPi4. Prérequis de plusieurs apps : *Quake III*, émulateurs 3D, futur rendu accéléré. Piste : addon `circle/addon/vc4` (+ Mesa/V3D). Gros morceau ; sans lui, ces apps restent en soft-render (ou hors de portée). |
| Compositor : blits par DMA 2D | 💡 | Évaluer l'usage du **DMA en mode 2D** (transferts avec stride source/dest, BCM2711) pour blitter les **fenêtres → bureau** et les **conteneurs → leur fenêtre**, au lieu de copies CPU. Décharge le CPU sur les grandes surfaces. Voir le contrôleur DMA de Circle (`CDMAChannel`, mode 2D). Complémentaire du dirty-rect. |
| Port audio : synth FM + tracker | ❄️ | Amener le synth FM + tracker dans Onyx. Fixed-point + PWM. |
| Filer : associations de fichiers via ini | ❄️ | `[fileassoc] .ext=programme` (aujourd'hui hardcodé dans `open_file`). |
| Filer : copier/couper → coller (presse-papiers fichier) | 💡 | Copier/couper mémorise le path ; coller ailleurs fait copy (copier) ou move (couper). |

---

## Idées brutes (à trier)

_Notes en vrac, à classer plus tard._

---

## Plan d'action (session 2026-09-25)

Ordre = dépendances. Chaque phase est livrée (commit + staging SD) avant la suivante.

| # | Phase | Contenu | Statut |
|---|-------|---------|--------|
| P1 | **Fondations système** ✅ (à valider sur le Pi) | IPC par **services nommés** (`ipc_register`/`ipc_lookup`, messages 512 o) ; **presse-papiers** (kapi, texte + chemins de fichiers, Edit ▸ Copy/Cut/Paste dans tinypad / Writer / File Viewer) ; **notifications** : `notify()` (lib user) → IPC → app `notifyd` (bulle sous la barre de menu, **fondu** via une opacité de fenêtre gérée par le compositeur) ; **fin de session** (menu Onyx ▸ Shut Down… : redémarrer / arrêter). | 🔨 |
| P2 | **Corbeille** ✅ (à valider sur le Pi) | Lib user `trash.h` (déplacement vers `SD:/.Trash/` + fichier d'info = chemin d'origine) ; File Viewer : Supprimer → corbeille, vue Corbeille avec **Restaurer** / **Vider**. (`/bin/rm` reste une vraie suppression.) | 💡 |
| P2b | **Apps plein écran** ✅ (à valider sur le Pi ; démo `plasma`) | `fullscreen_begin` : l'app reçoit un tampon plein écran mappé chez elle, `present_fb` l'affiche ; pendant ce temps le compositeur ne dessine plus les fenêtres ; `fullscreen_end` / sortie de l'app rend le bureau. | 💡 |
| P3 | **Glisser-déposer + Shelf** ✅ (à valider sur le Pi ; ABI v42) | Session de drag gérée par le noyau (source → cible sous le curseur, Ctrl = copier) ; **Shelf** en bas de l'écran (onglets, recouvrable par les fenêtres) ; File Viewer source/cible ; **associations de fichiers** (`/etc/fileassoc.ini`) ; message « ouvrir ce fichier » aux apps (qui proposent d'enregistrer le document en cours) ; icône Corbeille sur le shelf. | 🔨 |
| P4 | **Visionneuse d'images** ✅ (à valider sur le Pi) | App `imageview` (BMP, GIF animé, PCX, JPEG, PNG, WebP : `user/img/imgload.hpp` = stb_image + simplewebp + décodeur PCX maison) ouverte depuis le File Viewer (`fileassoc.ini`), aperçu de tous ces formats dans le File Viewer. | 🔨 |
| P5 | **Widgets wtk (façon WPF)** ✅ (à valider sur le Pi ; démo `widgets`) | RadioButton, GroupBox, ListBox, NumericUpDown, ToggleSwitch, Calendar / DatePicker, ColorPicker (dialogue), TreeView, ToolTip, ImageBox. | 🔨 |
| P6 | **Réseau** ✅ (à valider sur le Pi : `ping`, `nslookup`, `netstat`, `whois` — ABI v43 ; serveur **`ftpd`** ; client **FTP/FTPS dans le VFS** `ftpfs` — ABI v44) | **Serveur FTP** (déploiement à chaud), multi-utilisateurs sans fichier de config : `ftpd [homedir] [user] [password]` ; si une instance tourne déjà, la nouvelle informe seulement l'instance principale (IPC) « tel user, tel mot de passe, tel dossier racine » puis quitte. **Client FTP intégré au VFS** (un préfixe de chemin type `FTP:hôte/…` routé vers un client FTP, pour que File Viewer & co y naviguent comme sur la carte). `/bin/ping` (Circle répond déjà aux pings), `nslookup`, `netstat`, `whois`. | 💡 |
| P7 | **Apps utiles** | Calculatrice graphique, éditeur d'icônes, lecteur RTF. | 💡 |
| P7b | **Lisa (assistant IA)** | Un « Eliza » moderne : app de chat qui appelle l'**API REST de Groq** (HTTPS via mbedTLS, comme `httpsget`) ; la **clé d'API** dans un fichier `.ini` (ex. `SD:/apps/lisa.app/config.ini`, jamais committée) ; l'agent est **contextualisé** : un rôle (prompt système) + tout l'historique du chat envoyé à chaque requête. | 💡 |
| P8 | **Jeux** | Solitaire / FreeCell, Pipes, Arkanoid, Invaders (Minesweeper existe déjà). | 💡 |
| P9 | **Programmation + VM** | **D'abord un BASIC façon QBasic** : éditeur où le programme principal et chaque SUB / FUNCTION sont édités séparément (un seul à la fois dans la fenêtre), navigation par un dialogue listant les SUB/FUNCTION, création d'un nouveau SUB/FUNCTION par le menu ; le langage fournit des fonctions pour **wtk** (fenêtres, contrôles, événements) et les **apps système** (notify, presse-papiers, fichiers, glisser-déposer…) ; exécution **compilée en bytecode** pour une **VM** (la fondation VM ci-dessous, partagée). Puis Port de **Lua** (newlib) ; cadre commun de « machines virtuelles » (bytecode ou ROM : affichage, entrées, son, horloge) → CHIP-8, uxn/Varvara ; outil didactique (Lua + tortue). | 💡 |
