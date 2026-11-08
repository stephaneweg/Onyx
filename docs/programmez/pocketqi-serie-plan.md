# PocketQI dans *Programmez!* : plan détaillé de la série

*Rédigé le 30 octobre 2026, d'après le dépôt `stephaneweg/PocketQI` (commit `a5d5382`, 28/10/2026), `C:\Projects\Robot\pocket_esp`, `C:\Projects\Robot\pocket` et le portage dans Onyx. Le dossier `C:\Projects\Robot\secrets` n'a pas été ouvert.*

Notation employée :

| Statut | Sens |
|---|---|
| **Carte** | essayé sur la vraie XIAO, avec une trace écrite (README ou commit) |
| **Émulateur / PC** | essayé dans QEMU ou sur Windows, pas de trace sur la carte |
| **Code** | présent et compilé, aucune trace d'essai |
| **Partiel** | une partie seulement marche |
| **Absent** | n'existe pas |

---

## 1. L'état réel du projet

### 1.1 Où est le code

- **La référence est le dépôt GitHub `PocketQI`**, `system/esp` et les modules partagés. Il n'y a pas de clone dans `C:\Projects\Robot` : je l'ai cloné dans mon dossier temporaire.
- **`C:\Projects\Robot\pocket_esp\firmware` n'est pas le firmware** : il ne contient que l'écran Wi-Fi et le pilote de la carte, dans leur état du 25/10. En revanche, `pocket_esp` garde ce qui manque au dépôt : BOM, achats, `CONCEPTION.md`, fichiers d'impression, manuel de montage.
- **`C:\Projects\Robot\pocket` est un autre appareil**, un Pi 4 en boîtier. Il n'est pas le sujet.
- **Le dépôt est privé** : l'API publique de GitHub répond 404. Seuls mes identifiants enregistrés ont permis de le lire.
- **Le cœur est partagé entre les cibles.** `system/windows/src/pocketqi_win.cpp` (2 210 lignes) est compilé tel quel sur l'ESP32 ; `system/esp/components/pocketqi` apporte la plateforme. Le projet garde donc des noms hérités : `groq_proto.c` sert aussi Mistral, `win_mic_read` lit le micro de l'ESP, `galo.bi` est la bibliothèque des apps.

### 1.2 Ce qui existe

| Fonction | Statut | Preuve et réserve |
|---|---|---|
| Micro PDM, filtre passe-haut, gain ×4 | **Carte** (26/10) | `traiter_son`, `plateforme_esp.c:250` |
| Découpe des phrases par énergie, en local | **Carte**, en partie | `pocket_ecoute_block` ; fin de phrase réglée au banc PC. Bruit fort jamais mesuré sur la XIAO |
| Réveil au bouton et au toucher | **Carte** (26/10) | `pf_basculer_eveil` ; endormi, le micro rend des zéros |
| Mot de réveil « dis Pocket » sur l'ESP | **Absent** par défaut | PocketSphinx ne tient pas (19 Mo de tas, `kws_esp.c`). Il existe sur PC et sur Pi |
| Petit réseau de réveil (`-eveil-moteur reseau`) | **Code** | 200 Ko ; bon seulement enrôlé sur une voix, mauvais dans le bruit (`modules/eveil/README.md`) |
| Transcription Voxtral | **Émulateur** de bout en bout | sur la carte, seul l'encodage Opus est mesuré. **Langue fixée à `fr`** (`https_esp.c:356`) |
| Dialogue LLM, sortie JSON, mémoire à marqueurs | **Émulateur / PC** | `groq_proto.c`, `pocketqi_win.cpp` |
| Voix Voxtral en flux, MAX98357A | **Carte** (27-28/10) | validé à l'oreille, deux amplis |
| Visage animé | **Carte** | coût réel par image **à mesurer** ; ordre RGB encore listé « à vérifier » |
| Sous-titres | **Code** | `ecran_visage_sous_titre` ; `FONCTIONNALITES.md` les donne encore pour à faire |
| Caméra, aperçu, annulation, photo de test | **Carte** (27/10) | capteur OV3660 |
| Envoi d'une photo au doigt, sens et couleurs de l'aperçu | **Partiel** | restent à vérifier (`system/esp/README.md`) ; chaîne complète essayée sur PC (`POCKET_PHOTO`) |
| Apps BASIC (agenda, météo, courses, à faire, carnet…) | **Émulateur / PC** | sur la XIAO, il faut la microSD : aucune trace d'essai |
| Mail (assistant à l'écran, Gmail) | **Émulateur** | README : « Pas essayé : Outlook réel, la carte » |
| Boucle de seconds tours, refus, `Fail` | **PC** | banc réel : 50/52 (commit `49edb74`) ; 3 tours au plus |
| PocketQI Studio (BASIC, blocs) | **Émulateur** ; **Pi via Onyx** | README ESP : « Pas essayé sur la carte » |
| Émulateur QEMU du Pocket | **Émulateur** | sans caméra |
| Molette | **Absent en pratique** | le tactile occupe ses broches par défaut (`GX_TACTILE_SUR_MOLETTE = 1`) |
| Boîtier imprimé | **À confirmer** | fichiers dans `pocket_esp/impression`, hors dépôt, au nom de Galo. Rien n'indique qu'il soit monté |
| Autonomie | **Absent** | tous les chiffres sont des estimations `[S]`. Rétroéclairage non coupable, pas de jauge |
| Veille, OTA, QR codes, reconnaissance faciale | **Absent** | idées seulement |

### 1.3 Les écarts avec la proposition faite au magazine

| La proposition dit | La réalité | Ce que je propose |
|---|---|---|
| « Rien de personnel n'est envoyé au LLM » | **Faux tel quel.** Voir section 2 | reformuler ; c'est le point le plus important de ce plan |
| Le découpage ne cite pas la voix de sortie | sans synthèse, la partie 1 ne donne pas un assistant qui répond | ajouter la voix à la partie 1 |
| « Mistral » | quatre services : Voxtral (transcription), Ministral 14B (dialogue), Voxtral TTS (voix), Pixtral (vision) ; Groq en secours si une clé est posée | les présenter comme tels |
| Boucle d'appels d'outils | pas d'API `tools` native : du JSON imposé (`response_format`), une liste d'`outcomes`, et des seconds tours gérés par le shell | l'expliquer ainsi, c'est plus instructif |
| « écoute » | réveil au bouton ou au toucher sur l'ESP | le dire dès la partie 1 ; « dis Pocket » reste sur PC et Pi |
| Lancer des apps BASIC | l'agenda, le mail et la météo existent, mais non vérifiés sur la carte | les vérifier avant d'écrire la partie 3 |
| Code libre pour reproduire | dépôt privé, sans licence, boîtier et câblage hors dépôt ou périmés | section 6 et 7 |

`xiaozhi-esp32` n'apparaît que dans `modules/visage` : les petits signes autour des yeux sont faits « d'après Otto de xiaozhi ». Aucun code de ce projet n'est dans le dépôt, d'après une recherche par nom.

---

## 2. Le fil rouge : ce que fait réellement le code

La phrase de la proposition confond deux choses : **le modèle de langage** et **le fournisseur**. Le masquage protège bien la première, pas la seconde. Mistral reçoit la voix et tout ce que Pocket prononce.

### 2.1 Ce qui part, et vers qui

Configuration de l'ESP (`esp/sdcard/pocketqi.cfg`) : transcription puis LLM, voix `mistral`. PicoTTS n'existe pas sur l'ESP.

| Donnée | Destinataire | Masquée ? | Où le voir |
|---|---|---|---|
| Le son de chaque phrase, éveillé | Mistral (transcription) | **Non**, par nature | `voxtral_transcribe` |
| La phrase écrite | Mistral (LLM), Groq en secours | **Oui, pour les seules valeurs déjà connues** et écrites à l'identique | `groq_mask` |
| La première fois qu'une valeur est dite (« je m'appelle Léa ») | LLM | **Non** : c'est le LLM qui la renvoie par `retenir` | `groq_parse_response`, `README` Windows |
| Un nom ou un fait jamais mémorisé | LLM | **Non** | `groq_mask` ne connaît que `memoire.txt` |
| Une adresse ou un numéro dictés au carnet | LLM | **Non** : le LLM les passe en argument de `cardbook set` | `sd/apps/cardbook/README.md` |
| Ce que Pocket dit, **valeurs remises en clair** | Mistral (TTS) | **Non** | `sayLang` → `speechQueue` → `mistral_tts_say` |
| **Le texte d'un mail lu à voix haute** | Mistral (TTS) | **Non**, jamais au LLM mais bien chez Mistral | `apps/mail/main.bas`, `Say para$` |
| Expéditeurs et sujets (`mail check`, `list`, en `askinfo`) | LLM | seulement les valeurs de la mémoire | choix écrit dans `apps/mail/README.md` |
| Agenda, tâches, courses demandés en `askinfo` | LLM | seulement les valeurs de la mémoire | `groq_build_request`, `tool_result` |
| Photo VGA et question | Mistral (vision) | **Non** ; la question est démasquée | `decrirePhoto`, boucle de `run_outcomes` |
| Ville, sujet Wikipédia | Open-Meteo, Wikipédia | valeur en clair, c'est le but | `apps/meteo/main.bas`, `cmd_wiki` |

