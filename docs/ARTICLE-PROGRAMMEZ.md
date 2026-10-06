# Article pour Programmez! — mémoire de travail

> Note de travail (en français : c'est la langue de l'article). Pas un document de référence d'Onyx :
> il n'est pas exporté par `docs/build_docs.py`. Ouvert le 2026-10-06.

## Contexte

- Le magazine **Programmez!** a répondu : il s'intéresse à l'usage de l'IA dans le développement d'Onyx.
- Il va envoyer des **specs** (format, longueur, ton, encadrés) : **pas encore reçues**. Quand elles arrivent,
  adapter le plan et rédiger la version complète.
- Le travail se fera **par itérations** : ébauches, validations, modifications.
- Rôle de Claude : aider à la **mise en texte** à partir du topo de l'utilisateur ci-dessous.

## Angle retenu (proposition)

**« Je suis l'architecte, Claude est le développeur. »** Ne pas dire que l'IA a tout fait seule, mais
montrer *comment* un humain qui porte la vision et une IA qui produit le code ont mené un projet système
impossible à finir seul sur son temps libre. Pour des lecteurs développeurs, la méthode est le cœur.

## Plan proposé

1. **Chapeau** (3-4 lignes) : un OS multitâche préemptif sur Raspberry Pi 4, écrit avec une IA. Comment,
   et ce qu'on en retient.
2. **La genèse** : la borne d'arcade, Circle, le snake, l'idée d'OS, l'essoufflement.
3. **Le renouveau** : l'IA au travail, le déclic.
4. **Encadré technique : Circle → Onyx** (le « gap »).
5. **La méthode** (le cœur).
6. **Ce que ça a donné** : chiffres et captures (`screenshots/`).
7. **Avantages, inconvénients, leçons, conseils** : c'est ce qui rend l'article crédible.

## Le topo de l'utilisateur (sa version, reformulée)

### La genèse

Dans le cadre de ses loisirs, l'utilisateur construit une petite borne d'arcade : un Raspberry Pi sous
Linux. Il se dit qu'un OS dédié, voire un petit système bare metal, serait plus adapté, surtout s'il porte
ou écrit lui-même des jeux. Il découvre **Circle**, une plateforme bare metal pour le Pi. Les premiers
essais marchent : un **snake** avec deux contrôleurs de boutons branchés en USB. Vient l'idée d'un OS sur
le Pi. Avec le temps, le projet s'essouffle et il le met de côté.

### Le renouveau

Au travail, son équipe explore l'usage de l'IA pour programmer. À force de l'utiliser, il prend la mesure
du potentiel de Claude, et l'idée lui vient de lui confier le travail technique pour continuer l'OS.
Il fait prendre connaissance à Claude de Circle et de l'ébauche du snake, et lui partage sa vision :
partir des briques de Circle pour en faire un **OS multitâche**. Le gap est énorme : Circle propose un
multitâche **coopératif** dans **un seul espace d'adressage** ; Onyx est **préemptif**, chaque processus
en **EL0** dans **son propre espace d'adressage**.

On présentera Circle et Onyx brièvement.

### La méthode (le « comment »)

- **Le modèle** : Opus, jugé plus adapté à une réflexion intense et technique.
- **Le code sur GitHub** pour donner accès à Claude.
- **1. Analyse** : Claude analyse le code ; l'utilisateur décrit ses objectifs.
- **2. Plan** : Claude propose un plan étape par étape, chaque étape débloquant si nécessaire les
  fonctionnalités dont la suivante a besoin.
- **3. « Grilling »** si nécessaire : une phase où les choix sont arrêtés. Pour les apps : une
  **maquette de la GUI** d'abord, pour s'assurer d'avoir la même vision.
- **4. Développement autonome** une fois le tout cadré.
- **5. Handoff** : à la fin, Claude documente ses changements dans un `.md` (pour que les sessions
  suivantes en prennent connaissance : `docs/HANDOFF.md`) et fait un rapport.
- **6. Test sur le matériel réel** : à chaque phase ou à la fin, Claude dit quoi tester sur le Pi et le
  résultat attendu.

### Avantages et inconvénients retenus par l'utilisateur

- **Un OS est très technique, mais repose sur des sujets très bien documentés.** À ce niveau, Claude est
  fort pour retrouver les algorithmes qui marchent et les mettre en œuvre (ordonnancement, MMU, tables de
  pages, allocateurs, pilotes, formats de fichiers…).
