# Idées — Onyx

Liste vivante des idées de fonctionnalités et d'applications pour Onyx (OS custom sur Circle).

Statuts : 💡 idée · 🔨 en cours · ✅ fait · ❄️ reporté

---

## Applications

| Idée | Statut | Notes |
|------|--------|-------|
| Cardfile — fiches/formulaires (façon Cardfile Win 3.11) | 💡 | Mode **édition** : définir le formulaire (liste de champs modifiable). Mode **visualisation** : CRUD sur les enregistrements (créer/modifier/lire/supprimer). Stockage des schémas + données sur SD. |
| Serveur HTTP simple | 💡 | App graphique : on indique le **home directory** à servir, le reste tourne en tâche de fond. Dépend de la pile réseau exposée. **Réf. (meilleure) :** `circle/sample/21-webserver` (à déporter en app) ; aussi `temp/https.asm` (x86, MenuetOS). |
| Client HTTP | ✅ | Fetch via `/bin/wget <url>` (corps → stdout) sur la lib `user/httpc.h`. HTTP/1.0 + `Connection: close`, pas de chunked, **pas de HTTPS** (TLS absent). Le rendu HTML reste à faire (cf. *Navigateur web*). |
| Interpréteur BASIC (façon QuickBasic) | 💡 | Éditeur : fenêtre où on écrit le code ; le bloc **sans nom = `main`** (comme en QuickBasic) ; un **dialogue pour sélectionner la SUB / FUNCTION à éditer** (chaque procédure s'édite séparément). Le code est **compilé en bytecode en mémoire** puis exécuté par une **VM**. Langage pas forcément BASIC mais similaire → doit couvrir les fonctions de QBasic. |
| Breakout (jeu) | 💡 | Casse-briques classique (raquette, balle, briques, niveaux). |
| Sokoban : niveaux supplémentaires | 💡 | Ajouter ≥ 10 niveaux pour commencer. App existante : `user/sokoban.c`. |
| Navigateur web | 💡 | Embryon de browser : barre d'URL, fetch via la classe `HttpClient`, rendu HTML basique. Réutiliser le parseur `htmlscanner.cpp` (`circle/sample/31-webclient`) comme point de départ. ⚠️ HTTP seul au début (pas de HTTPS natif). |
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
| Port audio : synth FM + tracker | ❄️ | Amener le synth FM + tracker dans Onyx. Fixed-point + PWM. |
| Filer : associations de fichiers via ini | ❄️ | `[fileassoc] .ext=programme` (aujourd'hui hardcodé dans `open_file`). |
| Filer : copier/couper → coller (presse-papiers fichier) | 💡 | Copier/couper mémorise le path ; coller ailleurs fait copy (copier) ou move (couper). |

---

## Idées brutes (à trier)

_Notes en vrac, à classer plus tard._