### 2.2 Ce qui est vrai, et peut être affirmé

- `memoire.txt` reste sur la carte. Le modèle ne voit jamais la liste des valeurs, seulement leurs marqueurs (`{nom}`), et l'historique est gardé en marqueurs.
- Le corps d'un mail n'atteint jamais le LLM : en `askinfo`, `MAIL:INBOX/<uid>` et `MAIL:SEND` sont refusés (403), et une app qui a lu des mails ne renvoie pas son échec au LLM.
- En `askinfo`, le carnet ne donne ni adresse mail, ni numéro, ni adresse postale.
- Un mail n'est envoyé qu'après un oui. Le mot de passe n'est jamais journalisé.
- Endormi, le micro ne rend que des zéros : rien ne part.
- La caméra n'est allumée que pendant l'aperçu. La photo ne part que si la personne appuie sur Envoyer.
- La conversation est oubliée après 10 minutes sans parler, et 6 échanges au plus sont renvoyés.
- Le LLM ne peut demander que des actions déclarées : liste blanche et contrôle des arguments.
- Sur l'ESP, les certificats sont vérifiés par le paquet d'ESP-IDF. Le README note toutefois que les dates ne le sont pas.

### 2.3 La phrase que je propose

> « Pocket ne donne au modèle de langage que des marqueurs pour ce qu'il a appris sur vous, et jamais le corps de vos mails. Mais il ne peut pas être privé de tout : votre voix, et tout ce qu'il dit à voix haute, passent par les serveurs de Mistral. Cette série montre où passe la frontière, ligne de code à l'appui. »

Ce fil rouge est **plus fort** que la promesse initiale : le lecteur développeur préférera un inventaire exact à un slogan. Chaque partie se terminera par le même encadré, **« Ce qui est sorti de l'appareil »**, relevé sur la console.

### 2.4 Le réglage côté fournisseur

Les pages d'aide de Mistral indiquent que les données du mode gratuit de Studio peuvent servir à l'entraînement, avec une désinscription possible dans le menu *Privacy* de la console. Pour l'offre payante, le texte que j'ai pu lire dit que l'utilisateur garde le contrôle, sans préciser le réglage par défaut. **La série doit faire désactiver ce réglage par le lecteur dès la création de la clé.** La conservation de 30 jours et les conditions de rétention nulle ne viennent que de sites tiers : à vérifier dans les conditions officielles avant d'imprimer un chiffre. Groq : même prudence, les sources disent qu'il n'entraîne pas sur les données de l'API, mais je n'ai pas lu son contrat.

### 2.5 Petites améliorations possibles avant publication

À décider par l'auteur ; aucune n'est faite.

1. **Filtre local** avant le LLM : reconnaître mails et numéros par motif, et les remplacer par un marqueur. Peu de code, excellent exemple pour la partie 3.
2. **Corriger le commentaire de `groq_proto.h`**, qui dit encore que « la reconnaissance vocale, elle, reste locale ».
3. **Serveur local** à la place de Mistral (LAN) : piste de fin de série. Le port Onyx a déjà un `config.ini url =` ; pas l'ESP.
4. **PicoTTS sur l'ESP** pour lire les mails sans les envoyer : étude existante (G1 de `FONCTIONNALITES.md`), plusieurs jours de travail.

