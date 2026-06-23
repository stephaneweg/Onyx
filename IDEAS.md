# Idées — Zircon

Liste vivante des idées de fonctionnalités et d'applications pour Zircon (OS custom sur Circle).

Statuts : 💡 idée · 🔨 en cours · ✅ fait · ❄️ reporté

---

## Applications

| Idée | Statut | Notes |
|------|--------|-------|
| Cardfile — fiches/formulaires (façon Cardfile Win 3.11) | 💡 | Mode **édition** : définir le formulaire (liste de champs modifiable). Mode **visualisation** : CRUD sur les enregistrements (créer/modifier/lire/supprimer). Stockage des schémas + données sur SD. |
| Serveur HTTP simple | 💡 | App graphique : on indique le **home directory** à servir, le reste tourne en tâche de fond. Dépend de la pile réseau exposée. **Réf. (meilleure) :** `circle/sample/21-webserver` (à déporter en app) ; aussi `temp/https.asm` (x86, MenuetOS). |
| Client HTTP | 💡 | Récupération de ressources HTTP (download/fetch). Dépend de la pile réseau exposée. **Réf. (meilleure) :** `circle/sample/31-webclient` — fetch d'un doc HTTP + parseur HTML (`htmlscanner.cpp`) ; aussi `temp/httpc.asm` (x86, MenuetOS). ⚠️ Circle ne gère pas HTTPS nativement (SSL/TLS → circle-stdlib). |
| Breakout (jeu) | 💡 | Casse-briques classique (raquette, balle, briques, niveaux). |
| Sokoban : niveaux supplémentaires | 💡 | Ajouter ≥ 10 niveaux pour commencer. App existante : `user/sokoban.c`. |
| Client IRC | 💡 | Connexion à un serveur IRC, channels, chat. Dépend de la pile réseau exposée. **Réf. :** `temp/board.asm` (x86, issu de MenuetOS) comme implémentation de référence. |

## Fonctionnalités (système / kernel)

| Idée | Statut | Notes |
|------|--------|-------|
| IPC : mécanisme de communication inter-processus | 💡 | Brique de base. À terme : déporter le serveur graphique (compositeur, gestion fenêtres, souris, clavier) dans un exécutable séparé. |
| Réseau : exposer la pile TCP/IP de Circle | 💡 | Surfacer la stack réseau de Circle via l'ABI/kapi (sockets pour les apps). Prérequis des autres idées réseau. **Transport :** WiFi d'abord via `circle/addon/wlan` (driver `bcm4343` + `hostap`/wpa_supplicant) ; plus tard, choix WiFi / Ethernet. **Config IP : DHCP par défaut** (`#define USE_DHCP`, `CNetSubSystem` ctor par défaut) ; IP statique seulement si on désactive `USE_DHCP`. |
| Réseau : résolveurs ARP / DNS, etc. | 💡 | Implémenter ARP, DNS (et autres protocoles utiles) au-dessus de la pile exposée. |
| Réseau : synchro date/heure via NTP | 💡 | Une fois la couche réseau exposée, récupérer date & heure. **Réf. :** `circle/sample/18-ntptime`. |
| Compositor : dirty-rect + flush async | 💡 | Aujourd'hui : redraw plein écran chaque frame. Optimisation perf différée. |
| Port audio : synth FM + tracker | ❄️ | Amener le synth FM + tracker dans Zircon. Fixed-point + PWM. |
| Filer : associations de fichiers via ini | ❄️ | `[fileassoc] .ext=programme` (aujourd'hui hardcodé dans `open_file`). |
| Filer : copier/couper → coller (presse-papiers fichier) | 💡 | Copier/couper mémorise le path ; coller ailleurs fait copy (copier) ou move (couper). |

---

## Idées brutes (à trier)

_Notes en vrac, à classer plus tard._
