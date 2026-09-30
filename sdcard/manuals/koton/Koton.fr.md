# Koton — Manuel de l'utilisateur

*Un studio de musique qui pense en harmonie, sur Onyx*

Édition de septembre 2026, pour Koton tel que livré avec Onyx. Les illustrations montrent le morceau de
démonstration fourni avec Koton, `SD:/koton/songs/demo.kson` : une bossa en fa♯ mineur à 108 temps par minute.

## Sommaire

- [1. Introduction](#1-introduction)
- [2. Premiers pas](#2-premiers-pas)
- [3. La fenêtre](#3-la-fenêtre)
- [4. Le morceau : tonalité, mesure, tempo](#4-le-morceau--tonalité-mesure-tempo)
- [5. Les pistes](#5-les-pistes)
- [6. Les blocs sur l'arrangement](#6-les-blocs-sur-larrangement)
- [7. La piste d'accords](#7-la-piste-daccords)
- [8. Les accompagnements](#8-les-accompagnements)
- [9. Les riffs : le piano roll](#9-les-riffs--le-piano-roll)
- [10. La batterie](#10-la-batterie)
- [11. Les lignes mélodiques](#11-les-lignes-mélodiques)
- [12. Les polyrythmes : les anneaux](#12-les-polyrythmes--les-anneaux)
- [13. Dessiner dans une grille](#13-dessiner-dans-une-grille)
- [14. Écouter et jouer](#14-écouter-et-jouer)
- [15. Le son : SoundFont, chaîne de son, plugins](#15-le-son--soundfont-chaîne-de-son-plugins)
- [16. Composer avec l'IA](#16-composer-avec-lia)
- [17. Fichiers et export](#17-fichiers-et-export)
- [18. Questions et réponses](#18-questions-et-réponses)
- [19. Glossaire](#19-glossaire)
- [Annexe A. Les raccourcis clavier](#annexe-a-les-raccourcis-clavier)
- [Annexe B. Qualités et couleurs d'accords](#annexe-b-qualités-et-couleurs-daccords)
- [Annexe C. Les styles d'accompagnement](#annexe-c-les-styles-daccompagnement)
- [Annexe D. Les plugins](#annexe-d-les-plugins)

## 1. Introduction

### 1.1 Ce que fait Koton

Koton est un studio de musique — une *station audionumérique* (DAW) — pour Onyx. C'est **Koton Studio**,
une DAW pour Windows, refaite pour Onyx, et elle fonctionne de la même façon : **on pense d'abord en
harmonie**.

Un morceau est un ensemble de **pistes**, et une piste une rangée de **blocs**. Un bloc n'est pas une
liste de notes saisies une à une : c'est un petit **générateur** qui fabrique ses notes — un accord, un
accompagnement qui joue les accords, une ligne mélodique, un groove de batterie, des anneaux
euclidiens. En bas de chaque morceau se trouve la **piste d'accords** : elle est muette, elle ne porte
que l'harmonie, et toutes les autres parties la lisent. Changez-y un accord : l'accompagnement, la
basse et la mélodie suivent.

Avec Koton, vous pouvez :

- écrire une **grille d'accords** par **degrés** (I, IV, V, vi…) qui suivent la tonalité quand vous la
  changez, avec un **copilote** qui propose l'accord suivant, 30 sortes de **cadences**, les dominantes
  secondaires et la conduite des voix ;
- la faire **accompagner** dans l'un des 28 styles — accords plaqués, arpèges, basse d'Alberti, comping
  jazz, bossa, reggae, valse, funk, harpe… — ou selon un motif que vous dessinez sur les voix de l'accord ;
- ajouter des **lignes mélodiques** dont vous dessinez le rythme pendant que Koton choisit les hauteurs
  dans l'harmonie, ou des **riffs** écrits sur un piano roll qui montre l'accord sous chaque note ;
- ajouter de la **batterie** tirée d'un catalogue ou dessinée sur les 47 percussions General MIDI, et des
  **polyrythmes** en anneaux euclidiens ;
- faire jouer le tout par un synthétiseur à **SoundFont**, un par piste, calculé sur le troisième cœur
  du Pi, et ajouter des **plugins** — instruments, effets et générateurs — qui tournent comme des
  programmes à part ;
- demander à une **IA** (Gemini et d'autres) de composer un morceau entier, une nouvelle partie ou une
  batterie sur votre morceau ;
- ouvrir les morceaux de **Koton Studio pour Windows** (`.sq`), enregistrer les vôtres en **`.kson`**,
  exporter un fichier **WAV**, jouer ou écrire avec un **clavier MIDI USB**.

Cette édition n'a pas de vue partition.

### 1.2 Ce qu'il vous faut

- Un Pi sous Onyx, avec un **casque ou des enceintes** sur la prise jack 3,5 mm.
- La **SoundFont** fournie sur la carte (`SD:/koton/soundfonts/GeneralUser-GS.sf2`, 32 Mo). Koton la
  charge au démarrage : cela prend quelques secondes.
- Pour l'IA : le **réseau** actif (voir le guide de l'utilisateur d'Onyx) et une **clé d'API** d'un
  fournisseur — celles de Gemini sont gratuites sur `aistudio.google.com`.
- En option : un **clavier MIDI USB** (n'importe quel modèle conforme à la norme), branché avant ou
  pendant que Koton tourne.

### 1.3 À propos de ce manuel

- Les écrans de Koton sont en **anglais**. Les boutons, menus et champs sont donc écrits en **gras**, tels
  qu'à l'écran, suivis au besoin de leur traduction : **Listen** (écouter), **Save style…** (enregistrer
  le style).
- Une commande de menu s'écrit **File ▸ Open…** : le menu **File**, puis son article **Open…**.
- Les touches s'écrivent `Ctrl+S`, `Space` (espace), `Del` (suppr), `Esc` (échap).
- Les noms de notes et d'accords sont à l'anglaise, comme à l'écran : C = do, D = ré, E = mi, F = fa,
  G = sol, A = la, B = si ; `F#m9` = fa♯ mineur neuvième.

## 2. Premiers pas

### 2.1 Ouvrir Koton

Ouvrez **Koton** depuis le tiroir *Productivity* du dock, ou double-cliquez un fichier `.kson` ou `.sq`
dans le File Viewer : Koton s'ouvre avec lui. La fenêtre occupe l'écran ; la barre d'état, en bas, dit où
tourne le moteur sonore — **Engine on core 2** est ce qu'on souhaite y lire.

Koton démarre avec un morceau vide de cinq pistes : *Melody*, *Accompaniment*, *Bass*, *Drums* et la piste
*Chords*. Pour voir la démonstration, choisissez **File ▸ Open…**, ouvrez `SD:/koton/songs/demo.kson`,
puis appuyez sur `Space` : il joue.

### 2.2 Votre premier morceau en dix minutes

1. **La tonalité.** Cliquez sur **Key** dans la barre de transport, choisissez une tonalité (par exemple
   *A*, *Natural minor (Aeolian)* : la mineur naturel) et un tempo, puis **OK**.
2. **Les accords.** Dans le navigateur à droite, cliquez sur **Chord** : un premier accord apparaît sur la
   piste d'accords (la tonique). Cliquez sur l'accord : son éditeur s'ouvre en bas. Dans **Suggest the
   next chord** (proposer l'accord suivant), cliquez sur une des cartes : cet accord est ajouté après. Ou
   cliquez sur **Chain 4 bars** : le copilote en écrit quatre de plus. Ou **Cadence…** : choisissez un
   style (*Pop (I-V-vi-IV)*, *Jazz (ii - V - I)*…) et un nombre de mesures.
3. **L'accompagnement.** Cliquez sur l'en-tête de la piste *Accompaniment*, puis sur **Accompaniment**
   dans le navigateur : un bloc apparaît. Tirez son bord droit pour qu'il couvre tous vos accords. Dans
   son éditeur, choisissez un **Style** — *Bossa nova / Latin*, *Arpeggio up*, *Waltz*…
4. **La basse.** Sélectionnez la piste *Bass* et ajoutez-y aussi un **Accompaniment**, avec le style
   *Pop (bass + chord)* et **Octave** 2, ou cochez **Bass** avec un style léger.
5. **La batterie.** Cliquez sur **Drum pattern** dans le navigateur ; dans son éditeur, choisissez une
   **Category** (catégorie) et un **Motif**.
6. **La mélodie.** Sélectionnez la piste *Melody* et cliquez sur **Melodic line** : Koton joue une ligne
   sur vos accords. Changez son **Contour**, ou redessinez son rythme dans la grille.
7. **Écoutez.** Appuyez sur `Space`. Encore une fois pour arrêter.
8. **Enregistrez.** **File ▸ Save** (`Ctrl+S`) : donnez un nom ; le morceau va dans `SD:/koton/songs`.

> **Astuce.** Chaque bloc a un bouton **Listen** dans son éditeur : il joue ce bloc seul, en boucle,
> pendant que vous le modifiez.

## 3. La fenêtre

![La fenêtre de Koton](images/window.png)
*La fenêtre avec le morceau de démonstration, le premier accord sélectionné.*

1. **Enregistrer, Annuler, Rétablir** — puis le transport : retour au début, **Lecture / Arrêt**, Arrêt,
   **Boucle**, le **métronome**.
2. **La position** : mesure.temps.double-croche, et le temps écoulé depuis le début.
3. **Le morceau** : son tempo (**BPM**), sa tonalité (**Key**), sa mesure (**Meter**) et son **Swing**. Un
   clic sur l'un d'eux ouvre les réglages du morceau (section 4).
4. **Snap** (aimantation) : comment les blocs s'alignent quand on les déplace ou les étire — sur la
   mesure, le temps, le demi ou le quart de temps, ou pas du tout.
5. **Le moteur** : où il tourne (*CORE 2 — DSP* au mieux) et sa charge.
6. **Compose with AI…** (composer avec l'IA, section 16).
7. **Le niveau général**, voies gauche et droite.
8. **La règle** : un clic y place le curseur de lecture ; un glissé crée une boucle.
9. **Sections** : des repères nommés (*Intro*, *Couplet*…) ; **Tempo** en dessous montre le tempo du
   morceau.
10. **L'en-tête d'une piste** : son nom, muet et solo, son son, son volume et son panoramique
    (section 5).
11. **Un bloc** : son nom, et une vignette des notes qu'il joue.
12. **La piste d'accords** : le nom de chaque accord, son degré en chiffres romains et sa **fonction** —
    tonique (bleu), sous-dominante (vert), dominante (orange).
13. **L'éditeur** du bloc sélectionné. Tirez la petite poignée de son bord supérieur pour l'agrandir.
14. **La chaîne de son** de la piste sélectionnée : son instrument, ses effets, sa réverbération, son niveau.
15. **Le navigateur** : ce que vous pouvez mettre dans le morceau.
16. **La barre d'état** : le moteur, la latence, les voix qui jouent, la SoundFont, le dernier message et
    le fichier (*modified* quand il y a des changements non enregistrés).

![La barre de transport](images/transport.png)
*La barre de transport.*

Les **menus** sont dans la barre de menus d'Onyx, en haut de l'écran : **File** (fichier), **Edit**
(édition), **Song** (morceau), **Track** (piste), **Transport**, **View** (affichage) et **AI**.

## 4. Le morceau : tonalité, mesure, tempo

Cliquez sur **BPM**, **Key**, **Meter** ou **Swing** dans la barre de transport, ou choisissez **Song ▸
Key, meter, tempo…** (`Ctrl+K`).

![Les réglages du morceau](images/song.png)
*Les réglages du morceau.*

- **Key** : la tonique (C, C♯, D♭ … B) et le **mode** : *Major (Ionian)* (majeur, ionien), *Natural minor
  (Aeolian)* (mineur naturel, éolien), *Harmonic minor* (mineur harmonique), *Melodic minor* (mineur
  mélodique), *Dorian* (dorien), *Phrygian* (phrygien), *Lydian* (lydien), *Mixolydian* (mixolydien),
  *Locrian* (locrien).
- **A new key** (une nouvelle tonalité) : ce qui arrive au morceau quand vous changez la tonalité :
  - **Transpose the song** (transposer le morceau) — tout bouge : les notes des riffs, les accords. Un
    morceau en do majeur devient le même morceau en ré majeur ; un changement de mode (majeur → mineur)
    modifie aussi la tierce, la sixte et la septième de la gamme.
  - **Chords follow their degrees** (les accords suivent leurs degrés) — seuls les accords changent : un
    accord écrit *IV* reste le quatrième degré de la nouvelle tonalité. Pratique pour essayer votre grille
    dans un autre mode.
- **Meter** (mesure) : les temps par mesure et la valeur de note (`4/4`, `3/4`, `6/8`…). En `6/8` et dans
  les autres mesures composées, les arpèges divisent le temps en trois.
- **Tempo (bpm)** : de 20 à 400.
- **Swing (%)** : 50 est binaire ; 66 un swing ternaire ; jusqu'à 75.
- **Humanize (%)** (humaniser) : de petits décalages aléatoires du placement et de l'intensité, comme en
  ferait un musicien.
- **Length (bars)** (longueur en mesures) : la longueur minimale du morceau (il est plus long quand ses
  blocs vont plus loin).

## 5. Les pistes

### 5.1 L'en-tête

![L'en-tête d'une piste](images/header.png)
*L'en-tête d'une piste.*

1. **Le nom**. Double-cliquez la partie vide de l'en-tête pour renommer la piste.
2. **M** : rendre la piste muette.
3. **S** : solo — seules les pistes en solo s'entendent.
4. **Le son** : un clic ouvre le choix de l'instrument (ou du kit de batterie).
5. **Le volume** : tirez la barre ; le nombre est en décibels.
6. **Le panoramique** : tirez le bouton vers le haut ou le bas — gauche, centre, droite.

L'en-tête de la piste d'accords n'a pas de son (elle est muette) : un clic dessus ouvre **Cadence…**.

### 5.2 Choisir un son

![Choisir un instrument](images/sound.png)
*Choisir un instrument : les familles General MIDI à gauche.*

Pour une piste d'instrument, choisissez une **famille** (piano, orgue, guitare, basse, cordes, cuivres,
synthé…) à gauche, puis un **instrument** à droite : Koton joue une courte phrase avec lui à chaque choix.
**OK** le garde. Une piste de batterie montre les **kits** de la SoundFont, tels qu'elle les nomme.
Quand des plugins sont installés, la famille **Plugin instruments** les liste (section 15.3).

### 5.3 Le menu de la piste

Cliquez du bouton droit sur l'en-tête d'une piste :

![Le menu de la piste](images/headmenu.png)
*Le menu de la piste.*

- **Rename…** (renommer), **Instrument…** (ou **Drum kit…**), **Collapse** / **Expand** (réduire /
  agrandir la piste).
- **Move up**, **Move down** (monter, descendre) — la piste d'accords reste toujours en bas.
- **Duplicate the track** (dupliquer la piste) — avec des copies de ses riffs.
- **Remove every block** (retirer tous les blocs), **Delete the track** (supprimer la piste).
- **Add an instrument track**, **Add a drum track** (ajouter une piste d'instrument, de batterie) — aussi
  dans le menu **Track** et le navigateur.

## 6. Les blocs sur l'arrangement

### 6.1 Poser un bloc

Trois façons :

- **Le navigateur** : cliquez sur un générateur (**Chord**, **Accompaniment**, **Drum pattern**,
  **Riff**, **Melodic line**…). Il va sur la piste sélectionnée — ou sur la première piste qui convient :
  les accords sur la piste d'accords, la batterie sur une piste de batterie —, au curseur de lecture si
  la place est libre, sinon après le dernier bloc de la piste.
- **Un double-clic** sur un endroit vide d'une piste : un bloc du genre habituel de la piste (le même que
  son dernier bloc ; un riff sur une nouvelle piste d'instrument, de la batterie sur une piste de
  batterie, un accord sur la piste d'accords).
- **Un clic droit** sur une piste : le menu de ce qu'on peut y mettre.

![Le menu d'une piste](images/lanemenu.png)
*Le menu d'une piste d'instrument.*

Sur une piste d'instrument : *Riff*, *Accompaniment*, *Melodic line*, *Melodic rings* (anneaux
mélodiques), *Poly chords* (accords polyrythmiques). Sur une piste de batterie : *Drums*, *Polyrhythm*.
Sur la piste d'accords : *A chord* (un accord), *Cadence…*, *Chain 4 bars after it* (enchaîner 4
mesures), *Poly chords*. Un clic droit sur un bloc propose aussi **Freeze into a riff** (figer en riff),
**Duplicate** et **Delete**.

### 6.2 Déplacer, étirer, supprimer

- **Cliquez** sur un bloc pour le sélectionner : son éditeur s'ouvre en bas.
- **Tirez-le** pour le déplacer. Il reste entre ses voisins, aligné selon **Snap**.
- **Tirez son bord droit** pour l'allonger ou le raccourcir. Ce que signifie la longueur dépend du bloc :
  un accord dure plus longtemps, un accompagnement couvre plus d'accords, un bloc de batterie répète sa
  mesure plus de fois, un riff a plus de place.
- `Del` supprime le bloc sélectionné, `Ctrl+D` le duplique juste après, `←` / `→` sélectionnent ses voisins.
- **Undo** (annuler, `Ctrl+Z`) et **Redo** (rétablir, `Ctrl+Y`) se souviennent des 40 derniers changements.

> **Remarque.** Les blocs gardent leur place les uns par rapport aux autres : le silence avant un bloc fait
> partie de lui. Quand vous supprimez un bloc, les suivants ne bougent pas.

### 6.3 Figer un bloc en riff

**Freeze into a riff** (clic droit sur le bloc) transforme n'importe quel générateur en riff ordinaire,
avec les notes qu'il joue à cet instant. Faites-le quand vous voulez retoucher des notes à la main. Le
riff ne suit plus les accords.

### 6.4 Sections, boucle, zoom

- **Sections** : double-cliquez la rangée *Sections* pour ajouter un repère nommé (*Refrain*…) ;
  double-cliquez un repère pour le renommer, ou videz son nom pour le retirer.
- **Boucle** : glissez sur la règle, ou appuyez sur le bouton **Loop** (`Ctrl+L`) : sans boucle encore
  définie, il boucle le bloc sélectionné, ou les quatre premières mesures. Un clic droit sur la règle
  active ou coupe la boucle.
- **Défilement et zoom** : la molette fait défiler les pistes, `Shift`+molette défile dans le temps,
  `Ctrl`+molette zoome (aussi **View ▸ Zoom in / out**).

## 7. La piste d'accords

### 7.1 Des accords par degrés

Chaque accord de la piste d'accords a un **degré** dans la tonalité : *i* est la tonique d'une tonalité
mineure, *V* sa dominante… Koton écrit en majuscules les degrés à tierce majeure (I, IV, V) et en
minuscules ceux à tierce mineure (ii, iii, vi). Un accord lié à son degré suit la tonalité : en do majeur,
*IV* est fa ; si le morceau passe en ré, il devient sol.

Chaque bloc d'accord est coloré selon sa **fonction** : **tonique** (bleu — le repos), **sous-dominante**
(vert — on s'éloigne), **dominante** (orange — une tension qui veut se résoudre).

### 7.2 L'éditeur d'accord

![L'éditeur d'accord](images/chord.png)
*L'éditeur d'accord.*

1. **Degree** (degré) : *I* … *VII*, les **dominantes secondaires** *V/ii* … *V/vi* (la dominante d'un
   autre degré, avec sa septième), ou **Manual (fixed chord)** (accord fixe) — choisissez alors librement
   sa **Root** (fondamentale).
2. **Colour** (couleur) : *Triad* (triade), *Sixth* (sixte), *7th*, *9th (7+9)*, *9th (add9)* ;
   **Suspension** : *Sus2*, *Sus4* ; **Force** : une qualité imposée — *Major*, *Minor*, *Augmented*
   (augmenté), *Diminished* (diminué), *Dominant*. **Beats** : sa durée en temps.
3. **Voicing** : ce que l'accord demande aux accompagnements qui le jouent — **Open voicing** (voicing
   ouvert : les notes écartées) et la **Voice leading** (conduite des voix) : *Auto (least motion)* déplace
   les voix le moins possible depuis l'accord précédent ; *Close at the top* garde la note du haut proche ;
   *Close to the bass* garde la basse proche ; *None* le joue à l'état fondamental ; *Fixed inversion* vous
   laisse choisir le renversement.
4. **L'accord sur un clavier**, son nom, son degré et sa fonction.
5. **Suggest the next chord** : les cartes du copilote — les accords qui suivent bien celui-ci, compte tenu
   des deux ou trois derniers et de l'endroit de la phrase (une fin de phrase appelle une cadence). Une
   carte montre le degré, l'accord et son effet (*Rest* : repos, *Tension*, *Opening* : ouverture…) ; les
   meilleures sont entourées. **Un clic sur une carte : cet accord est ajouté après celui-ci. Un clic droit :
   celui-ci devient cet accord.** La liste au-dessus des cartes choisit une **humeur** : *Joyful* (joyeux),
   *Serene* (serein), *Melancholic*, *Nostalgic*, *Epic*, *Bright* (lumineux), *Jazzy*.
6. **Chain 4 bars** : le meilleur choix du copilote, quatre fois.
7. **Cadence…** (section suivante).
8. **Listen** : l'accord, tenu.

### 7.3 Les cadences

![La cadence](images/cadence.png)
*Une cadence.*

**Cadence…** (l'éditeur d'accord, le menu **Song**, le navigateur, l'en-tête de la piste d'accords) écrit
toute une progression à la fin de la piste d'accords :

- **Style** : 30 styles — *Auto (rich)*, *Authentic (V - I)* (parfaite), *Plagal (IV - I)*, *Jazz (ii - V -
  I)*, *Turnaround (I-vi-ii-V)*, *Pop (I-V-vi-IV)*, *Doo-wop*, *EDM*, *Royal road*, *Pachelbel (canon)*,
  *Circle of fifths* (cycle des quintes), *Blues*, *Minor blues*, *Andalusian* (andalouse), *Phrygian /
  Spanish*…
- **From degree** : le degré de départ (par défaut, celui du dernier accord de la piste).
- **Bars** (mesures) et **Chords a bar** (accords par mesure).

Chaque fois, c'est une nouvelle variante : relancez-la si elle ne vous plaît pas (et annulez la
précédente).

### 7.4 Des accords polyrythmiques sur la piste d'accords

Un bloc **Poly chords** peut aussi se poser sur la piste d'accords : il fournit alors l'harmonie pendant sa
durée. Voir la section 12.3.

## 8. Les accompagnements

Un bloc **Accompaniment** joue **les accords de la piste d'accords, quels qu'ils soient**, dans un style.
Il ne porte lui-même aucun accord : étirez-le sur autant d'accords que vous voulez ; quand un accord change,
il suit.

![L'éditeur d'accompagnement](images/accomp.png)
*Un accompagnement dans le style Bossa nova / Latin.*

À gauche :

- **Cell (beats)** (cellule) : la longueur du motif, répété tout du long (4 en 4/4 : une mesure).
- **Length** : la longueur du bloc entier, en temps.
- **Style** : l'un des 28 styles (annexe C), **Custom (drawn)** (dessiné), ou un des styles que vous avez
  enregistrés dans ce morceau.
- **Octave** : la hauteur de l'accord (4 : autour du do central).
- **Bass**, **Bass on every beat** : la fondamentale de l'accord ajoutée en dessous (à chaque temps).
- **Open voicing** : *No*, *Yes*, ou *As the chord says* (comme le dit l'accord).
- **Halve the durations** : des arpèges deux fois plus rapides.
- **Voice leading** : *Auto (least motion)*, *Close at the top*, *Close to the bass*, *As the chord says*, ou
  *None (fixed inversion)* — et alors un **Inversion** (renversement). Avec la conduite des voix,
  **Tendency** départage deux choix : *Rising* (montant) ou *Falling* (descendant).

### 8.1 Dessiner son propre accompagnement

Cliquez sur **Customise this style** (personnaliser ce style) : le style devient une **grille des voix de
l'accord** que vous pouvez modifier.

![Un accompagnement dessiné](images/accgrid.png)
*La grille des voix de l'accord.*

Les rangées sont les voix de l'accord, du grave à l'aigu : **Bass** (basse), **1** (la fondamentale),
**3**, **5**, **7**, **1'** (la fondamentale une octave plus haut), **9**, **3'**, **5'**, **7'**, **9'**.
Une voix que l'accord n'a pas (un 7 sur une triade) joue la note de l'accord la plus proche. La grille est
la **cellule**, répétée. Dessinez-la comme à la section 13.

Au-dessus de la grille :

- **Resolution** : le nombre de colonnes par temps (4 : des doubles croches ; 3 ou 6 : des triolets).
- **Start from** un style, **Copy** : la grille faite à partir de ce style — puis modifiez-la.
- **Save style…** : la garder sous un nom ; elle apparaît à la fin de la liste **Style** de tous les
  accompagnements du morceau. **Apply to all** : tous les accompagnements qui utilisent ce style
  reçoivent cette grille.
- **Clear** (effacer).

### 8.2 La cellule mélodique

L'onglet **Melodic cell** ajoute une seconde voix sur les accords, dessinée sur les **degrés de la
tonalité** : les rangées *1* à *7''* (trois octaves). Son **Octave**, et où se trouve le **degré 1** :
*Chord's root* (la fondamentale de l'accord) ou *Inversion's bass* (la basse du renversement). Quand les
accords changent, la cellule suit.

![La cellule mélodique](images/melcell.png)
*L'onglet de la cellule mélodique.*

## 9. Les riffs : le piano roll

Un **riff** contient de vraies notes. Son éditeur est un piano roll qui connaît l'harmonie.

![L'éditeur de riff](images/riff.png)
*Un riff sur F♯m9 : sa première note, F♯4 ; les notes de l'accord teintées.*

- Au-dessus des notes, **les accords** sous le riff. Dans les rangées, **les notes de l'accord sont
  ombrées** dans la couleur de sa fonction (la fondamentale plus fort), la **gamme** est plus claire que
  les notes hors gamme : on voit d'un coup d'œil quelles notes « vont bien ».
- La grille a l'apparence de Koton Studio pour Windows : **un carré par tranche** (26 pixels), la première
  tranche de chaque temps plus claire, les rangées de **C** plus claires et celles des touches noires plus
  sombres, chaque rangée nommée à gauche, les notes en turquoise. L'éditeur s'ouvre sur la **première
  note** du riff ; `Shift`+molette ou la barre du bas avance dans le temps, `Ctrl`+molette rétrécit ou
  élargit les carrés.
- **Draw** (dessiner), **Select** (sélectionner), **Erase** (gommer) : l'outil. (Un clic droit gomme
  toujours.)
- **Snap** : où commencent les notes — *Bar* (mesure) … *1/32*, les triolets, *Off*. **Length** : la
  longueur d'une nouvelle note.
- **Beats** : la longueur du riff. **Name** : tapez-le puis `Enter`.
- **−12**, **−1**, **+1**, **+12** : transposer les notes sélectionnées, ou toutes, d'une octave ou d'un
  demi-ton.
- **Fit to the chords** (ajuster aux accords) : chaque note va sur la note la plus proche de l'accord qui
  est dessous.
- **Quantise** (quantifier) : le début des notes placé sur l'aimantation.
- **Step record** (saisie pas à pas) : voir la section 14.4.
- **Clear**.

Cliquez sur une touche du clavier de gauche pour entendre la note. Pour dessiner, voir la section 13.

> **Remarque.** Un riff peut être joué par plusieurs blocs (après un **Duplicate** du bloc, la copie a son
> propre riff ; un riff ouvert depuis un `.sq` de Koton peut être partagé). Changer ses notes change tous
> les blocs qui le jouent.

## 10. La batterie

![L'éditeur de batterie](images/drums.png)
*Un bloc de batterie avec un motif du catalogue.*

- **Category** et **Motif** : le **catalogue** — *Standard* (rock, pop, funk, disco, swing jazz, shuffle,
  bossa nova, half-time, hip-hop, marche, reggae, valse, punk, ballade, trap…), *Africa* (Afrique),
  *Australia* (Australie) — et **Custom (drawn)** avec les motifs que vous avez enregistrés.
- **Customise (draw it)** : le motif copié dans une grille modifiable (ci-dessous).
- **Density** (densité : *Light*, *Normal*, *Dense*) et **A fill on the last bar** (un break sur la
  dernière mesure) : pour le groove intégré d'un nouveau bloc, avant qu'un motif soit choisi.
- **Beats / bar** et **Repeats** : la longueur de la mesure et le nombre de répétitions.

Le kit de batterie est celui de **la piste** : choisissez-le dans son en-tête.

### 10.1 Un groove dessiné

![Un groove dessiné](images/drumgrid.png)
*Un groove personnalisé : une rangée par percussion.*

Les rangées sont les 47 percussions General MIDI — grosse caisse, caisse claire, charleys, toms, cymbales,
puis les percussions latines —, colorées par famille. La grille a l'apparence de Koton Studio pour
Windows : **un carré par pas**, dans la couleur de sa rangée (sombre quand il est vide, le premier pas de
chaque temps plus clair ; vif quand il joue). **Un clic pose un coup, un clic sur un coup l'enlève.**
**Customise** dessine le groove sur la grille la plus grossière qui garde chaque coup à sa place — 4 pas par
temps pour un groove en doubles croches ; **Resolution** (résolution) la change (24 / beat pour le
placement le plus fin). **Save motif…** (il apparaît ensuite sous *Custom (drawn)*), **Clear**.

### 10.2 Les rythmes euclidiens

La rangée **Euclid** écrit un **rythme euclidien** sur une percussion : *k* coups répartis aussi
régulièrement que possible sur *n* pas — E(3,8) est le *tresillo*, E(5,8) le *cinquillo*. Choisissez la
percussion, **E(** coups **,** pas **)**, une **rot**ation et le pas (*Eighths* : croches, *Sixteenths* :
doubles croches, *Eighth triplets* : triolets de croches), puis **Apply**. La ligne en dessous montre le
motif (● un coup, · un silence), son nom quand il en a un, et combien de coups tombent **sur les temps**.
**<** et **>** décalent les coups de cette percussion d'un pas plus tôt ou plus tard.

## 11. Les lignes mélodiques

Une **ligne mélodique** est une mélodie dont **vous ne dessinez que le rythme** : Koton choisit les
hauteurs dans l'harmonie — une note de l'accord sur les temps forts, des notes de passage entre, une
forme que vous choisissez.

![L'éditeur de ligne mélodique](images/line.png)
*Une ligne mélodique.*

- **Voices** : une à trois voix, chacune une rangée de la grille.
- **Beats** : la longueur de la ligne.
- **Contour** : la forme — *Wave (arcs)* (vagues), *Rising* (montante), *Falling* (descendante), *Static
  (pivot)*, *Zigzag*, *Random* (aléatoire), *Thue-Morse*, *L-system*, *Fractal (1/f)*.
- **Anchor** (ancrage) : la première note — la plus proche, ou la fondamentale, la tierce, la quinte, la
  septième, la neuvième de l'accord.
- **Variation** : *Split* (couper les notes longues), *Gate* (aérer), *Retrograde* (rétrograde), *Mirror*
  (miroir, renversement).
- **Motif** : un rythme que vous avez enregistré (**Save motif…**) ou *Custom*. **Apply to the lines with
  this motif** donne ce rythme à toutes les lignes du morceau qui l'utilisent, sauf celles marquées
  **Preserve** (préserver).
- **Its shape** (sa forme) : **Continuity** (continuité : une conduite des voix douce d'un bloc au
  suivant), **Tension** (le registre qui monte ou descend, en demi-tons), **Amplitude** (l'ambitus, en
  demi-tons), **Ornaments** (ornements : appoggiatures, retards), **Wave** (notes par arc ; 0 :
  automatique).
- La rangée **Euclid**, comme pour la batterie (section 10.2), pour une voix.

## 12. Les polyrythmes : les anneaux

Trois sortes de blocs empilent des **anneaux** : chaque anneau est un rythme euclidien E(coups, pas) qui
tourne sur le même **cycle**. Des anneaux de longueurs différentes — 3 contre 4, 5 contre 8 — font un
polyrythme qui se rejoint à la fin du cycle.

### 12.1 Les anneaux de batterie (Polyrhythm)

![Un polyrythme](images/poly.png)
*Trois anneaux : E(3,8), E(5,12), E(7,16).*

- **Cycle (beats)** et **Repeats**.
- **La roue** : les anneaux, le plus grand d'abord. **Cliquez un anneau pour le choisir ; sur l'anneau
  choisi, cliquez un pas pour poser ou enlever un coup** — l'anneau devient alors *dessiné* ; changer ses
  coups, ses pas ou sa rotation le rend de nouveau euclidien. Pendant la lecture, une aiguille tourne
  avec lui.
- **Rings** (anneaux) : la liste, chacun avec son motif ; **M** le rend muet. **Add a ring** (ajouter),
  **Remove it** (retirer).
- **L'anneau choisi** : **Hits** (coups), **Steps** (pas), **Rot.**ation, **Muted** (muet), sa **Lane** (la
  percussion) et une percussion d'**Accent** pour son premier coup.

### 12.2 Les anneaux mélodiques

La même chose, pour une mélodie : chaque anneau donne le rythme d'une **Voice** (1 à 3), avec une
**Octave** et le **Legato** (une note tenue jusqu'à la suivante). Les hauteurs viennent de l'harmonie,
comme pour une ligne mélodique.

![Les anneaux mélodiques](images/rings.png)
*Des anneaux mélodiques.*

### 12.3 Les accords polyrythmiques

Un bloc **Poly chords** porte **ses propres accords** et joue leurs notes en anneaux.

![Les accords polyrythmiques](images/polychord.png)
*Des accords polyrythmiques.*

- **Mode** : *One ring per chord tone* (un anneau par note de l'accord : chaque anneau joue une **Tone** de
  l'accord) ou *One ring sweeping the tones* (chaque anneau parcourt les notes selon un **Contour**, avec
  une **Seed**, une graine).
- **On a new chord** : où un anneau reprend — *Nearest* (la plus proche), *Lowest* (la plus grave),
  *Highest* (la plus aiguë), *Root*, *Third*, *Fifth* (fondamentale, tierce, quinte).
- **Length**, **Cycle**, **Octave**, **Open voicing**, **Voice leading**.
- **Emergent melody** (mélodie émergente) : quand plusieurs anneaux frappent ensemble, un seul s'entend —
  la *Highest note*, la *Lowest*, *Auto (voice leading)* ou *Random* (avec une **Seed**, et **Avoid
  repeated notes** : éviter les notes répétées) : les anneaux deviennent une seule mélodie.
- **Its chords** (ses accords) : cliquez un accord pour le modifier — **Degree**, **Root**, **Colour**,
  **Suspension**, **Force**, **Beats** comme dans l'éditeur d'accord. **Add a chord**, **Remove it**.

## 13. Dessiner dans une grille

La grille d'accompagnement, la cellule mélodique, les grooves dessinés, le rythme des lignes mélodiques et
le piano roll fonctionnent de la même façon :

- **Cliquez** une case vide : une note (pour la batterie : un coup). **Glissez** en gardant le bouton
  enfoncé : sa longueur.
- **Tirez une note** pour la déplacer ; **tirez son extrémité droite** pour l'allonger ou la raccourcir.
- **Clic droit** sur une note : elle est effacée (ou l'outil **Erase**).
- `Del` supprime la ou les notes sélectionnées ; `←` `→` `↑` `↓` les déplacent.
- La **molette** fait défiler les rangées, `Shift`+molette défile dans le temps, `Ctrl`+molette zoome.
- Les colonnes sont groupées par temps et par mesures (les barres de mesure plus marquées).

Chaque geste est une étape d'**Undo**.

## 14. Écouter et jouer

### 14.1 Le transport

- **Lecture / Arrêt** (`Space`) : joue depuis le curseur de lecture (le triangle jaune sur la règle ;
  cliquez la règle pour le déplacer). Pendant la lecture, un clic sur la règle y saute.
- **Arrêt** (`Esc`) : arrête ; une seconde fois, le curseur revient au début.
- **Retour au début** (`Home`).
- **Boucle** (`Ctrl+L`) — voir la section 6.4.
- **Métronome** : un clic à chaque temps pendant la lecture.

### 14.2 Listen

Chaque éditeur a un bouton **Listen** en haut à droite : le bloc seul, en boucle, avec le son de sa piste.
Modifiez le bloc pendant qu'il joue : vous entendez le changement tout de suite. **Stop** l'arrête
(lancer la lecture du morceau aussi).

### 14.3 Un clavier MIDI

Branchez un clavier MIDI USB (avant ou pendant que Koton tourne) : il joue avec le son de la **piste
sélectionnée**.

### 14.4 La saisie pas à pas

Dans l'éditeur d'un riff, cochez **Step record** : chaque note jouée sur le clavier MIDI est écrite au
curseur (le repère rouge au-dessus de la grille), avec la **Length** choisie, et le curseur avance
d'autant. Des notes jouées ensemble forment un accord. À la fin du riff, le curseur repart au début.

### 14.5 La latence

Le moteur tourne sur le troisième cœur du Pi, environ 40 ms avant vos oreilles. Une piste jouée par un
**plugin instrument** démarre environ 0,1 s plus tard, et une note jouée en direct dessus s'entend avec
0,1 s de retard ; un **plugin d'effet** retarde tout le mix de 0,1 s pour que tout reste ensemble
(section 15.3).

## 15. Le son : SoundFont, chaîne de son, plugins

### 15.1 La SoundFont

Les instruments de Koton viennent d'une **SoundFont** (`.sf2`) : des échantillons enregistrés des 128
instruments General MIDI et des kits de batterie. La carte contient **GeneralUser GS**. Koton charge le
premier `.sf2` de `SD:/koton/soundfonts` ; **File ▸ SoundFont…** en choisit un autre (utilisé au prochain
démarrage), et **File ▸ Get a SoundFont…** retélécharge GeneralUser GS s'il manque (le réseau doit être
actif).

### 15.2 La chaîne de son

Le panneau à droite de l'éditeur montre la chaîne de la **piste sélectionnée** :

![La chaîne de son](images/accomp-chain.png)
*La chaîne de son d'une piste.*

- **Instrument** : son son — *SF2* (un instrument de la SoundFont), *Kit* (un kit de batterie) ou
  *Plugin*. **Change** ouvre le choix (section 5.2) ; **Edit** ouvre la fenêtre d'un plugin instrument.
- **Les effets** (jusqu'à quatre, dans l'ordre) : chacun avec une case marche/arrêt, **Edit** — un premier
  clic montre ses principaux boutons dans le panneau, un second ouvre sa propre fenêtre —, et **x** pour
  le retirer. **+ Add an effect** liste les plugins d'effet.
- **Reverb** : la part de la piste envoyée à la réverbération du morceau (double-clic : la valeur par
  défaut).
- **L'indicateur de niveau** de la piste.

### 15.3 Les plugins

Les **plugins** sont des programmes à part que Koton démarre quand un morceau les utilise : des
**instruments** (jouent une piste à la place de la SoundFont), des **effets** (traitent le son d'une piste)
et des **générateurs** (écrivent les notes d'un bloc). Ils se trouvent dans `SD:/koton/plugins` ; les onze
fournis avec Koton sont décrits à l'annexe D. La section **Plugins** du navigateur les liste : un clic pose
un instrument sur la piste sélectionnée, un effet dans sa chaîne, ou un bloc générateur sur elle.

![La fenêtre d'un plugin : FM 2-op](images/plugin-fm2.png)
*La fenêtre de l'instrument FM 2-op.*

![La fenêtre d'un générateur : l'arpégiateur](images/plugin-arp.png)
*La fenêtre de l'arpégiateur.*

La fenêtre d'un plugin s'ouvre par-dessus Koton ; déplacez-la par sa barre de titre, fermez-la par son
**x**. Ses réglages sont enregistrés dans le morceau.

### 15.4 Quand un plugin s'arrête

Si le programme d'un plugin s'arrête (un bug, ou arrêté depuis le gestionnaire de tâches), la barre d'état
le signale : la piste continue sans lui — la piste d'un instrument se tait, un effet est contourné. Dans la
chaîne de son, son bouton **Edit** devient **Restart** (redémarrer).

## 16. Composer avec l'IA

**Compose with AI…** (la barre de transport, le menu **AI**, ou la section **AI** du navigateur) demande à
un grand modèle de langage d'écrire de la musique, puis place sa réponse sur le morceau sous forme de blocs
Koton — accords par degrés, accompagnements, lignes mélodiques ou riffs, batterie —, que vous pouvez
ensuite modifier comme les autres.

![Composer avec l'IA](images/ai.png)
*La boîte de dialogue Compose with AI.*

- **What** (quoi) :
  - **Compose a piece** (composer un morceau) — un morceau entièrement nouveau : il **remplace** le
    morceau actuel (**Undo** le ramène).
  - **Develop the theme (after the end)** (développer le thème) — les mesures suivantes, à partir du
    dernier riff du morceau.
  - **Add an instrument over the song** (ajouter un instrument) — une nouvelle piste qui joue avec le reste.
  - **Add drums over the song** (ajouter une batterie).
  - **A polyrhythmic piece** (un morceau polyrythmique) — lui aussi remplace le morceau.
- **Style** et **Intention** : avec vos mots (*« une bossa mélancolique, un refrain plus lumineux, une fin
  qui ralentit »*).
- **Bars (about)** : le nombre approximatif de mesures.
- Options : **The melody as notes (riffs)** (la mélodie en notes, sinon en lignes mélodiques), **Drums**,
  **The AI voices the chords** (l'IA réalise elle-même les accords), **Poly chords**, **Poly drums**.
- **Provider** (fournisseur) : *Gemini*, *Groq*, *Mistral*, *Claude*, *DeepSeek*, *Grok*, ou un serveur
  compatible OpenAI ; son **modèle** (le modèle habituel est pré-rempli) ; votre **clé d'API**.

**Generate** envoie la demande (par `SD:/bin/llm`) ; une fenêtre montre la progression, **Cancel**
l'interrompt. Cela prend de quelques secondes à une minute. La barre d'état résume ensuite ce qui a été
placé.

**Sans clé** : **Copy the prompt** met toute la demande dans le presse-papiers — collez-la dans n'importe
quelle conversation avec une IA (sur un autre ordinateur, par exemple), copiez sa **réponse entière**, et
revenez à **Paste a reply** (coller une réponse) : Koton la place de la même façon.

La boîte **se souvient de votre dernière demande** : elle se rouvre avec le même style, la même intention, le
même nombre de mesures et les mêmes options (depuis la barre de transport ou le menu **AI**, avec le même type de demande aussi),
même après la fermeture de Koton.

> **Confidentialité.** Le fournisseur, le modèle et la **clé d'API** sont conservés dans
> `SD:/koton/settings.json`, en clair sur la carte. Votre morceau (ses accords et ses parties) est envoyé au
> fournisseur quand vous le demandez.

## 17. Fichiers et export

- **File ▸ New song** (`Ctrl+N`), **Open…** (`Ctrl+O`), **Save** (`Ctrl+S`), **Save As…**. Avant un nouveau
  morceau ou l'ouverture d'un autre, Koton propose d'enregistrer vos changements.
- **`.kson`** est le fichier de Koton (du JSON). Il s'ouvre aussi dans Koton Studio pour Windows.
- Les fichiers **`.sq`** de **Koton Studio pour Windows** s'ouvrent : ce qu'Onyx n'a pas (la partition,
  les plugins VST, certaines automations) est laissé de côté. **Save** écrit alors un `.kson` — le `.sq`
  n'est jamais écrasé.
- **File ▸ Export as WAV…** : tout le morceau rendu dans un fichier WAV (44,1 kHz, 16 bits stéréo), avec
  deux secondes de queue pour la réverbération.
- Un fichier déposé sur la fenêtre est ouvert.

| Où | Quoi |
|---|---|
| `SD:/koton/songs` | vos morceaux ; `demo.kson` est la démonstration |
| `SD:/koton/soundfonts` | les SoundFonts (`.sf2`) |
| `SD:/koton/plugins` | les plugins, un dossier chacun |
| `SD:/koton/settings.json` | la SoundFont choisie, le dernier dossier, le fournisseur, le modèle et la clé de l'IA, votre dernière demande à l'IA |
| `SD:/apps/koton.app/drums.json` | le catalogue de batterie |

## 18. Questions et réponses

**Je n'entends rien.**
Regardez la barre d'état. *No SoundFont* — mettez un `.sf2` dans `SD:/koton/soundfonts` ou utilisez
**File ▸ Get a SoundFont…**. *The sound output is used by another app* — fermez l'autre application qui
joue du son (un émulateur, un lecteur) et relancez Koton. Vérifiez aussi les **M** / **S** des pistes,
leur volume, et le volume d'Onyx (le haut-parleur de la barre de menus).

**Le son craque.**
La barre d'état compte les *underruns* (moments où le moteur a pris du retard). Le moteur devrait tourner
sur le *core 2* : s'il indique *a real-time thread* ou *the UI thread*, une autre application occupe le
cœur libre — fermez-la. Beaucoup de plugins à la fois coûtent aussi.

**Mon accompagnement ne joue rien.**
Il joue les accords de la piste d'accords : il faut des accords sous lui. Ajoutez des accords (section 7) ou
étirez les accords sous le bloc.

**J'ai changé un accord et la mélodie n'a pas suivi.**
Les riffs contiennent des notes fixes : utilisez **Fit to the chords** dans l'éditeur du riff. Les lignes
mélodiques, les accompagnements et les anneaux suivent d'eux-mêmes.

**L'IA a répondu mais rien ne s'est passé.**
La barre d'état ou un message en donne la raison : la réponse n'était pas valable (réessayez), la clé est
fausse, ou le réseau est coupé. **Copy the prompt** / **Paste a reply** fonctionne toujours, même sans
réseau sur le Pi.

**Comment récupérer mes morceaux de Koton Studio ?**
Copiez les fichiers `.sq` sur la carte (le File Viewer, FTP) et ouvrez-les.

## 19. Glossaire

| Terme | Sens |
|---|---|
| **Bloc** | Un générateur posé sur une piste : un accord, un accompagnement, un riff, une batterie, une ligne, des anneaux. |
| **Cadence** | Un enchaînement d'accords qui termine une phrase (V → I : *parfaite* ; IV → I : *plagale*). |
| **Conduite des voix** | Déplacer chaque note d'un accord le moins possible vers l'accord suivant. |
| **Degré** | La place d'un accord dans la tonalité : I (tonique), ii, iii, IV (sous-dominante), V (dominante), vi, vii. |
| **Dominante secondaire** | La dominante d'un autre degré (V/V : la dominante de la dominante). |
| **Fonction** | Ce que fait un accord : tonique (repos), sous-dominante (éloignement), dominante (tension). |
| **Mode** | La gamme de la tonalité : majeur, mineur et les modes anciens. |
| **Piste d'accords** | La piste muette du bas qui porte l'harmonie que lisent toutes les parties. |
| **Renversement** | Un accord dont la basse n'est pas la fondamentale. |
| **Riff** | Un bloc de notes fixes. |
| **Rythme euclidien** | E(k, n) : k coups répartis aussi régulièrement que possible sur n pas. |
| **SoundFont** | Un fichier de sons d'instruments enregistrés (`.sf2`). |

## Annexe A. Les raccourcis clavier

| Touches | Quoi |
|---|---|
| `Space` | Lecture / arrêt |
| `Home` | Retour au début |
| `Esc` | Arrêt (encore : le curseur au début) |
| `Del` | Supprimer le bloc sélectionné (dans une grille : les notes sélectionnées) |
| `Ctrl+D` | Dupliquer le bloc sélectionné |
| `←` `→` | Le bloc précédent / suivant (dans une grille : déplacer les notes sélectionnées) |
| `Ctrl+N` / `Ctrl+O` / `Ctrl+S` | Nouveau morceau / ouvrir / enregistrer |
| `Ctrl+Z` / `Ctrl+Y` | Annuler / rétablir |
| `Ctrl+K` | Tonalité, mesure, tempo du morceau |
| `Ctrl+L` | Boucle oui / non |
| molette, `Shift`+molette, `Ctrl`+molette | Défiler les pistes, défiler dans le temps, zoomer |

## Annexe B. Qualités et couleurs d'accords

La **Colour** d'un accord lié à son degré ajoute des notes à la triade de la gamme sur ce degré :

| Couleur | Ajoute | Sur I en do majeur | Sur ii en do majeur |
|---|---|---|---|
| Triad | — | C | Dm |
| Sixth | la sixte | C6 | Dm6 |
| 7th | la septième de la gamme | Cmaj7 | Dm7 |
| 9th (7+9) | la septième et la neuvième | Cmaj9 | Dm9 |
| 9th (add9) | la neuvième seule | Cadd9 | Dm(add9) |

**Suspension** remplace la tierce par la seconde (*Sus2*) ou la quarte (*Sus4*). **Force** rend l'accord
*Major*, *Minor*, *Augmented*, *Diminished* ou *Dominant*, quoi que dise la gamme (un IV majeur en
mineur, un V de dominante en mineur naturel). Koton connaît 35 qualités : majeur, mineur, diminué,
augmenté, sus2, sus4, maj7, m7, 7, m7♭5, dim7, 6, m6, add9, m(add9), 9, maj9, m9, 7♭9, 7♯9, 11, 13,
maj7♯11, 7sus4, 7sus2, 9sus4, 9sus2, 6sus4, 6sus2, maj7sus4, maj7sus2, maj9sus4, maj9sus2, add9sus4,
7♯5.

## Annexe C. Les styles d'accompagnement

| Style | Style |
|---|---|
| Block chords (held) — accords plaqués tenus | Tango (staccato) |
| Block chords (quarters) — plaqués en noires | Bossa nova / Latin |
| Block chords (eighths) — plaqués en croches | Funk (16th stabs) |
| Arpeggio up — arpège montant | Habanera (bass) |
| Arpeggio up-down — montant-descendant | Ballad (held arpeggio) — ballade |
| Alberti (C-G-E-G) | Country (alternating bass) |
| Jazz comping (Charleston) | Slow rock (12/8 triplets) |
| Rock (eighths) | Arpeggio: 2 eighths + quarter |
| Pop (bass + chord) | Arpeggio: 3 eighths + dotted quarter |
| Blues shuffle (triplets) | Arpeggio: 4 eighths + half |
| Arpeggio down — arpège descendant | Arpeggio: triplet + quarter |
| Arpeggio (eighths) | Arpeggio: 4 eighths + quarter |
| Waltz (bass-chord-chord) — valse | Harp (rolled arpeggio) — harpe |
| Reggae skank (off-beats) | Custom (drawn) — dessiné |
| March (bass-chord) — marche | |

## Annexe D. Les plugins

| Plugin | Genre | Ce qu'il fait | Ses réglages |
|---|---|---|---|
| **FM 2-op** | instrument | FM à deux opérateurs : un modulateur déforme une porteuse sinusoïdale — pianos électriques, cloches, basses. | Ratio, Index, Feedback, Attack, Decay, Sustain, Release, Mod decay, Mod sustain, Detune, Velocity, Volume |
| **Subtractive** | instrument | Deux oscillateurs et du bruit dans un filtre résonant — le synthétiseur classique. | Osc 1, Osc 2, hauteur et accord fin, Osc mix, Noise, Pulse width, Filter, Cutoff, Resonance, Env amount, Key track, les deux enveloppes, Volume |
| **Plucked strings** | instrument | Karplus-Strong : une kora, une harpe, une guitare. | Decay, Damping, Brightness, Pick point, Release, Width, Velocity, Volume |
| **Delay** | effet | Un écho en millisecondes, ping-pong. | Time, Feedback, Tone, Ping-pong, Width, Mix |
| **Reverb** | effet | Une salle (Freeverb). | Room size, Damping, Width, Pre-delay, Wet, Dry |
| **Chorus** | effet | Un chorus jusqu'à trois voix ; avec du feedback, un flanger. | Rate, Depth, Delay, Feedback, Voices, Spread, Mix |
| **EQ 3-band** | effet | Égaliseur : graves, médiums, aigus. | Low, Low freq, Mid, Mid freq, Mid width, High, High freq, Output |
| **Drive** | effet | De la saturation à la fuzz. | Character, Drive, Low cut, Tone, Asymmetry, Mix, Level |
| **Arpeggiator** | générateur | Arpège les accords de la piste d'accords. | Pattern, Notes/beat, Extend, Articulation, Velocity, Octave, Voice leading, Spread, Rhythm |
| **Euclidean melody** | générateur | Un rythme euclidien qui joue les notes des accords selon un contour. | Hits, Steps, Rotation, Steps/beat, Contour, Tones, Octave, Range, Velocity, Accent, Articulation, Seed |
| **Cellular automaton** | générateur | Un automate cellulaire à une dimension (les règles de Wolfram) sur la gamme ou les accords. | Notes/beat, Rule, Width, Scale, Octave, Range, Seed, First row, Density, Velocity, Articulation, Chord-aware |

L'arpégiateur et l'automate cellulaire lisent les blocs des générateurs de Koton Studio pour Windows.