- **Un levier extraordinaire pour la créativité.** L'IA code vite et donne vite des résultats.
- **Le revers : le risque de se disperser.** On travaille sur l'interface graphique, puis une idée d'app
  surgit, puis un petit bug revient en mémoire… L'utilisateur lance parfois **5 conversations en
  parallèle**. Il doit alors prévoir **un agent qui centralise les résultats** pour les consolider dans
  la branche principale (`main`). Voir aussi la règle git de `CLAUDE.md` : chaque session fusionne
  `origin/main` avant de commencer et avant de committer, puis pousse dans `main`.

### Le coût (témoignage de l'utilisateur, 2026-10-06)

- Il faut l'expliquer clairement : sur un projet de cette taille, **les tokens peuvent se consommer
  vite**. La vraie question est : **accepte-t-on parfois d'attendre ?**
- Son vécu : il était sur l'abonnement **Max à 100 €**. Ça marchait très bien, puis à un moment il n'avait
  plus assez de quota pour la semaine. Ne voulant pas **attendre 3 jours** que le quota se renouvelle, il a
  **upgradé** son abonnement.
- Sa conclusion : sans son impatience, et s'il avait été moins « dispersé » dans ses idées, **l'abonnement
  à 100 € aurait sans doute suffi**.
- Lien à faire dans l'article : coût et dispersion vont ensemble. Cinq conversations en parallèle, c'est
  cinq fois plus de quota. Le vrai arbitrage est entre **patience/discipline** et **budget**.

- **Les prix des formules Claude** (l'utilisateur accepte qu'on les cite). Relevés le 2026-10-06 sur des
  sites tiers, en dollars US, **à revérifier sur la page officielle (claude.com/pricing) et en euros TTC
  pour la France avant publication** :
  - **Free** : 0 $ (accès limité) ;
  - **Pro** : 20 $/mois (17 $/mois en paiement annuel) ; inclut Claude Code ;
  - **Max 5x** : 100 $/mois, 5 fois l'usage de Pro (la formule « Max à 100 € » de l'utilisateur) ;
  - **Max 20x** : 200 $/mois, 20 fois l'usage de Pro (la formule vers laquelle il a upgradé, à confirmer) ;
  - pour info : Team (Standard 25 $/siège, Premium 125 $/siège), Enterprise sur devis.

### La dispersion en chiffres : les branches GitHub

