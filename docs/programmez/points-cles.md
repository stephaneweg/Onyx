# Onyx : les points clés (pour Programmez!)

*Chiffres relevés dans le dépôt le 27 octobre 2026.*

## Le temps

- **Premier commit le 22 juin 2026.** Le projet a eu deux périodes de travail : une semaine en juin
  (du 22 au 28, 170 commits : le noyau multi-processus), une pause de trois mois, puis une reprise
  continue depuis le 25 septembre.
- **32 jours actifs au total** (des jours avec au moins un commit), pour **2 098 commits**.
- Le rythme de la reprise : en moyenne une soixantaine de commits par jour actif, avec une pointe à 233
  commits le 1er octobre.
- Reconstruire tout le système depuis un clone neuf (`tools/build-sdcard.sh`) prend **5 minutes** sur
  4 cœurs. Le navigateur Jet (WebKit), lui, demande **1 h 56** (`tools/build-jet.sh`).

## Les tokens et le coût

- **Tokens mesurés sur ce PC** : ce sont les transcriptions de Claude Code des 13 derniers jours actifs
  (du 7 au 27 octobre ; les plus anciennes ne sont plus sur le disque). Cela représente 13 sessions et
  5 222 réponses du modèle (Claude Opus) :
  - **3,8 millions de tokens générés** (le code, les documents, les réponses) ;
  - **81 millions de tokens de contexte écrits en cache**, puis **2,3 milliards relus depuis le cache**.
    L'essentiel du volume vient de là : à chaque étape, le modèle relit le contexte de la session (le
    code ouvert, l'historique de la conversation). Le cache rend cette relecture beaucoup moins chère
    qu'une lecture neuve.
  - Le total du projet est plus élevé : ce relevé ne couvre ni septembre et le début d'octobre, ni les
    sessions lancées dans le cloud ou sous WSL.
- **Le coût réel est celui de l'abonnement**, pas un prix au token. Au départ, il s'agissait de
  l'abonnement Max à 100 € par mois. Le quota hebdomadaire a été épuisé une fois, et plutôt que d'attendre
  trois jours, l'auteur est passé à la formule supérieure (Max 20x, environ 200 $ par mois ; les prix sont
  à vérifier sur claude.com/pricing). À la facturation à l'usage de l'API, le même volume coûterait
  nettement plus cher : c'est tout l'intérêt de l'abonnement pour un projet de cette taille.

## La taille

| | |
|---|---|
| Image du noyau | **1,3 Mo**, Circle compris : `kernel8-rpi4.img` (Pi 4, Pi 400), `kernel_2712.img` (Pi 5, Pi 500 ; testé et validé) |
| Cartes Pi 5 | une distribution à part : `sdcard5/` (388 Mo) et `sdcard5_lite/` |
| Serveurs graphiques | Elegant 162 Ko, PocketUI 230 Ko |
| Les kits (bibliothèques partagées) | de 90 Ko (AppKit, NetKit) à 800 Ko (AudioKit) ; UIKit 590 Ko |
| Carte complète (`sdcard/`) | **392 Mo** : le système, plus de 100 applications, les polices, les exemples |
| Carte minimale (`sdcard_lite/`) | **127 Mo** : le reste s'installe par le gestionnaire de paquets |
| Jet, le navigateur WebKit | un binaire d'environ 105 Mo, installé par paquet |
| Code écrit pour Onyx | environ **43 000 lignes** pour le noyau et **384 000** pour l'espace utilisateur (C, C++, assembleur), plus 67 000 pour les outils sur PC ; sans compter les bibliothèques tierces (WebKit, FFmpeg, MuPDF, FreeType…) |
| Documentation | 64 documents Markdown dans `docs/` |

Il n'y a pas d'ISO : Onyx s'installe en copiant le dossier `sdcard/` sur une carte microSD formatée en
FAT32.

## Les performances face à Raspberry Pi OS

### Mesuré sur Onyx (Pi 4, 8 Go, bureau en 1280 × 1024, carte microSD, 27 octobre)

