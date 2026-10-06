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
- **Durée du projet** : inconnue de Claude (l'historique git des clones est tronqué). À fournir par
  l'utilisateur : la date de reprise et le temps qu'il y consacre.

## À compléter (par l'utilisateur)

- Ce qui a coincé : bugs difficiles, allers-retours, erreurs de Claude et comment elles sont apparues
  (souvent au test sur le Pi).
- Son rôle réel : arbitrer, refuser, recadrer (les règles de `CLAUDE.md` : « kits first », licences MIT…).
- Le temps passé. (Le coût : voir « Le coût » plus haut ; préciser le montant de l'abonnement supérieur
  si on veut le citer.)
- Un conseil à un lecteur qui voudrait faire pareil.

## Suivi

- 2026-10-06 : topo reçu, plan et ébauche v0, avantages/inconvénients ajoutés, puis le coût
  (Max 100 €, quota hebdomadaire épuisé, upgrade). **En attente des specs.**