L'utilisateur propose d'illustrer la dispersion par **le nombre de branches** du dépôt. Relevé du
2026-10-06 : **16 branches en plus de `main`**, dont 9 créées par des sessions Claude
(`claude/<nom-aléatoire>`, par ex. `claude/quirky-feynman-0f3259`) et d'autres nommées par sujet
(`webkit-port`, `usb-volumes`, `kapi-compact`, `term_updates`…). 14 sont déjà fusionnées dans `main`,
2 portent encore des commits à elle (`kapi-compact` : 5, `claude/bold-gould-4a406c` : 1). Une capture
de la liste des branches sur GitHub ferait une bonne illustration (à prendre **avant** l'élagage).

**À faire (l'utilisateur) : un élagage des branches**, sur sa décision. Les 14 branches fusionnées peuvent
être supprimées sans perte ; les 2 autres sont à examiner d'abord.

## Les traces de la méthode dans le dépôt (illustrations possibles)

La méthode a laissé des traces écrites. Ce sont autant d'exemples ou de captures pour l'article :

- **Étude → choix de l'utilisateur → plan → tests sur le Pi** : `docs/GUI-USERSPACE-STUDY.md` (une étude de
  faisabilité ; elle se termine par une section « For the user to decide », puis une option barrée et
  marquée *decided (2026-10-04)*) → `docs/SHARED-LIBS-PLAN.md` (un tableau **D1-D8 « What the user
  decided »**, les choix par défaut P1-P6 « not contradicted », le plan par étapes 0/1a/1b/1c, la
  section « Tests — to automate on the Pi », puis **« Results, and what is still not verified »** :
  17 vérifications passées, 84 apps démarrées sans échec, ce qui a été trouvé en route, ce qui n'est pas
  vérifié). C'est le cycle complet sur un seul sujet, le meilleur exemple.
- **Les maquettes** : **14 dossiers `docs/<app>/mockups`, 96 images** (3dforge, archiver, clipboard,
  daw, gui-redesign, mail, media, paint, pdf, photos, pkg, qbstudio, screenshot, slides). Chaque
  `README.md` raconte l'étude, les maquettes et les décisions.
  - `docs/gui-redesign/README.md` : l'utilisateur demande un avis (rétro ou mobile ?) ; Claude dessine
    le même bureau en trois habillages (Workbench, CDE, Windows 3.1) ; l'utilisateur préfère CDE et
    demande « à quoi ressemblerait un CDE modernisé ? » → la direction retenue. Puis la section
    « The open questions, as answered ». Les images `mockups/retro-*.png` puis `cde-modern*.png` montrent
    l'itération en une planche : une très bonne illustration.
  - `docs/mail/README.md` : « study, first mock-ups », puis **« Decided (the user, 2026-10-02) »** :
    le nom, les trois colonnes de la maquette, un moteur HTML maison plutôt que Jet, Gmail/Outlook.
- **Le handoff et les tests sur le matériel** : `docs/HANDOFF.md` (environ 2 000 lignes), une section par
  chantier dont le titre dit l'état honnêtement (« built, tested on the PC, **NOT yet on the Pi** »),
  avec des listes « **To test on the Pi** » (quoi brancher, quoi faire, quoi attendre) et les retours de
  l'utilisateur (« On the Pi (the user, 2026-10-05, a screenshot): the app runs… »).
- **Les études mises de côté** : `docs/MULTI-USER-PLAN.md`, une étude complète que l'utilisateur a
  décidé de ne pas lancer (« set aside, Onyx stays single-user »). Elle montre que l'humain arbitre :
  tout ce que l'IA propose n'est pas fait.
- **Les règles de travail** : `CLAUDE.md` (kits first, documentation à jour, git sur `main`, publication
  des paquets, licences MIT). Ce sont les consignes permanentes données à chaque session, une sorte de
  « contrat » entre l'humain et l'IA.
- **La correction** : dans les rapports, les sections « Found on the way » et « Not verified » listent les
  bugs trouvés et ce qui reste à vérifier. L'historique git montre aussi les commits de correction après
  les retours du Pi.

## Étude de cas n°2 : de NetSurf à WebKit (relevé du dépôt, 2026-10-06)

Le récit de l'utilisateur : on est partis de NetSurf ; il a demandé ce qu'il faudrait pour WebKit ; on a
abordé POSIX ; Claude a analysé ce qu'il fallait de POSIX, les manques, et on les a comblés un à un ;
puis le portage des dépendances, etc. ; puis le GC concurrent qui faisait laguer le JavaScript, résolu en
désactivant la concurrence. **Le dépôt confirme chaque étape, avec les dates (historique git) :**

1. **NetSurf poussé à ses limites** (jusqu'au 2026-10-01) : le navigateur « Jet » était un portage de
   NetSurf, enrichi commit après commit (Selectors 4, grid, transitions et animations CSS, un cache de
   code JS…). Le 2026-09-30, `docs/07` compare ce navigateur à Ladybird, Chromium, WebKit et Opera : les
   écarts restent grands.
2. **« Que faudrait-il pour WebKit ? » → l'analyse POSIX** (2026-10-02 matin) : `docs/POSIX-PLAN.md`
   (environ 1 650 lignes). Sa section 0, « Findings from the code », liste ce qui manque, vérifié dans le
   code : la chaîne de compilation sans threads ni TLS, newlib sans `sys/mman.h`, `poll.h`,
   `sys/socket.h`… (une vingtaine d'en-têtes), `libc.a` sans `mmap`, `clock_gettime`, `posix_memalign`…
   (17 fonctions), les sockets de Circle sans `select` ni `connect` non bloquant. Puis les « work
   packages » : **WP-MEM** (pagination à la demande, mmap, TLS), **WP-FILE/PROC** (descripteurs, pipes,
   spawn), **WP-NET** (sockets BSD, poll), **WP-LIBC** (la couche POSIX, `libonyxposix`), **WP-TC** (une
   vraie chaîne `aarch64-onyx-elf` : GCC 14.2, threads POSIX, TLS natif). Avec une estimation :
   « about 45-60 agent-days… about 6-8 weeks elapsed ».
3. **Les décisions de l'utilisateur, datées dans le plan** : « The goal: WebKit replaces Jet » (§8),
   « Decision: WebKit2 » sur le modèle du portage PlayStation (§9), un test externe (l'Open POSIX Test
   Suite, §7), `fork()` « pour plus tard » (§12)…
4. **Les manques comblés un à un** : commits `WP-0` (le squelette de l'ABI), `WP-MEM`, `WP-NET`,
   `WP-FILE/PROC`, `libonyxposix`, puis les « smoke ports » (SQLite, libxml2, curl + mbedTLS) qui
   prouvent la couche, puis le premier tour de tests sur le Pi (un bug de compteurs trouvé et corrigé),
   puis la chaîne de compilation, puis **WP-IPC** (sockets locaux, passage de descripteurs, mémoire
   partagée : ce que WebKit2 demande).
5. **Les dépendances portées** : ICU 78.3, FreeType, libpng, libjpeg-turbo, libwebp, HarfBuzz, Skia.
6. **WebKit lui-même**, par étapes validées sur le Pi : JavaScriptCore (`jsc`, « five test steps pass »
   sur le Pi) → WebCore rend une page (Skia sur le CPU) → WebKit2 et le navigateur (kotonstudio.com en
   HTTPS sur le Pi, 2026-10-03) → le JIT → le compositeur sur le GPU V3D → la vidéo. Le 2026-10-04,
   NetSurf quitte le dépôt et le navigateur WebKit prend le nom de **Jet**. Une série de **25 patches**
   sur WebKit (`tools/webkit/patches/`).
7. **Le GC concurrent qui faisait laguer le JS** (`docs/08-WEBKIT-PORT.md`, « And the collector »,
   corrigé le 2026-10-04) : le ramasse-miettes de JavaScriptCore est concurrent par défaut ; il marque à
   côté du script et, pour suivre, lui prend le processeur. Or sur Onyx, les threads d'un programme
   partagent un seul cœur. Mesure sur le Pi : 400 000 petites chaînes en **3,9 s**, contre **0,2 s**
   avec `--useConcurrentGC=0` ; `--logGC=1` montrait le script réduit à **2 %** du temps, un cycle de
   3,6 s. Correction : `useConcurrentGC = false` et un seul thread de marquage par défaut (patch `0023`).
   Une belle anecdote : le diagnostic est venu d'une **mesure**, et la solution est un réglage, pas du
   code.

**Le point frappant pour l'article** : le plan estimait 6 à 8 semaines pour le seul socle POSIX. Selon
les dates des commits, le socle, l'IPC, la chaîne de compilation et les dépendances ont été faits le
**2026-10-02** (plusieurs agents en parallèle), et le navigateur WebKit tournait sur le Pi le lendemain.
**À confirmer par l'utilisateur** (ce sont les dates des commits ; le travail a pu commencer avant) avant
de l'écrire.

## Repères de chronologie (historique git, 2026-10-06)

- **2026-06-22** : premier commit, « Bootstrap: multi-process kernel on Circle ». Le même jour : un
  ordonnanceur préemptif à la place de celui de Circle, des espaces d'adressage par processus
  (TTBR0/ASID), un premier processus EL0, le compositeur et deux démos fenêtrées.
- Une marche arrière honnête, le même jour : « Option C: apps run in EL1 with per-app page tables + direct
  kernel calls » (les apps repassent en EL1 pour avancer). **Le mode protégé EL0 n'est revenu que le
  2026-10-02** (`docs/EL0-PROTECTED-MODE.md` : « every app runs at EL0, the EL1 legacy mode is
  removed »). À raconter : on accepte un raccourci pour avancer, puis on paie la dette quand le système
  est mûr.
- **1 444 commits** sur `main` au 2026-10-06, en un peu plus de trois mois.

## Ébauche de texte (v0, à reprendre)

**La genèse.** Tout est parti d'un loisir : la construction d'une petite borne d'arcade, animée par un
Raspberry Pi sous Linux. Très vite, une idée s'impose : pour une machine dédiée au jeu, un système complet
est surdimensionné. Un OS dédié, voire un programme « bare metal » qui tourne directement sur le matériel,
serait plus adapté. Surtout si je voulais porter ou écrire moi-même des jeux. C'est là que je découvre
**Circle**, un environnement bare metal en C++ pour le Raspberry Pi. Les premiers essais sont
concluants : un *snake* jouable à deux, avec deux contrôleurs de boutons branchés en USB. De là naît une
ambition plus grande : bâtir un vrai système d'exploitation sur le Pi. Mais la marche est haute, le temps
libre limité, et le projet finit dans un tiroir.

**Le renouveau.** Au travail, nous avons été amenés à explorer l'usage de l'IA pour programmer. À force de
l'utiliser, j'ai pris la mesure de ce dont Claude était capable. Une idée m'est alors venue : et si je lui
confiais le travail technique pour reprendre mon OS ? Je garderais la vision, les choix et la validation.

**Encadré : de Circle à Onyx.** *Circle* fournit les briques : pilotes USB, réseau, écran, son, cartes SD.
Mais il offre un multitâche **coopératif**, dans **un seul espace d'adressage** : une tâche qui boucle
bloque tout, et une tâche qui écrit au mauvais endroit corrompt tout le système. *Onyx* garde Circle comme
couche d'abstraction matérielle et bâtit au-dessus un vrai système : multitâche **préemptif**, chaque
processus en **EL0** (le mode non privilégié de l'ARMv8) dans **son propre espace d'adressage** (une table
de pages par processus, étiquetée par ASID). Les programmes appellent le noyau via des appels système,
pas par appel direct. Un programme fautif est tué ; le système continue.

**La méthode.** J'utilise Claude Opus : pour un travail de réflexion aussi technique, le modèle le plus
puissant me semblait le bon choix. Le dépôt GitHub est le terrain commun : Claude y lit le code, y
travaille et y pousse ses modifications. (1) Claude prend connaissance de l'existant et je lui décris
mes objectifs. (2) Il propose un plan par étapes, chacune apportant si nécessaire ce dont la suivante a
besoin. (3) Quand un sujet le demande, un « grilling » arrête les choix ; pour une application, je demande
d'abord une maquette de l'interface. (4) Une fois le cadre posé, Claude développe seul. (5) À la fin, il
rédige un *handoff* (un document Markdown qui décrit l'état du projet) pour que les sessions suivantes
reprennent là où il s'est arrêté, puis me fait un rapport. (6) À chaque phase, il m'indique quoi tester
sur le Pi et le résultat attendu.

## Chiffres tirés du dépôt (au 2026-10-06, à revérifier avant publication)

- Environ **100 applications** (`user/Apps`) : bureau, terminal, tableur, éditeur, peinture, mail, IRC,
  jeux, synthés…
- **Émulateurs** GB, GBA, NES, SNES, N64, GameCube ; **portages** Doom, SuperTuxKart.
- **Jet**, un navigateur : portage de WebKit, en cours.
- **10 kits** (bibliothèques partagées : AppKit, UIKit, SystemKit, NetKit, FileKit, ImageKit, AudioKit,
  FontKit, PrinterKit, GpioKit).
- Un **gestionnaire de paquets signés** avec mises à jour (`pkg`, `pkgman`, `pkgd`).
- Environ **33 documents** de référence dans `docs/`.
- Environ **326 000 lignes** C/C++/asm dans `kernel/` + `user/` hors Circle. Ce total **inclut des
  bibliothèques tierces** (zlib, etc.) : séparer ce qui a été écrit pour Onyx.
- **Durée du projet** : premier commit le **2026-06-22**, 1 444 commits au 2026-10-06 (voir « Repères de
  chronologie »). À fournir par l'utilisateur : le temps qu'il y consacre.

## À compléter (par l'utilisateur)

- Ce qui a coincé : bugs difficiles, allers-retours, erreurs de Claude et comment elles sont apparues
  (souvent au test sur le Pi).
- Son rôle réel : arbitrer, refuser, recadrer (les règles de `CLAUDE.md` : « kits first », licences MIT…).
- Le temps passé. (Le coût : voir « Le coût » plus haut ; confirmer la formule choisie à l'upgrade.)
- Un conseil à un lecteur qui voudrait faire pareil.

## Suivi

- 2026-10-06 : topo reçu, plan et ébauche v0, avantages/inconvénients ajoutés, puis le coût
  (Max 100 €, quota hebdomadaire épuisé, upgrade), les prix des formules, les branches comme
  illustration de la dispersion (élagage à faire), les traces de la méthode dans le dépôt, l'étude de cas
  NetSurf → WebKit (POSIX, dépendances, GC), la chronologie. **En attente des specs.**