---

## 3. Le découpage retenu

Je garde les trois parties acceptées par le rédacteur en chef, avec deux changements :

- **la voix de sortie entre dans la partie 1**, pour que le lecteur ait un assistant complet dès le premier numéro ;
- **la boucle d'outils se construit en deux temps** : le JSON en partie 1, le second tour en partie 2 (la caméra en est l'exemple le plus simple), les apps et la correction d'erreurs en partie 3.

| Partie | Titre de travail | Pages | Signes | À la fin, le lecteur… |
|---|---|---|---|---|
| 1 | Écouter, comprendre, répondre | 6 | 27 000 | parle à son Pocket, qui lui répond à voix haute |
| 2 | Voir et montrer | 5 | 22 500 | voit le visage, lit les sous-titres, fait identifier un objet |
| 3 | Agir et étendre | 6 | 27 000 | écrit sa propre app BASIC, que le LLM sait appeler |

Si le magazine impose 4 pages par partie (18 000 signes), voir la ligne « à couper » de chaque partie. Le découpage tient aussi en deux parties, mais je le déconseille : la partie 3 perdrait son sujet.

---

## 4. Le plan de chaque partie

### Partie 1 : Écouter, comprendre, répondre

**Promesse au lecteur.** « Pour moins de 100 euros et un après-midi de soudure, construisez un petit appareil qui vous écoute, comprend et vous répond. Vous saurez ce que chaque étape coûte en millisecondes, et ce qu'elle laisse sortir. »

**Plan par intertitres.**

| Intertitre | Signes |
|---|---|
| Introduction : un assistant, c'est une boucle | 1 500 |
| Quatre services, deux cœurs, 8 Mo de PSRAM | 3 000 |
| Le matériel et son câblage | 3 500 |
| Du micro à la phrase | 4 500 |
| Reconnaître la parole : pourquoi pas en local | 3 000 |
| Parler à un LLM : une API sans mémoire | 4 500 |
| Répondre à voix haute, en flux | 3 000 |
| Mesures, pièges et encadré « Ce qui est sorti » | 3 000 |
| Avec Claude, et la suite | 1 000 |

**Volet théorique.**

- Ce qu'est un signal audio numérique : 16 kHz, 16 bits, PDM et PCM, composante continue, dBFS.
- Détecter la parole sans IA : énergie, plancher de bruit, fin de phrase après 0,7 s de silence.
- Pourquoi la transcription part dans le nuage sur cette puce : ordres de grandeur mémoire, 19 Mo de PocketSphinx contre 8 Mo de PSRAM.
- Ce qu'est un LLM vu du développeur : une fonction sans état, à qui l'on renvoie à chaque appel le prompt système et l'historique ; tokens, température, sortie JSON imposée.
- La synthèse en flux : SSE, base64, PCM flottant 24 kHz, mémoire tampon et sous-alimentation.
- Le coût caché du TLS : la poignée de main, et l'intérêt de garder les connexions ouvertes.

**Volet pratique.**

1. Câbler sur table et flasher.
2. Vérifier le micro : `/micro`, attendu entre −26 et −35 dBFS en parlant.
3. Poser les secrets : `/wifi`, `/cle mistral`. Sans microSD, Pocket fonctionne sans les apps : idéal pour cette partie.
4. Lire dans la console la phrase reconnue, le JSON reçu, les temps.
5. **Sans matériel** : les mêmes étapes avec `pocketqi_win` sous Windows, ou l'émulateur QEMU.

**Extraits de code réels.**

| Fichier | Fonction | Pour montrer |
|---|---|---|
| `esp/components/pocketqi/plateforme_esp.c` | `traiter_son` | le filtre passe-haut, une correction de 10 lignes |
| `modules/eveil/src/pocket_ecoute.c` | `pocket_ecoute_block` | l'automate d'écoute |
| `esp/components/pocketqi/https_esp.c` | `voxtral_transcribe` | un multipart écrit à la main |
| `modules/llm/groq_proto.c` | `groq_build_request`, `groq_parse_response` | la requête et la lecture du JSON |
| `windows/src/pocketqi_win.cpp` | `ask` | le fournisseur, le secours, l'attente meublée |
| `esp/components/pocketqi/voix_mistral_esp.c` | `mistral_tts_say`, `ligne`, `tache_joue` | le flux SSE et sa file de 30 s |
| `esp/components/pocketqi/son_esp.c` | `son_preparer` | quatre lignes qui suppriment les crépitements |

**Schémas et captures.** Schéma de la boucle avec ses temps ; schéma de câblage **à refaire** ; photo du montage sur table ; capture de console annotée ; répartition de la latence en barres.

