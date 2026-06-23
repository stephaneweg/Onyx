# Zircon — Présentation du projet

> **Note sur le nom.** « Zircon » est un nom de travail (hérité du nom du dossier).
> Ce projet **n'a aucun rapport** avec le micro-noyau *Zircon* de Google/Fuchsia,
> ni avec *LK*. C'est un système d'exploitation maison, écrit à partir de zéro,
> qui s'appuie sur le framework bare-metal **Circle** comme couche d'abstraction
> matérielle.

## 1. En une phrase

Zircon est un **petit système d'exploitation multi-processus** pour **Raspberry Pi 4**
(AArch64), construit **au-dessus de Circle** ([rsta2/circle](https://github.com/rsta2/circle))
qui lui sert de HAL et de pile de pilotes. Il charge des **programmes ELF depuis la carte
SD** et les exécute comme des **applications isolées les unes des autres**, chacune dans
sa propre table de pages. Le tout est piloté par un **bureau graphique** (« Maynard »)
composé d'un compositeur logiciel, d'une barre de tâches, d'un lanceur, d'un terminal,
d'un gestionnaire de fichiers et d'une trentaine d'applications.

Il **tourne sur du vrai matériel Raspberry Pi 4** (pas seulement en émulation).

## 2. Philosophie : « posséder le haut, réutiliser le bas »

Circle est un framework bare-metal *mono-processus, mono-espace d'adressage, coopératif*
(« votre application **est** le système »). Zircon **garde la moitié basse** de Circle
(excellente) et **remplace sa moitié « système d'exploitation »** :

| Couche | Décision | Détail |
|---|---|---|
| Reset → EL1, activation MMU, GIC, constructeurs C++ | **Garder Circle** | Notre point d'entrée est `main()` ; tout ce qui est en dessous est déjà fait. |
| Allocateur de pages 64 Ko (`palloc`/`pfree`) | **Garder** — c'est notre allocateur de frames | `CMemorySystem::Get()`. |
| GIC-400, timer, mailbox, EMMC+FatFs, USB, Ethernet, framebuffer | **Réutiliser** | C'est tout l'intérêt de s'appuyer sur Circle. |
| Ordonnanceur `CScheduler`/`CTask` | **Réécrire l'implémentation, garder l'API** | Voir [internals du noyau](02-INTERNALS-NOYAU.md). |
| Tables de pages par processus, changement de contexte, vecteurs d'exception, espaces d'adressage | **Écrire de zéro** | N'existe pas dans Circle. |

Conséquence pratique : on **lie** le noyau contre les bibliothèques statiques déjà
compilées de Circle (`libcircle.a`, `libfs`, `libusb`, …) plutôt que de recopier ses
sources.

## 3. Caractéristiques principales

- **Isolation par processus via la MMU.** Chaque application a sa propre table de pages
  L2/L3 (granule de 64 Ko), étiquetée par **ASID**. Une application ne peut pas voir la
  mémoire d'une autre.
- **Modèle d'exécution « Option C ».** Les applications tournent en **EL1** (privilégié)
  et appellent directement les fonctions du noyau (pas de trappe `SVC` dans le chemin
  courant). Le compromis : les apps sont isolées **entre elles**, mais **pas du noyau**.
  Voir §5.
- **ABI stable à adresse fixe (`kapi`).** Le noyau publie une **table de pointeurs de
  fonctions** à une adresse virtuelle fixe, mappée en lecture seule dans chaque
  application. Les applications appellent le noyau à travers cette table → **un binaire
  d'application continue de fonctionner sans recompilation** quand le noyau change
  (contrat *append-only*, version d'ABI actuelle : **16**).
- **Bureau graphique complet.** Compositeur logiciel 32 bits, gestionnaire de fenêtres,
  boîte à outils de widgets dessinés par le noyau (boutons, cases à cocher, curseurs,
  zones de texte, barres de défilement, icônes…), fenêtres avec décoration thématisable,
  fond d'écran, curseur souris.
- **Shell Maynard.** Barre/lanceur (panel), liste d'applications, terminal interactif
  avec **pipes et redirections** (`|`, `>`, `>>`, `<`), gestionnaire de fichiers, et une
  collection d'outils en ligne de commande dans `/bin`.
- **Catalogue d'applications.** Éditeur de texte, tableur, calculatrice scientifique,
  logiciel de dessin, explorateur de fractales, calendrier, gestionnaire de tâches,
  éditeur de thème, et de nombreux jeux (Tetris, Snake, 2048, Démineur, Sokoban, Pong,
  Game of Life, SameGame).
- **Périphériques d'entrée USB.** Clavier et souris (HID), avec changement de disposition
  clavier à chaud (US, UK, DE, FR, ES, IT, Dvorak).

## 4. L'architecture en un coup d'œil

```
┌──────────────────────────────────────────────────────────────────┐
│  Applications (ELF EL1, isolées par ASID)                          │
│  panel · applist · terminal · filer · tinypad · jeux · /bin/*      │
│       │  appellent le noyau via kapi.h (wrappers inline)           │
├───────┼──────────────────────────────────────────────────────────┤
│       ▼   Table ABI kapi  (à 14 Go, lecture seule dans chaque app) │   ← contrat stable
├──────────────────────────────────────────────────────────────────┤
│  NOYAU ZIRCON (EL1)                                                │
│   • mm/      espaces d'adressage par processus, MMU, ASID          │
│   • sched/   ordonnanceur coopératif (remplace celui de Circle)    │
│   • arch/    vecteurs d'exception VBAR_EL1, trap frame             │
│   • proc/    chargeur ELF64                                        │
│   • sys/     impl. kapi, flux/stdio, console de debug              │
│   • gui/     GImage (rendu logiciel), compositeur+WM, skins,       │
│              dialogues modaux                                       │
├──────────────────────────────────────────────────────────────────┤
│  CIRCLE  (HAL + pilotes, réutilisé tel quel)                       │
│   palloc · GIC · timer · EMMC+FatFs · USB HID · framebuffer 2D     │
├──────────────────────────────────────────────────────────────────┤
│  Raspberry Pi 4  (BCM2711, 4× Cortex-A72, ARMv8-A)                 │
└──────────────────────────────────────────────────────────────────┘
```

## 5. Le modèle d'exécution « Option C »

C'est la décision d'architecture la plus structurante. Après avoir prototypé des
processus EL0 + appels système (`SVC`), le projet a basculé sur **Option C** :

- Les applications tournent en **EL1** (même niveau de privilège que le noyau), **chacune
  dans sa propre table de pages** étiquetée par ASID.
- Elles **appellent les fonctions du noyau directement** via la table ABI `kapi` — pas de
  trappe système dans le chemin normal.
- **Isolation :** les applications sont isolées **entre elles** (un ASID/TTBR0 distinct
  par app ; une app ne peut pas adresser les pages d'une autre). Le **noyau n'est pas
  protégé** : une app EL1 peut techniquement toucher la mémoire du noyau. C'est le
  compromis assumé en échange de l'ergonomie de l'appel direct.
- La machinerie EL0/`SVC` (vecteurs, dispatch d'appels système) **existe toujours mais est
  inerte** ; elle pourrait resservir si l'on voulait de vraies applications EL0 isolées du
  noyau.

## 6. Ordonnancement : coopératif (sur matériel)

Bien que le noyau ait été conçu au départ pour être **préemptif** (tick à 100 Hz), le
portage sur matériel a montré qu'une **préemption depuis l'IRQ ne fonctionne pas** dans le
modèle de Circle : les threads tournent en **EL1t** (avec `SP_EL0`), tandis que le
gestionnaire d'IRQ tourne en **EL1h** (avec `SP_EL1`). Un changement de contexte depuis
l'IRQ échangerait `SP_EL1`, pas la pile du thread.

→ **L'ordonnancement est donc coopératif.** Les tâches changent quand elles appellent
`yield`, `msleep`, `present`, `wait`, etc. — exactement comme l'ordonnanceur d'origine de
Circle. Les applications graphiques cèdent la main naturellement à chaque image (via
`present`/`msleep`), ce qui donne l'illusion du parallélisme. Détails dans
[Internals du noyau](02-INTERNALS-NOYAU.md).

## 7. Matériel cible

- **Raspberry Pi 4** (BCM2711, 4× Cortex-A72, ARMv8-A) — cible principale.
- Sortie **HDMI** (framebuffer 32 bpp, 1024×768 par défaut, configurable).
- **Clavier + souris USB**.
- **Console série** optionnelle (GPIO14/15, 115200 8N1) pour le journal de démarrage et
  les vidages d'exception.
- Le RPi 5 est différé (E/S derrière la puce propriétaire RP1 via PCIe — bring-up plus
  lourd). Le multi-cœur (SMP) est aussi différé.

## 8. Structure du dépôt

```
ARCHITECTURE.md   conception d'origine + manifeste de build (historique, en partie daté)
README.md         résumé (en partie daté : décrit l'état « 2 démos »)
docs/             CETTE documentation (présentation, internals, guides dev/utilisateur)
kernel/           le noyau Zircon (voir docs/02-INTERNALS-NOYAU.md)
user/             le userland : apps (*.c), runtime (crt0.S, user.ld), /bin tools
sdcard/           fichiers prêts à flasher pour un RPi 4 (firmware, config, apps, /bin)
tools/            scripts hôtes (génération d'icônes BMP)
circle/           clone amont de Circle (non commité ; à cloner séparément)
```

> **Attention aux docs héritées.** `ARCHITECTURE.md`, `README.md`, `kernel/README.md` et
> `sdcard/README.md` décrivent des **états plus anciens** du projet (processus EL0,
> ordonnancement préemptif, 640×480, « deux démos »). Là où ils contredisent la présente
> documentation, **ce sont ces documents-ci (`docs/`) qui font foi** pour l'état actuel.
> `ARCHITECTURE.md` §11–§12 reste néanmoins la meilleure référence pour *pourquoi* Option C
> et l'ordonnancement coopératif ont été retenus.

## 9. Pour aller plus loin

| Document | Pour qui | Contenu |
|---|---|---|
| **[02 — Internals du noyau](02-INTERNALS-NOYAU.md)** | qui veut comprendre le noyau | démarrage, mémoire/MMU, ordonnancement, exceptions, ABI, GUI, flux |
| **[03 — Guide du développeur](03-GUIDE-DEVELOPPEUR.md)** | qui veut bâtir/compiler/étendre | toolchain, build, modèle d'app, étendre l'ABI, conventions, débogage |
| **[04 — Guide de l'utilisateur](04-GUIDE-UTILISATEUR.md)** | qui veut s'en servir | carte SD, bureau, terminal, fichiers, applications, personnalisation |

## 10. Genèse rapide (jalons)

1. Prise de contrôle de `main()` au-dessus du `sysinit` de Circle ; console série.
2. Ordonnanceur de remplacement (en-tête « shadow » de `CScheduler`).
3. Vecteurs `VBAR_EL1`, trap frame, chemin d'appel système.
4. Tables de pages par processus (`CAddressSpace`), commutation TTBR0/ASID.
5. Chargeur ELF64 → processus.
6. Framebuffer (`C2DGraphics`) + cœur de rendu `GImage` (porté du `SimpleOS` FreeBASIC de
   l'auteur).
7. Compositeur + gestionnaire de fenêtres ; deux démos animées simultanément.
8. **Bascule sur Option C** (apps EL1 + appel direct) puis **ABI à table fixe**.
9. Bureau Maynard (panel + applist), sous-système flux/stdio, terminal + `/bin`,
   gestionnaire de fichiers.
10. Dialogues modaux, thèmes, fond d'écran applicatif, gestion PID, dispositions clavier,
    éditeur de thème (ABI v16).