- **Démarrage** : **33 s** entre la commande `reboot` et le bureau complet (barre de menus, dock, Wi-Fi
  connecté, accès à distance ouvert dès 31 s). En mode pocket (640 × 480) : **31 s** jusqu'au lanceur.
  Ce temps comprend le firmware du Pi ; il s'agit d'un essai par mode, en redémarrage et non en mise sous
  tension. Le premier démarrage après un changement de résolution a pris 57 s : à écarter.
- **Lancement des applications**, du lancement à l'app prête, la mémoire du processus stable (trois
  essais, depuis la carte SD, sans préchargement) :

| Application | À vide | Avec un document | Mémoire |
|---|---|---|---|
| Letters (traitement de texte) | **0,3 s** | **0,3 s** (`letters-tour.rtf`, avec images) | 8 à 12 Mo |
| Sheet (tableur) | **0,8 s** | **0,8 s** (`cafe-2026.xlsx`, 3 feuilles, un graphique) | 9 à 11 Mo |
| Koton (le studio de musique) | **2,9 s** | — | 87 Mo (les sons) |

  Ces temps comprennent l'aller-retour réseau de la commande (environ 0,2 s). Le relevé se fait toutes
  les 0,2 s environ, d'où une précision de l'ordre du quart de seconde. Lire Letters depuis la carte
  prend 31 ms au noyau (576 Ko) : les kits qu'elle utilise sont déjà en mémoire, partagés avec le bureau.

### Une comparaison avec Raspberry Pi OS (à faire)

Il n'existe pas encore de comparaison mesurée sur le même Pi. Voici un protocole proposé, avec la même
machine et deux cartes :

1. **Démarrage** : chronométrer de la mise sous tension au bureau utilisable (dix essais, la médiane).
2. **Lancement d'une app** : le traitement de texte et le tableur (Letters / Sheet face à LibreOffice
   Writer / Calc), une première fois puis une seconde fois (préchargé ou en cache).
3. **Mémoire** au repos, une fois le bureau prêt.
4. **Le navigateur** : le temps de chargement d'une même page (programmez.com) dans Jet et dans Chromium.

Cette comparaison n'est pas tout à fait équitable : Raspberry Pi OS est un système généraliste, bien plus
complet. Elle montre ce que coûte cette généralité.

## Le préchargement, en bref

Au démarrage, `/bin/preload` lit la liste `SD:/etc/preload.ini` (les programmes et les kits les plus
utilisés, réglables dans le panneau de configuration). Il demande au noyau de les charger et de les garder
en mémoire, même quand aucun processus ne les utilise. Lancer ensuite l'un de ces programmes ne touche plus
la carte SD : le noyau associe au nouveau processus les pages de code déjà en mémoire (partagées, en
lecture seule) et ne lui donne en propre que ses données. Les kits fonctionnent de la même façon : le code
d'UIKit, par exemple, n'existe qu'en un exemplaire, quel que soit le nombre d'applications ouvertes. Quand
un paquet met à jour un programme préchargé, l'ancienne image est retirée, le fichier remplacé, et la
nouvelle version préchargée à nouveau.

## Les illustrations jointes

- `architecture.png` : le schéma d'architecture (le noyau avec Circle comme couche matérielle, les
  serveurs graphiques, les kits, les apps, l'UIKit propre à chaque serveur, le préchargement).
- `pocket-*.png` : le mode pocket (640 × 480, l'écran d'une console de poche) : le lanceur, le
  traitement de texte, le tableur. La même application, sans recompilation, que sur le bureau.
- `bureau-*.png` : le mode bureau, en 1280 × 1024 sur le Pi : Jet affichant programmez.com
  (`bureau-jet.png`) ; Letters, le tableur et le terminal, où `ps` montre les processus du système
  (`bureau-bureautique.png`) ; 3DForge, le logiciel de CAO, avec une pièce d'exemple (`bureau-3dforge.png`).