**Mesures à relever sur la carte** (elles n'existent pas encore).

- Latence fin de phrase → premier son, sur 20 phrases : médiane et pire cas, en Wi-Fi et sur partage de connexion.
- Répartition : transcription, LLM, premier son.
- WAV contre Ogg Opus sur la carte : taille, temps d'encodage, temps d'envoi.
- Mémoire libre, interne et PSRAM, au repos et au plus fort.
- Consommation au multimètre : éveillé, endormi.

Chiffres déjà disponibles, à citer avec leur origine : prêt en 3,9 s après l'allumage (carte) ; Opus 3,5 s d'encodage pour 3 s de parole (carte) ; émulateur : TLS gardé, 11,7 s → 6,1 s. **Les temps de l'émulateur ne valent pas pour la puce.**

**Pièges à raconter** (tous vécus, tous documentés).

- La parole jamais détectée : la composante continue du micro.
- La note saturée même à 5 % de volume : broches I²S flottantes, fronts qui rebondissent sur 20 cm de Dupont.
- 6,2 s de voix livrées en 11,7 s : fenêtre TCP trop petite.
- Le modèle audio direct, plus rapide, qui recopiait des réponses passées : retour à deux appels.
- La corruption de tas sur reprise de session TLS, trouvée à l'émulateur et au tas empoisonné.
- La phrase de 25 s déclenchée par le bruit au réveil : les zéros faisaient chuter le plancher.

**À couper si 4 pages.** La théorie PDM, le TLS, la répartition fine de la mémoire.

---

### Partie 2 : Voir et montrer

**Promesse au lecteur.** « Donnez un visage à votre assistant, faites-lui afficher ce qu'il dit, puis montrez-lui un objet : il vous dira ce que c'est. Au passage, vous aurez compris votre premier tour d'outil. »

**Plan par intertitres.**

| Intertitre | Signes |
|---|---|
| Introduction : un visage rend l'attente supportable | 1 500 |
| Dessiner des yeux avec des distances signées | 4 000 |
| Une humeur, deux axes, des ressorts | 3 000 |
| 20 images par seconde sur un bus partagé | 4 000 |
| Des sous-titres au rythme de la lecture | 1 500 |
| La caméra : du capteur au JPEG | 2 500 |
| Le LLM ne voit pas : le premier second tour | 3 000 |
| Mesures, pièges, encadré « Ce qui est sorti » | 2 000 |
| Avec Claude, et la suite | 1 000 |

**Volet théorique.**

- Distances signées : décrire une forme par une fonction, mêler deux formes en continu.
- Animer par ressorts plutôt que par images-clés : une transition qui survit à une interruption.
- Un modèle d'émotion à deux axes (valence, éveil) ; pourquoi l'humeur est le premier outcome.
- Budget d'une image : 50 ms à 20 images/s ; DMA, SPI, double tampon ; FreeRTOS, priorités, épinglage sur un cœur.
- Modèles multimodaux : une image base64 dans un message, ce qu'un modèle de vision sait et ne sait pas dire.
- **Le second tour** : le modèle demande, le programme exécute, le résultat revient. C'est le principe des appels d'outils, sans l'API `tools`.

**Volet pratique.**

1. Brancher l'écran et vérifier le contrôleur tactile : Pocket détecte CST328 ou CST3530.
2. Demander « sois triste », « sois en colère » : voir l'humeur changer.
3. Lire les sous-titres ; lire le coût par image dans le journal.
4. Tester la caméra avec `/photo-test` et `esp/outils/photo_test.py`.
5. Dire « qu'est-ce que c'est ? », viser, envoyer.

**Extraits de code réels.**

| Fichier | Fonction | Pour montrer |
|---|---|---|
| `modules/visage/src/pocket_visage.c` | `pocket_visage_step`, `pocket_visage_lignes_rgb565` | les ressorts, le dessin ligne à ligne |
| `esp/components/pocketqi/ecran_visage.c` | `image`, `ecran_visage_sous_titre`, `montrer_sous_titres` | la boucle d'affichage, 14 caractères/s |
| `esp/components/pocket_xiao/pocket_xiao.c` | `gx_bus_prendre`, `gx_bus_rendre` | le jeton du bus SPI |
| `esp/components/pocketqi/pocketqi_esp.cpp` | `tache_sd`, `regler_fils` | les priorités et les cœurs |
| `esp/components/pocketqi/camera_esp.c` | `camera_allumer`, `camera_taille` | allumée seulement si elle sert |
| `esp/components/pocketqi/ecran_photo.c` | `ecran_photo_prendre` | l'aperçu et le geste d'envoi |
| `windows/src/pocketqi_win.cpp` | `decrirePhoto`, branche `camview` de `run_outcomes` | la photo, puis le second tour |

**Schémas et captures.** Planche des humeurs (`out/planche.png`, à régénérer avec `visage_test planche.png` : `out/` n'est pas dans le dépôt) ; plan valence × éveil ; chronogramme SPI écran / microSD ; séquence du second tour ; photos réelles de l'écran : visage, sous-titres, aperçu ; un exemple d'identification réussi et un raté.

**Mesures à relever.**

- Coût réel d'une image sur la puce (estimé à 10-25 ms, **jamais mesuré**).
- Images/s pendant une requête TLS et pendant une lecture microSD. Le chiffre de 21 images/s vient de l'émulateur.
- Aperçu caméra : 680 images en 60 s, soit ~11 images/s (carte).
- Latence complète d'une identification ; taille du JPEG VGA.
- Taux de réussite sur 20 objets : Pixtral, puis Ministral en secours.

**Pièges à raconter.**

- Le tactile reçu n'est pas celui de la fiche : CST3530 à 0x58 au lieu de CST328 à 0x1A.
- Les pastilles MTDO/MTCK recouvertes par la carte Sense : le tactile prend les broches de la molette.
- L'assert dans `spi_hal_setup_trans` : la microSD qui parle en plein envoi d'image.
- Le blocage d'un essai sur dix, trouvé à l'émulateur et à gdb.
- 0 image/s pendant 50 à 80 s, à cause d'une priorité.
- « FB-OVF » : la caméra doit démarrer en VGA avant de descendre en 240 × 240.
- Le port série qui redémarre la carte par DTR/RTS.

**Encadré vie privée.** La photo part entière, avec ce qu'il y a derrière l'objet. Pixtral décrit un visage mais ne dit pas qui c'est ; la reconnaissance faciale locale (ESP-WHO) reste une idée, pas une fonction.

**À couper si 4 pages.** Le modèle à deux axes, les détails de FreeRTOS.

---

### Partie 3 : Agir et étendre

**Promesse au lecteur.** « Votre assistant parle ; apprenez-lui à agir. Vous écrirez une application BASIC de 30 lignes, le LLM saura l'appeler, ses erreurs lui seront renvoyées, et vous saurez dire exactement ce que chaque action expose. »

**Plan par intertitres.**

| Intertitre | Signes |
|---|---|
| Introduction : un LLM ne fait rien, il demande | 1 500 |
| Un contrat, pas des fonctions : les outcomes | 3 500 |
| Pourquoi du BASIC sur un ESP32 | 3 000 |
| Le manifeste : décrire une app au modèle | 2 500 |
| Votre première app : la météo, puis la vôtre | 4 000 |
| Vérifier, refuser, corriger : la boucle fermée | 3 500 |
| Mesurer un prompt : le banc de régression | 2 000 |
| La mémoire à marqueurs, et ses limites | 3 000 |
| L'inventaire final de la vie privée | 2 000 |
| Mail, agenda : les garde-fous | 1 000 |
| Et ensuite : Onyx, la vision locale, la veille | 1 000 |

**Volet théorique.**

- Appels d'outils : le principe, les variantes (API `tools`, sortie JSON structurée, MCP). Le projet a choisi une sortie JSON commune aux fournisseurs ; **la raison exacte reste à confirmer par l'auteur**.
- Plusieurs actions en une réponse, et leur ordre.
- Limiter une boucle agentique : 3 seconds tours, jamais deux fois le même appel, pas de question après une question restée sans réponse.
- Une VM comme bac à sable : l'app n'a que des périphériques (`SAY:`, `MAIL:`, `CAMERA:`) et une limite de temps.
- **L'injection de prompt** : le sujet d'un mail ou une page web revient dans le contexte du modèle. Liste blanche et confirmation orale en limitent les effets ; elles ne suppriment pas le risque.
- Évaluer un système non déterministe : une batterie de phrases, plusieurs passages, des critères écrits.

**Volet pratique.**

1. Lire `sd/apps/meteo/app.txt` et `main.bas` (161 lignes), sans clé d'API : Open-Meteo.
2. Écrire une app minimale de 30 lignes, avec `llm.<action>`, `Arg$`, `Say`, `Web$`, `JsonText$`.
3. La tester sans LLM, avec `/run`, puis à la voix.
4. La casser exprès : mauvais type d'argument, `Fail`, et lire le second tour.
5. Installer et lancer une app avec PocketQI Studio. **Studio est sous Windows uniquement.**
6. Relever dans la console, pour une même demande, ce qui part avant et après ajout d'un `/p nom=…`.

**Extraits de code réels.**

| Fichier | Fonction | Pour montrer |
|---|---|---|
| `sd/apps/meteo/app.txt`, `main.bas` | manifeste et programme | une app complète |
| `sd/lib/galo.bi` | `Say`, `Arg$`, `Web$`, `JsonText$`, `Fail`, `CamView$` | la bibliothèque (107 fonctions) |
| `windows/src/pocket_apps.cpp` | `PocketAppsPrompt`, `PocketAppsRun` | du manifeste au prompt, l'exécution |
| `windows/src/pocketqi_win.cpp` | `run_outcomes`, `tourPermis` | la boucle et sa limite |
| `modules/llm/groq_proto.c` | `groq_mask`, `groq_unmask` | la mémoire à marqueurs, 40 lignes |
| `sd/apps/mail/main.bas` et `pocket_apps.cpp` (`askinfo`, 403) | | une garantie imposée par l'hôte, pas par le prompt |
| `windows/essais/regression_prompt.py` | `ESSAIS` | le banc |

**Schémas et captures.** Le contrat outcome → contrôle → app → résultat ; arbre de décision des seconds tours ; capture de PocketQI Studio, blocs et code ; **carte complète des flux de données** (section 2.1 de ce plan) ; photo de l'appareil fini, si le boîtier existe.

**Mesures à relever.**

- Banc complet rejoué sur la version publiée, avec le modèle publié : score et coût.
- Déjà connu : compilation d'une app ~140 ms et ~2 Mo de PSRAM, relance par `.bax` ~9 ms (**émulateur**). À refaire sur la carte.
- Avant / après seconds tours : essais 25 et 26 du banc, 0/4 → 3/4 (commit `49edb74`).
- Taille réelle du prompt et tokens consommés par échange (non journalisés aujourd'hui).
- Un essai d'injection dans le sujet d'un mail, avec `fakemail.py`, jamais contre un vrai compte.

**Pièges à raconter.**

- « Mardi prochain » qui devenait un dimanche, avant de mettre le calendrier dans le prompt.
- Deux dentistes, Léo et Emma : le bon rendez-vous supprimé 2 fois sur 3. **À dire tel quel** : d'où la confirmation orale, à étendre.
- Le faux « 42/46 avant » : un exécutable compilé pendant la modification (commit `ae0f9e2`). Une belle leçon de méthode, vécue avec Claude.
- Google et son flux « code d'appareil » sans droit Gmail : le mot de passe d'application en QR code.
- `rename` sur FAT, qui refuse de remplacer un fichier.
- Un marqueur oublié ou inventé par le modèle : `vague_value`, `groq_unmask`.

**Encadré Onyx (1 500 signes au plus).** Sur son propre système, l'auteur a repris `core/` à l'identique (protocole, humeurs, écoute, visage) : Pocket y écrit dans le Calendrier, les Notes, les Contacts, et « dis Pocket » s'y entend en local avec PocketSphinx. À noter pour rester exact : ce portage ne vérifie pas les certificats TLS (`docs/03`, `net.h`). Renvoi vers l'article sur Onyx, s'il a paru.

**À couper si 4 pages.** Le banc de régression, Studio, l'encadré Onyx.

---

## 5. Matériel, comptes et coût d'usage

### 5.1 Matériel

Prix réels du panier Amazon.com.be du 25/10/2026, à l'unité ou par lot. **À revérifier avant impression.** Les distributeurs spécialisés ou AliExpress sont sans doute moins chers, et les lots laissent beaucoup de pièces inutilisées : je n'ai pas relevé ces prix.

| Article | Rôle | Prix | Sur table | Complet |
|---|---|---|---|---|
| Seeed XIAO ESP32-S3 Sense | cerveau, caméra, micro, microSD, charge | 32,50 € | ✔ | ✔ |
| Waveshare 2,8" LCD tactile | écran | 26,99 € | ✔ | ✔ |
| MAX98357A, lot de 3 | ampli | 6,99 € | ✔ | ✔ |
| HP 8 Ω 1 W 20 × 14, lot de 10 | haut-parleur | 11,66 € | ✔ | ✔ |
| Fils Dupont, lot de 120 | essais | 12,99 € | ✔ | ✔ |
| LiPo 804050, 2 100 mAh | batterie | 19,00 € | | ✔ |
| Interrupteurs, lot de 50 | marche / arrêt | 10,99 € | | ✔ |
| Vis M2,5, assortiment | écran | 7,53 € | | ✔ |
| Encodeurs de souris, lot de 5 | molette | 6,99 € | | ✔ |
| Micro-interrupteurs, lot de 250 | bouton | 6,45 € | | ✔ |
| Fils AWG28 et AWG22 | câblage soudé | 12,72 € | | ✔ |
| **Total** | | | **91,13 €** | **154,81 €** |

À ajouter : une microSD (5 à 8 €, nécessaire dès la partie 3), l'impression du boîtier, un fer à souder. Le bouton BOOT de la XIAO suffit comme bouton « parler » pour commencer, puisque c'est GPIO0.

### 5.2 Comptes et clés

| Service | Obligatoire ? | Usage | À faire |
|---|---|---|---|
| Mistral (Studio) | **oui** | transcription, LLM, voix, vision | créer la clé, **désactiver l'usage pour l'entraînement**, fixer un plafond de dépense si la console le permet |
| Groq | non | secours du LLM | ne pas poser de clé, si l'on veut un seul fournisseur |
| Open-Meteo, Wikipédia | sans clé | météo, encyclopédie | citer Open-Meteo (CC BY 4.0) |
| Gmail / Outlook | non | app mail | mot de passe d'application, partie 3 seulement |

### 5.3 Coût d'usage

Estimation, à remplacer par une mesure. Tarifs relevés le 30/10/2026 sur des **sites tiers** : la page officielle consultée ne donne pas ces lignes. Elle indique en outre une facturation « à la minute » pour la parole, quand les tiers donnent la voix au million de caractères. **À confirmer dans la console Mistral.**

| Poste, pour une question de 5 s et une réponse de 120 caractères | Hypothèse | Coût |
|---|---|---|
| Transcription, 0,0035 $/min | 5 s | 0,03 ¢ |
| LLM, Ministral 14B, 0,20 $/M tokens entrée et sortie | ~6 000 tokens de prompt (mon estimation, à mesurer), un tour | 0,12 ¢ |
| Voix, 13 $/M caractères | 120 caractères | 0,16 ¢ |
| **Total** | | **≈ 0,3 ¢ par échange** |

Soit environ **3 $ par mois à 30 échanges par jour**. Un second tour ajoute environ 0,12 ¢. Une photo coûte plus cher, mais je n'ai pas trouvé le prix de Pixtral. Contre-intuitif et utile pour le lecteur : **la voix coûte plus que la transcription, et à peu près autant que le LLM.**

La clé gratuite a fonctionné pour le LLM et la vision (mesures du 22 et du 27/10 dans le code) ; rien ne dit qu'elle suffise pour la voix, ni avec quel plafond. Sur la clé gratuite, l'entraînement doit être désactivé à la main.

---

## 6. Ce qu'il faut préparer pour que le code soit reproductible

### 6.1 Le dépôt

1. **Le rendre public**, ou créer un dépôt public dédié. Je recommande le dépôt existant : le firmware dépend de presque tous les modules.
2. **Poser un tag par partie** : `programmez-1`, `-2`, `-3`, et citer ces tags. Le code bougera, l'article non.
3. **Refaire la construction depuis un clone neuf sur chaque cible citée.** Résultat pour l'ESP dans la section 6.4.
4. **Mettre les README à jour**, ils ont du retard sur le code :
   - `system/esp/README.md` s'ouvre sur « pas encore essayé sur la carte », et cite `../pocketqi_win`, `../visage`, `../emu_esp32`, un chemin `/mnt/c/Projects/Robot/system/esp` ;
   - il renvoie à `pc/` et à `../bench_bax`, **absents du dépôt** ;
   - `FONCTIONNALITES.md` dit caméra et sous-titres à faire, et le tactile en CST328 ;
   - `pocket_xiao.h` et la fiche de câblage décrivent encore l'ancien montage.
5. **Écrire `docs/MATERIEL.md`** : le câblage **réel** (tactile sur D5/D7, reset sur D6, molette coupée, variantes CST328 et CST3530). C'est le manque le plus gênant pour un lecteur.
6. **Traiter le boîtier** : déposer STL ou 3MF et la fiche, renommés Pocket, ou écrire que le boîtier sort du cadre. Ne pas publier `pocket/refs` (images Rabbit R1 et rendus Gemini).
7. **Remplacer les exemples** : retirer du firmware par défaut les apps d'essai (`test`, `testapp`, `testfac`, `fact`, `lavage`, `mailessai`).

### 6.2 Les secrets

Ce que j'ai vérifié, sans lire `secrets/` :

- `.gitignore` refuse `*.key`, `memoire.txt`, comptes mail, `wpa_supplicant.conf`.
- Une recherche de formats de clés connus (Groq, GitHub, Google, clés privées PEM) dans **tout l'historique** ne trouve rien, hors un commentaire de mbedTLS. Dans les fichiers actuels, les chaînes de 32 caractères sont des identifiants de code.
- **Limite** : une clé Mistral n'a pas de préfixe reconnaissable. L'auteur doit lancer `gitleaks` sur l'historique, et comparer avec ses vraies clés, avant de rendre public.
- L'historique montre l'adresse d'auteur des commits : à accepter ou à réécrire.

Failles à signaler au lecteur, ou à corriger :

- **L'API du Studio est ouverte par défaut.** `pocketqi.cfg` lance `-api 443`, et sans `etc/admin.pass` il n'y a **aucun mot de passe** (`admin_apps.cpp:38`). Toute machine du réseau peut installer et lancer une app, qui peut lire `memoire.txt` et appeler le web. **Je recommande `-api 0` dans le fichier d'exemple.**
- Les clés sont en clair sur la microSD ou dans la NVS : `sdkconfig.defaults` n'active pas le chiffrement de la flash. Une clé dédiée, avec plafond.

### 6.3 Le zip pour le magazine

- Un zip par partie, tiré du tag par `git archive`, réduit à ce qui sert à l'ESP : `system/esp`, `system/windows`, `system/modules` utiles, `system/sd`, `docs`, licences.
- Sans : Pi, Circle, PicoTTS, voix pré-enregistrées, `third_party` inutilisés, Studio (renvoi vers le dépôt).
- Ajouter un `LISEZMOI.md` : prérequis, flash, secrets, et les versions exactes (ESP-IDF 5.5.1, LVGL 9.2.2).
- **Tester le zip lui-même** sur une machine propre.
- Rappeler la date de relevé des prix et des noms de modèles : `-latest` peut changer de comportement sans prévenir.

### 6.4 Construction depuis un clone neuf

Essai du 30/10/2026 dans WSL : clone neuf de `a5d5382`, `idf.py build` dans `system/esp`, ESP-IDF 5.5.1 déjà installé.

**Résultat : la construction réussit**, en 95 s, sans aucun avertissement. Binaire de 2,77 Mo, 54 % de la partition de 6 Mo restent libres. Le firmware n'a pas été flashé : l'essai prouve qu'il se construit, pas qu'il tourne.

Non essayés : `build_windows.sh`, l'émulateur, Studio, les cibles Pi, et un WSL vierge (`setup_wsl.sh`).

---

## 7. Licences et points bloquants

### 7.1 La situation

- **PocketQI n'a aucune licence** : pas de `LICENSE`, et `THIRD_PARTY.md` dit « à décider avant toute publication ». Aujourd'hui, un lecteur n'aurait aucun droit de réutiliser ce code. **Blocage.**
- **Une partie est déjà publiée sous MIT.** Le dépôt public d'Onyx contient `user/Apps/pocketqi/core/`, avec un en-tête MIT, cité comme tiré de PocketQI : `groq_proto.c`, `pocket_visage.c`, etc. Le choix logique est donc **MIT**, comme la règle d'Onyx.
- **Le nom du titulaire diffère.** Les en-têtes disent « Stéphane Wegener », la consigne et git disent « Stéphan ». À corriger, partout, avant publication.
- `THIRD_PARTY.md` est **périmé** : il dit qu'Onyx n'a aucune licence, alors que `docs/LICENSING.md` a décidé le MIT le 01/10/2026. Mais **Onyx n'a toujours pas de fichier `LICENSE` à sa racine**, et 10 des 12 sources de `modules/vm_onyx` n'ont aucune mention de licence. Il faut un `LICENSE` dans Onyx et une ligne de provenance dans PocketQI.

### 7.2 Ce que réutilise le firmware ESP

| Élément | Licence | Remarque |
|---|---|---|
| Code de l'auteur, copies d'Onyx | **MIT proposé** | VM BASIC, mail, impression |
| ESP-IDF 5.5.1, mbedTLS | Apache-2.0 | |
| LVGL 9.2.2 | MIT | |
| libopus 1.6.1 | BSD-3-Clause | |
| stb_truetype | MIT ou domaine public | |
| esp32-camera 2.1.8, esp_jpeg 1.3.2 | Apache-2.0 à confirmer | **absents de `THIRD_PARTY.md`** |
| DejaVu Sans | licence Bitstream Vera / DejaVu | |
| Pilote CST3530 | code propre d'après un pilote Apache-2.0 | à citer |
| Certificats racine `ca-bundle.pem` | à sourcer | **absent de `THIRD_PARTY.md`** |

**Aucune dépendance copyleft n'entre dans le firmware ESP** d'après ce tableau : il peut être distribué sous MIT, à condition de joindre les notices.

### 7.3 Ce qui reste à part

| Élément | Licence | Conséquence |
|---|---|---|
| Noyaux Pi (Circle) | **GPL-3.0** | le binaire Pi est GPLv3 ; les fichiers de l'auteur peuvent rester MIT |
| `esp/emulateur/qemu` | **GPL-2.0-or-later** | garder ce dossier sous sa licence, le dire dans le `LICENSE` racine |
| PocketSphinx et son modèle français | BSD-2-Clause annoncé | **vérifier la licence du modèle** |
| PicoTTS, voix | Apache-2.0 | hors du zip |
| Firmwares Broadcom | non libres | téléchargés, jamais dans le dépôt |
| 18 WAV de voix Mistral (`pi/noyau/sd/voix`) | sorties de l'API | **à confirmer**, ou à retirer |
| Signes du visage d'après xiaozhi | | vérifier ce qui a été repris de `references/xiaozhi_affichage` (hors dépôt) : idée seule, ou dessin |

### 7.4 Liste des blocages

| # | Point | Bloque | Responsable |
|---|---|---|---|
| 1 | Reformuler la promesse de vie privée | la partie 1 et tout le fil rouge | auteur + rédacteur en chef |
| 2 | Choisir la licence, poser `LICENSE`, corriger le nom | toute publication | auteur |
| 3 | Dépôt privé | la reproduction par les lecteurs | auteur |
| 4 | Câblage réel non documenté, molette coupée | la partie 1 | auteur |
| 5 | Aucune mesure de bout en bout sur la carte | la partie 1 | auteur |
| 6 | Apps et mail non vérifiés sur la XIAO | la partie 3 | auteur |
| 7 | Envoi d'une photo au doigt non vérifié | la partie 2 | auteur |
| 8 | API ouverte par défaut | le code publié | auteur |
| 9 | Scan des secrets par `gitleaks` | la mise en public | auteur |
| 10 | Conditions officielles Mistral, tarifs | les chiffres imprimés | auteur |

---

## 8. Questions ouvertes

### 8.1 Pour l'auteur

1. Acceptes-tu la reformulation de la section 2.3 ? Faut-il en parler à François Tonic avant d'écrire ?
2. Veux-tu réduire les fuites avant publication : filtre local des mails et numéros (2.5) ? PicoTTS est-il hors de portée ?
3. MIT pour tout ton code ? Quel nom exact pour le copyright : Stéphan ou Stéphane ?
4. Dépôt `PocketQI` public entier, ou dépôt réduit ? Publier l'adresse de tes commits ?
5. Le prototype est-il monté dans son boîtier ? La molette sera-t-elle rétablie (où passe alors le tactile) ? Publie-t-on le boîtier ?
6. As-tu fait tourner, **sur la XIAO avec la microSD**, l'agenda, la météo et le mail ? Une question vocale complète, de bout en bout ? L'envoi de la photo au doigt ?
7. Quel Wi-Fi vises-tu : partage de connexion du téléphone ou réseau de la maison ? Les mesures changent.
8. Clé Mistral gratuite ou payante, et le réglage d'entraînement est-il désactivé sur la tienne ?
9. Pourquoi des `outcomes` en JSON plutôt que l'API `tools` ? Je veux écrire ta vraie raison, pas la mienne.
10. Qui est « le client » des README ? Toi, je suppose ; à remplacer par « je ».
11. Veux-tu garder les noms hérités (`groq_proto.c`, `galo.bi`, `win_mic_read`), avec une phrase d'explication, ou les renommer ? Je conseille de garder : renommer casse le code partagé avec Onyx.
12. Dans quelle mesure le travail avec Claude doit-il apparaître : un encadré par partie, comme prévu ici, ou une seule fois ?

### 8.2 Pour le rédacteur en chef

1. Les numéros 279 et 280 portent-ils bien les parties 1 et 2 ? Quelle date pour la partie 3 ?
2. Quelle longueur par partie ? Je propose 6, 5 et 6 pages. Un minimum ou un maximum ?
3. La promesse sur la vie privée est reformulée, plus exacte : est-ce accepté ?
4. Le magazine prend-il des schémas de câblage redessinés, ou faut-il fournir un fichier vectoriel ? Résolution minimale des photos ?
5. Extraits de code : longueur maximale, et le zip est-il hébergé par le magazine, avec quelle taille limite ?
6. Le code cité doit-il être en français, comme dans le dépôt ? Les encadrés sont-ils comptés dans les signes ?
7. Une bio et une photo par partie, ou une fois pour toutes ?

---

## 9. Calendrier de rédaction

Le rédacteur en chef attend aussi l'article sur Onyx avant le 2 décembre : les deux chantiers se recouvrent en novembre. Les dates ci-dessous sont des propositions.

| Période | Travail | Fin de l'étape |
|---|---|---|
| 2-6 nov. | Réponses aux questions ; message au rédacteur en chef ; choix de la licence | décisions écrites |
| 9-20 nov. | Nettoyage du dépôt, `gitleaks`, câblage, tag `programmez-1`, clone neuf | dépôt prêt à ouvrir |
| 16-27 nov. | Mesures partie 1 sur la carte, photos | tableau des mesures |
| 30 nov.-11 déc. | Rédaction partie 1 | brouillon |
| 14-18 déc. | Relecture, exactitude de chaque chiffre, zip testé, envoi | **partie 1 livrée le 18 déc., 5 jours de marge** |
| 4-15 janv. | Vérifier caméra et sous-titres, mesures partie 2 | tableau des mesures |
| 18-29 janv. | Rédaction partie 2 | brouillon |
| 1-12 févr. | Relecture, tag, zip, envoi | **partie 2 livrée le 12 févr.** |
| 15-26 févr. | Apps et mail sur la carte, banc, essai d'injection | tableau des mesures |
| mars | Rédaction partie 3, encadré Onyx, inventaire final | date à fixer avec le magazine |

**Ouvrir le dépôt avant la sortie du numéro 279**, et jamais plus tard : l'article renverra vers lui.

---

## 10. Résumé

**Découpage retenu.** Les trois parties acceptées, pour environ 17 pages :
1. *Écouter, comprendre, répondre* (6 pages), avec la voix de sortie ajoutée ;
2. *Voir et montrer* (5 pages), où la caméra introduit le second tour ;
3. *Agir et étendre* (6 pages) : apps BASIC, boucle fermée, inventaire de la vie privée, Onyx en piste de fin.

**Écarts importants.**
- « Rien de personnel n'est envoyé au LLM » est faux tel quel. Le masquage ne couvre que les valeurs déjà mémorisées. La voix, tout ce que Pocket dit, y compris un mail lu à voix haute, et les photos partent chez Mistral.
- Pas de « dis Pocket » sur l'ESP, mais le bouton ou le toucher.
- Pas d'API `tools` : des `outcomes` en JSON et des seconds tours.
- Molette inactive dans le montage actuel ; câblage documenté périmé.
- Aucune mesure de latence de bout en bout sur la carte ; les chiffres existants viennent surtout de l'émulateur ou du PC. Autonomie inconnue.
- Apps BASIC et mail non vérifiés sur la XIAO ; envoi de la photo au doigt non vérifié.

**Points bloquants.**
- Dépôt privé et sans licence.
- Promesse de vie privée à reformuler avec le magazine.
- Câblage réel à écrire et mesures à relever avant la partie 1.
- API du Studio sans mot de passe par défaut.
- Scan `gitleaks` à faire avant ouverture.
- Tarifs et conditions Mistral à confirmer à la source.

**Questions ouvertes.**
- À l'auteur : la reformulation, la licence et le nom du titulaire, le périmètre du dépôt, l'état réel du prototype, la clé utilisée, la raison du JSON.
- Au rédacteur en chef : l'attribution des numéros 279 et 280, la date de la partie 3, la longueur, le format des figures et du zip.

---

## Sources externes consultées le 30/10/2026

- [Mistral, tarifs](https://mistral.ai/pricing)
- [Mistral, utilisation des données pour l'entraînement](https://help.mistral.ai/en/articles/347617-do-you-use-my-user-data-to-train-your-artificial-intelligence-models)
- [Mistral, désinscription de l'entraînement](https://help.mistral.ai/en/articles/455207-can-i-opt-out-of-my-input-or-output-data-being-used-for-training)
- [Tarifs Voxtral 3, relevé tiers du 22/10/2026](https://www.usagepricing.com/blueprint/activity/mistral-ai-2026-10-22-voxtral-3-audio-pricing)
- [Ministral 3 14B, relevé tiers](https://www.developersdigest.tech/models/ministral-3-14b)
- [Rétention chez Mistral, résumé tiers](https://conductatlas.com/platform/mistral-ai/mistral-ai-privacy-policy/provision/CA-P-007012/data-retention-api-inputoutput-30-day-rolling-window/)
- [Groq, Your Data in GroqCloud](https://console.groq.com/docs/your-data)
- [Groq, réponse de l'équipe sur la confidentialité](https://community.groq.com/t/data-privacy/402)
