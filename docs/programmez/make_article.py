# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
"""make_article.py -- the Word draft of the Programmez! article (docs/programmez/Onyx-Programmez-v2.docx).

    python docs/programmez/make_article.py

Plain, as the magazine asks: one column, no header, footer or frame; the pictures placed with their
caption for the review (they are sent apart as PNG files too)."""
import os, re
from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH

HERE = os.path.dirname (os.path.abspath (__file__))
OUT = os.path.join (HERE, "Onyx-Programmez-v2.docx")

doc = Document ()
st = doc.styles["Normal"]
st.font.name = "Calibri"; st.font.size = Pt (11)
st.paragraph_format.space_after = Pt (6)
for s in doc.sections:
	s.top_margin = s.bottom_margin = Cm (2.2); s.left_margin = s.right_margin = Cm (2.4)

def runs (p, text):
	"""**gras**, *italique*, `code` inside a paragraph."""
	for part in re.split (r"(\*\*[^*]+\*\*|\*[^*]+\*|`[^`]+`)", text):
		if not part: continue
		if part.startswith ("**"): p.add_run (part[2:-2]).bold = True
		elif part.startswith ("`"):
			r = p.add_run (part[1:-1]); r.font.name = "Consolas"; r.font.size = Pt (10)
		elif part.startswith ("*"): p.add_run (part[1:-1]).italic = True
		else: p.add_run (part)
	return p

def para (text, style = None):
	return runs (doc.add_paragraph (style = style), text)

def h (text, level = 1):
	doc.add_heading (text, level = level)

def bullets (items):
	for t in items: para (t, "List Bullet")

def figure (png, caption, width = 15.5):
	p = doc.add_paragraph (); p.alignment = WD_ALIGN_PARAGRAPH.CENTER
	p.add_run ().add_picture (os.path.join (HERE, png), width = Cm (width))
	c = doc.add_paragraph (); c.alignment = WD_ALIGN_PARAGRAPH.CENTER
	r = c.add_run (caption + "  [" + png + "]"); r.italic = True; r.font.size = Pt (9.5)
	r.font.color.rgb = RGBColor (0x44, 0x4c, 0x58)

def code (lines):
	p = doc.add_paragraph ()
	for i, l in enumerate (lines):
		r = p.add_run (l); r.font.name = "Consolas"; r.font.size = Pt (9.5)
		if i < len (lines) - 1: r.add_break ()

# ---------------------------------------------------------------- the article
doc.add_heading ("Onyx : j'ai écrit un système d'exploitation avec une IA", level = 0)

para ("**Stéphan Wegener**")
para ("*Développeur ERP chez NSI s.a., en Belgique. Il a appris à programmer adolescent, en QBasic puis en "
      "assembleur, sur un 386 ramassé sur un trottoir et retapé par son père. Linux lui a donné le goût de voir "
      "comment les systèmes sont faits et de personnaliser son bureau. Maker à ses heures (impression 3D, CNC, "
      "robots), il garde un faible pour la programmation bas niveau et les jeux vidéo.*")
PHOTO = "auteur-stephan-wegener.jpg"			# kept out of git (a personal photo)
if os.path.exists (os.path.join (HERE, PHOTO)):
	p = doc.add_paragraph (); p.add_run ().add_picture (os.path.join (HERE, PHOTO), width = Cm (4))
else:
	para ("*[Photo de l'auteur : fichier JPG séparé, à fournir.]*")

para ("**Un noyau multitâche préemptif, une centaine d'applications, un navigateur WebKit : en un peu plus d'un "
      "mois de travail effectif, sur un Raspberry Pi 4 puis un Pi 5, avec Claude comme développeur. Le "
      "développeur, ici, n'est pas celui qu'on croit : retour d'expérience sur une méthode de travail où l'humain "
      "tient la vision, et l'IA le clavier.**")

figure ("bureau-jet.png", "Figure 1 – Jet, le navigateur d'Onyx basé sur WebKit, affiche le site de Programmez! "
        "sur un Raspberry Pi 4.")

h ("La genèse : une borne d'arcade")
para ("Tout est parti d'un loisir : la construction d'une petite borne d'arcade, animée par un Raspberry Pi sous "
      "Linux. Très vite, une idée s'impose : pour une machine dédiée au jeu, un système généraliste est "
      "surdimensionné. Un OS dédié, voire un programme *bare metal* qui tourne directement sur le matériel, serait "
      "plus adapté, surtout si je voulais porter ou écrire moi-même des jeux.")
para ("C'est là que je découvre **Circle**, un environnement bare metal en C++ pour le Raspberry Pi. Les premiers "
      "essais sont concluants : un *snake* jouable à deux, avec deux contrôleurs de boutons branchés en USB. De là "
      "naît une ambition plus grande : bâtir un vrai système d'exploitation sur le Pi.")
para ("Mais la marche est haute, le temps libre limité. Le projet s'essouffle, et finit dans un tiroir.")

h ("Le renouveau : l'IA entre en scène")
para ("Dans le cadre de mon travail, nous avons été amenés à explorer l'usage de l'IA pour programmer. À force de "
      "l'utiliser, j'ai pris la mesure de ce dont Claude était capable. Une idée m'est alors venue : et si je lui "
      "confiais le travail technique pour reprendre mon OS ?")
para ("Je lui ai fait lire Circle et mon ébauche de snake, puis je lui ai exposé ma vision : partir des briques de "
      "Circle pour en faire un vrai système multitâche. Je garderais la vision, les choix et la validation ; Claude "
      "écrirait le code. Le 22 juin 2026, le premier commit s'intitulait sobrement « *Bootstrap: multi-process "
      "kernel on Circle* ».")

h ("De Circle à Onyx : le fossé à combler")
para ("Le Raspberry Pi est aussi un choix pratique : contrairement au PC x86 et à sa myriade de pilotes, son "
      "matériel est unique et connu. Circle en fournit déjà les pilotes, mais avec un multitâche coopératif, sans "
      "aucune protection entre les tâches. Il fallait en faire un vrai système : multitâche préemptif, chaque "
      "programme isolé dans son propre espace mémoire, et un plantage qui ne fait pas tomber le reste.")
para ("Le chemin n'a pas été linéaire. Pour avancer plus vite sur le reste, nous avons renoncé à cette isolation "
      "dès le premier jour, et ne l'avons rétablie qu'en octobre, une fois le système mûr. Un raccourci assumé, "
      "puis une dette remboursée au bon moment.")

h ("Sous le capot, en un coup d'œil")
para ("Le schéma de la figure 2 montre ce que le projet est devenu. En bas, le noyau d'Onyx, qui intègre Circle "
      "comme couche d'abstraction du matériel : les pilotes USB, la carte SD, l'écran, le Wi-Fi, le son. Au-dessus "
      "de la frontière des appels système, tout tourne en mode utilisateur, chaque programme dans son propre "
      "espace mémoire : le serveur graphique, les services, les applications.")
figure ("architecture.png", "Figure 2 – L'architecture d'Onyx : le noyau et Circle, les serveurs, les kits, les "
        "applications.", 16)
para ("Entre les deux, les **kits** : une bibliothèque partagée par domaine (l'interface, le réseau, les fichiers, "
      "les images, le son, l'impression…). Les applications ne parlent jamais directement au noyau ; seul AppKit le "
      "fait. Une application compilée aujourd'hui continue donc de fonctionner quand le noyau évolue.")
para ("Le même découpage rend l'interface **adaptative**. Onyx a trois modes : le **bureau**, servi par Elegant ; "
      "le mode **pocket**, pour le petit écran d'une console de poche ; le mode **console**, plein écran et piloté "
      "à la manette, ces deux derniers servis par PocketUI. Chaque serveur charge sa propre version d'UIKit, la "
      "bibliothèque des widgets, sous le même nom. Une application ignore dans quel mode elle tourne : le même "
      "binaire, sans recompilation, s'affiche en fenêtres sur le bureau et en plein écran en mode pocket (figure 3).")
figure ("bureau-bureautique.png", "Figure 3a – Le mode bureau : Letters, le tableur et le terminal, où ps liste "
        "les processus du système.", 14)
figure ("pocket-letters.png", "Figure 3b – Le mode pocket (640 × 480) : la même application Letters, le même "
        "binaire.", 10)
para ("Dernier mécanisme : le **préchargement**. Au démarrage, Onyx peut charger en mémoire les programmes et les "
      "kits les plus utilisés, et les y garder. Lancer l'un d'eux ne lit alors plus la carte SD : le noyau associe "
      "au nouveau processus les pages de code déjà présentes, partagées en lecture seule, et ne lui donne en propre "
      "que ses données. Le code d'UIKit, par exemple, n'existe qu'en un exemplaire, quel que soit le nombre "
      "d'applications ouvertes.")

h ("La méthode : je suis l'architecte, Claude est le développeur")
para ("J'utilise Claude Opus : pour un travail de réflexion aussi technique, il me semblait plus adapté que Sonnet, "
      "plus puissant ; et Fable, le plus grand modèle de la gamme, aurait été démesuré pour ce projet. Le code vit "
      "sur GitHub, qui sert de terrain commun : Claude y lit le code, y travaille et y pousse ses modifications. "
      "Chaque fonctionnalité suit le même cycle.")
para ("**1. « Quel est le coût pour cette fonctionnalité dans Onyx ? »** Un chantier commence presque toujours par "
      "cette question. Claude revient avec, d'un seul tenant, un état des lieux de ce qui existe, les manques à "
      "combler, leur difficulté, puis un plan par étapes, chacune apportant ce dont la suivante a besoin, et les "
      "choix à trancher.")
para ("Or Claude n'a aucune mémoire d'une conversation à l'autre. La question y est pour beaucoup : demander un "
      "coût, c'est demander un inventaire des écarts, et c'est laisser la décision à l'humain. Le reste tient au "
      "dépôt : chaque nouvelle session y lit les études précédentes, qui ont toutes la même forme, et la reproduit. "
      "La mémoire de l'habitude, c'est le projet lui-même.")
para ("**2. Le « grilling ».** Quand le sujet le demande, une séance de questions-réponses serrées arrête les "
      "choix. Les décisions sont écrites et datées dans le plan, y compris les options que j'ai écartées.")
para ("**3. La maquette.** Pour une application, je demande d'abord une maquette de l'interface, pour vérifier que "
      "nous avons la même vision avant d'écrire une ligne. Pour le bureau, Claude a dessiné le même écran en trois "
      "habillages rétro (Workbench, CDE, Windows 3.1) ; j'ai préféré CDE et demandé à quoi ressemblerait un CDE "
      "modernisé : c'est devenu l'interface d'Onyx.")
para ("**4. Le développement autonome.** Une fois le cadre posé, Claude développe seul.")
para ("**5. Le passage de relais.** À la fin, il rédige un *handoff* : un document Markdown qui décrit l'état du "
      "projet, pour que les sessions suivantes reprennent là où il s'est arrêté. Puis il me fait un rapport, qui "
      "distingue toujours ce qui est vérifié de ce qui ne l'est pas.")
para ("**6. Le test sur le vrai matériel.** À chaque phase, il m'indique quoi tester sur le Pi et le résultat "
      "attendu. C'est moi qui joue le rôle de banc d'essai, et mes retours relancent le cycle.")
para ("Un fichier `CLAUDE.md`, à la racine du dépôt, fixe les règles permanentes : garder la documentation à jour, "
      "publier les paquets, mettre notre code sous licence MIT… C'est une sorte de contrat entre l'humain et l'IA, "
      "relu au début de chaque session.")

h ("Étude de cas : de NetSurf à WebKit")
para ("Le premier navigateur d'Onyx était un portage de NetSurf, enrichi commit après commit : grilles CSS, "
      "animations, cache de code JavaScript. Fin septembre, une comparaison avec Chromium et WebKit montrait pourtant "
      "un écart toujours grand. J'ai alors posé la question : que faudrait-il pour porter WebKit ?")
para ("La réponse de Claude a pris la forme d'un plan détaillé de ce qui manquait côté POSIX. Il partait de "
      "constats vérifiés dans le code : une chaîne de compilation sans threads ni TLS, une vingtaine d'en-têtes "
      "absents (`sys/mman.h`, `poll.h`, `sys/socket.h`…), dix-sept fonctions manquantes (`mmap`, `clock_gettime`, "
      "`posix_memalign`…), des sockets sans `select`. Le travail était découpé en lots indépendants : mémoire et "
      "pagination à la demande, fichiers et processus, réseau, bibliothèque C, chaîne de compilation.")
para ("Mes décisions y sont datées : WebKit **remplace** NetSurf, l'architecture sera **WebKit2** "
      "(multi-processus, sur le modèle du portage PlayStation), `fork()` attendra.")
h ("Six semaines estimées, une journée réalisée", 2)
para ("Le plan estimait 45 à 60 « jours-agent », soit 6 à 8 semaines, pour ce seul socle POSIX. En pratique, il a "
      "été réalisé le 2 octobre, par plusieurs sessions menées en parallèle : la couche POSIX, la communication entre "
      "processus, une vraie chaîne de compilation (GCC 14.2 avec threads POSIX) et les dépendances de WebKit (ICU, "
      "FreeType, HarfBuzz, Skia…). Des portages de test (SQLite, libxml2, curl) prouvaient chaque étape. Le "
      "lendemain, le navigateur WebKit affichait une page en HTTPS sur le Raspberry Pi.")
para ("Ce n'est possible que parce que le plan existait : des lots aux interfaces fixées à l'avance, et des tests "
      "prévus pour chacun. C'est ce qui permet à plusieurs agents de travailler en parallèle sans se marcher dessus.")
h ("Une nuit en autonomie sur le Pi", 2)
para ("Une fois le navigateur en marche, JavaScript restait lent sur le Pi. Un soir, avant d'aller dormir, j'ai "
      "demandé à Claude de continuer seul pendant la nuit. Onyx avait déjà un accès à distance par telnet, un "
      "serveur FTP et un bureau à distance par VNC. **Parce que je l'y ai explicitement autorisé**, Claude s'en est "
      "servi pour monter, de lui-même, une boucle de travail complète : déployer un nouveau noyau ou un programme "
      "par FTP, redémarrer le Pi, lancer les tests et les benchmarks, récupérer les journaux d'erreurs, corriger, et "
      "recommencer.")
para ("Au réveil, l'historique racontait la nuit, commit après commit. Un service du système monopolisait le "
      "processeur en attendant ses messages : tous les programmes tournaient quatre fois trop lentement. Le "
      "ramasse-miettes de JavaScript, conçu pour plusieurs cœurs, étouffait le script. Le Wi-Fi était passé de 4 à "
      "plus de 8 Mo/s. Chaque correction était mesurée sur le vrai matériel et publiée en paquet.")
para ("L'autorisation est le point clé. Installer un noyau et redémarrer une machine sont des opérations sensibles, "
      "et c'est à l'humain de décider si une IA peut les faire seule. Ici, nous étions sur un terrain de jeu : aller "
      "vite comptait davantage, et le risque restait limité, puisque ce n'était pas une machine de production. "
      "C'est toute la philosophie du projet.")

h ("Ce que ça a donné")
para ("Le projet a avancé en deux temps : une semaine fin juin, puis, après une pause de près de trois mois, une "
      "reprise continue depuis la fin septembre 2026. En 32 jours de travail effectif, le dépôt a reçu plus de "
      "2 000 commits.")
para ("Le noyau est multitâche préemptif, avec des processus isolés et la pagination à la demande. Une centaine "
      "d'applications tournent dessus, avec leurs bibliothèques partagées, un gestionnaire de paquets signés et une "
      "documentation tenue à jour par Claude lui-même. Onyx, conçu pour le Raspberry Pi 4, a depuis été porté sur "
      "le **Raspberry Pi 5** : il démarre sur un Pi 500, bureau et Wi-Fi vérifiés ; le GPU et l'Ethernet restent à tester.")
figure ("bureau-koton.png", "Figure 4 – Koton Studio, le studio de musique d'Onyx : une chanson pensée en "
        "accords, que les pistes d'accompagnement, de mélodie et de batterie suivent.", 14)
h ("« Qu'est-ce qui manque à Onyx ? » : quand Claude devient Product Manager", 2)
para ("Si la suite logicielle est si fournie, ce n'est pas parce que j'avais tout prévu. Je demandais souvent à "
      "Claude des idées de logiciels, ou de comparer Onyx aux systèmes grand public : que chercherait en vain un "
      "utilisateur de Windows, de macOS ou de Linux ? Claude change alors de rôle. Il dresse la liste des manques, "
      "puis me demande par quoi je veux commencer. Je lui dis ce qui m'intéresse, et il lance aussitôt un agent, "
      "dans une autre session, qui se charge de l'analyse : un coût, une maquette, un plan.")
para ("C'est ainsi que sont venus des émulateurs de consoles (la borne d'arcade n'est jamais loin), une suite "
      "bureautique, le navigateur WebKit, ou une petite CAO paramétrique qui produit aussi le G-code de ma fraiseuse "
      "CNC. Et des outils pour programmer : QBasic, bien sûr, clin d'œil à mes débuts, un studio pour dessiner des "
      "applications fenêtrées en BASIC, un atelier pour piloter les broches du Pi, et un jeu de tortue pour "
      "apprendre la programmation aux enfants.")
figure ("bureau-gpiolab.png", "Figure 5a – GPIO Lab : les 40 broches du connecteur du Pi, leur mode et leur "
        "chronogramme.", 13)
figure ("bureau-turtle.png", "Figure 5b – Turtle Quest : un niveau réussi, un sous-programme qui dessine tous les "
        "polygones.", 13)
para ("L'IA n'est donc pas seulement un développeur. Elle tient aussi le rôle d'un Product Manager : elle connaît "
      "ce que font les autres systèmes, propose quoi construire ensuite, et organise le travail. La décision, elle, "
      "reste la mienne.")

h ("Onyx en chiffres")
para ("Les chiffres ci-dessous ont été relevés le 27 octobre 2026, dans le dépôt et sur un Raspberry Pi 4 "
      "(8 Go, carte microSD).")
bullets ([
	"**Durée** : premier commit le 22 juin 2026 ; 32 jours actifs, 2 098 commits.",
	"**Code** : environ 43 000 lignes pour le noyau et 384 000 pour les applications et les bibliothèques (C, "
	"C++, assembleur), sans compter les bibliothèques tierces (WebKit, FFmpeg, MuPDF, FreeType…).",
	"**Taille** : une image du noyau de 1,3 Mo, Circle compris ; une carte complète de 392 Mo (le système, plus de "
	"100 applications, les polices, les exemples) ; une carte minimale de 127 Mo, le reste s'installant par paquets. "
	"Pas d'ISO : on copie un dossier sur une carte SD.",
	"**Démarrage** : pour donner une idée de la rapidité, le bureau apparaît **9 s** après la mise sous tension, et "
	"le réseau est connecté au bout de **25 s**. Un Raspberry Pi OS met environ 30 s pour afficher le bureau. C'est un "
	"ordre de grandeur, pas une comparaison scientifique : Raspberry Pi OS est un système généraliste, bien plus complet.",
	"**Lancement des applications** : le traitement de texte est prêt en 0,3 s, le tableur en 0,8 s, avec ou sans "
	"document ; le studio de musique, qui charge 87 Mo de sons, en 2,9 s.",
	"**Tokens** : sur les trois dernières semaines, Claude a généré 3,8 millions de tokens (le code, les documents, "
	"les réponses) et relu 2,3 milliards de tokens de contexte, presque tous depuis le cache.",
])

h ("Les forces, et le revers de la médaille")
h ("Un sujet technique, mais bien documenté", 2)
para ("Écrire un OS est très technique, mais repose sur des sujets abondamment documentés : ordonnancement, tables "
      "de pages, allocateurs, pilotes, formats de fichiers. À ce niveau, Claude excelle : il retrouve les "
      "algorithmes qui marchent et les met en œuvre.")
h ("Un levier pour la créativité…", 2)
para ("L'IA code vite et donne vite des résultats. Une idée d'application le matin peut tourner sur le Pi le soir. "
      "C'est un levier extraordinaire pour la créativité.")
h ("… et un risque de dispersion", 2)
para ("Le revers, c'est la dispersion. On travaille sur l'interface graphique, puis une idée d'application surgit, "
      "puis un petit bug revient en mémoire. Il m'arrive de lancer cinq conversations en parallèle. Il faut alors un "
      "agent chargé de centraliser les résultats et de les consolider dans la branche principale. Le nombre de "
      "branches sur GitHub en témoigne : une quinzaine, en plus de `main`, et un élagage s'impose.")
para ("**Le vrai défi n'est donc plus d'écrire le code, mais d'organiser le travail.**")
h ("Quand Claude se trompe", 2)
para ("Claude se trompe, et il le fait avec le même aplomb que lorsqu'il a raison. Je ne relis pas chaque ligne de ce "
      "code : ma garantie, ce sont le vrai matériel et les tests. Six épisodes montrent à quoi cela ressemble.")
para ("**Le noyau qui coupe le Pi du monde.** Le 4 octobre, pour accélérer le Wi-Fi, Claude installe par FTP un noyau "
      "doté d'un nouveau chemin rapide. Le Pi démarre, mais sans Wi-Fi, sa seule liaison : plus moyen de le réparer à "
      "distance. J'ai dû retirer la carte SD et y recopier un noyau à la main. Depuis, toute modification du réseau "
      "passe par un fichier d'essai valable pour un seul démarrage : si l'essai n'est pas confirmé, le Pi redémarre "
      "tout seul et retrouve ses réglages.")
para ("**Le test qui passe pour une mauvaise raison.** Le 5 octobre, Claude publie sept versions du paquet système et "
      "ses tests sur le Pi sont bons. Pourtant, le Pi n'avait installé aucune d'elles et restait depuis des heures sur "
      "une ancienne version : les fichiers copiés à la main pour les essais masquaient le problème, et le panneau de "
      "configuration cherchait encore une bibliothèque disparue. Vérifier ce qui est réellement installé fait "
      "désormais partie de la procédure.")
para ("**Le gain qui cachait une perte.** Les mesures de débit étaient superbes : plus de 8 Mo/s en Wi-Fi. Mais j'ai "
      "remarqué que le bureau à distance gelait dès qu'un téléchargement tournait. La cause : en agrégeant ses trames, "
      "le Pi perdait alors de 20 à 75 % de ce qu'il envoyait. Sans agrégation, il n'émet plus qu'à 3 Mo/s environ. "
      "Claude a exposé le compromis, j'ai choisi la fiabilité.")
para ("**Le paquet vide.** Le navigateur pèse trop lourd pour Git : son binaire n'existe que dans un clone. Une session "
      "a publié le paquet depuis un autre clone : 2,6 Ko au lieu de 43 Mo, signé et en apparence valide. Une autre "
      "session l'a repéré et republié. C'est le prix du parallélisme : chaque session ne voit que sa propre copie.")
para ("**La promesse à moitié vraie.** Les kits doivent isoler les applications des changements du noyau. En compactant "
      "la table des appels système, nous avons découvert que quatorze programmes, dont le navigateur, la lisaient "
      "encore directement : ils auraient appelé la mauvaise fonction, sans le moindre message d'erreur. Une analyse "
      "des binaires fait désormais partie des contrôles avant de publier un noyau.")
para ("**Jusque dans cet article.** Pour les chiffres de la fin, Claude a mesuré 33 s de démarrage, avec assurance. "
      "Le chiffre me paraissait trop élevé : il comptait jusqu'à la connexion Wi-Fi. J'ai contesté, et nous avons "
      "ajouté au journal du noyau l'heure de chaque événement pour mesurer proprement.")
para ("Ces erreurs ont un point commun : aucune ne s'annonce. Mon garde-fou n'est donc pas de tout relire, mais de "
      "tester sur le vrai matériel, d'exiger des rapports qui séparent ce qui est vérifié de ce qui ne l'est pas, et "
      "de transformer chaque incident en règle écrite, que la session suivante relira. Et il reste une part que "
      "je ne délègue pas : me méfier d'un chiffre ou d'un résultat trop beau.")
h ("Le coût : savoir attendre", 2)
para ("Sur un projet de cette taille, les tokens se consomment vite. Le gros du volume ne vient pas du code écrit, "
      "mais du contexte relu : à chaque étape, le modèle relit le code ouvert et la conversation en cours. Le cache "
      "rend cette relecture bien moins chère, mais elle pèse sur le quota.")
para ("La vraie question est : accepte-t-on parfois d'attendre ? J'étais sur l'abonnement Max, à environ 90 € HTVA par mois, et "
      "cela marchait très bien. Puis, un jour, mon quota de la semaine s'est épuisé. Plutôt que d'attendre trois "
      "jours, je suis passé à la formule supérieure.")
para ("Avec moins d'impatience et moins de dispersion, cet abonnement aurait sans doute suffi. Cinq "
      "conversations en parallèle consomment le quota cinq fois plus vite : l'arbitrage se fait entre discipline et "
      "budget. Pour mémoire, Claude propose une formule gratuite, Pro à environ 18 € par mois, puis Max à environ "
      "90 € ou 175 € par mois selon l'usage (prix HTVA, convertis depuis les tarifs en dollars).")

h ("Ce que je retiens")
para ("L'IA n'a pas écrit Onyx toute seule. Elle a écrit le code d'un projet dont j'ai tenu la vision, arbitré "
      "chaque choix et testé chaque étape sur le vrai matériel. Tout ce qu'elle a proposé n'a pas été fait : une "
      "étude complète sur le multi-utilisateur, par exemple, a été mise de côté, et Onyx reste mono-utilisateur.")
para ("Quelques conseils à qui voudrait se lancer :")
bullets ([
	"**Cadrer avant de coder.** Étude, décisions, maquette : le temps passé en amont fait gagner des jours.",
	"**Écrire la mémoire du projet.** Un handoff et un fichier de règles permettent à chaque nouvelle session de "
	"repartir sans rien perdre.",
	"**Tester sur le vrai matériel.** Les rapports distinguent ce qui est vérifié de ce qui ne l'est pas ; le Pi "
	"tranche.",
	"**Se discipliner.** La vitesse de l'IA pousse à tout lancer en même temps ; c'est là que se perdent le quota et "
	"la cohérence.",
])
para ("Le projet que j'avais rangé dans un tiroir faute de temps est devenu, en un peu plus d'un mois de travail, un "
      "système complet. Le temps de développement a fondu ; il reste le plus intéressant, décider de ce qu'on "
      "construit.")

h ("Pour essayer Onyx")
para ("Le code d'Onyx est public, sous licence MIT pour l'essentiel (le noyau suit la GPL-3.0 de Circle) : "
      "https://github.com/stephaneweg/Onyx. Il vous faut un Raspberry Pi 4 (ou un Pi 400), ou un Raspberry Pi 5 (ou "
      "un Pi 500), et une carte SD.")
para ("**Sans rien compiler.** Le dossier `sdcard/` du dépôt est une carte prête à démarrer pour le Pi 4, "
      "`sdcard5/` pour le Pi 5. Copiez tout son contenu à la racine d'une carte SD formatée en FAT32, insérez-la "
      "dans le Pi et allumez. Au premier démarrage, un assistant règle le pays, le clavier, le fuseau horaire et le "
      "Wi-Fi. Le navigateur n'est pas sur la carte : installez-le ensuite depuis le gestionnaire de paquets, dans le "
      "panneau de configuration (l'icône en forme d'engrenage du dock).")
para ("**En compilant tout.** Il faut un PC sous Linux, ou sous Windows avec WSL 2 et Ubuntu. Un script fait tout "
      "le travail :")
code (["sudo apt update && sudo apt install -y git build-essential python3 curl xz-utils",
       "git clone https://github.com/stephaneweg/Onyx.git",
       "cd Onyx",
       "sh tools/build-sdcard.sh"])
para ("Le script télécharge la chaîne de compilation d'Arm (GCC pour AArch64), récupère et compile Circle, puis le "
      "noyau, les bibliothèques et les applications, et dépose le tout dans `sdcard/`. Comptez environ cinq minutes "
      "sur un processeur à quatre cœurs. "
      "Sous WSL, clonez le dépôt dans votre dossier Linux plutôt que sous `/mnt/c`, bien plus lent. Le navigateur "
      "n'est pas compilé par le script, car WebKit demande des heures de compilation : il s'installe, là aussi, par "
      "le gestionnaire de paquets. Pour le recompiler depuis ses sources, un second script, `tools/build-jet.sh`, "
      "s'en charge en deux heures environ sur quatre cœurs.")

h ("Épilogue : repartir de zéro")
para ("Au moment où je termine cet article, je suis déjà sur le projet suivant. Avec Claude, j'ai commencé un nouvel "
      "OS pour le Raspberry Pi 4, ARMPI, et cette fois de zéro, sans m'appuyer sur Circle. Le système est écrit en "
      "assembleur AArch64, avec FASM ARM. C'est un micronoyau : comme dans Minix ou QNX, les services et les pilotes "
      "tournent en espace utilisateur.")
para ("Le gros du travail a tenu en trois jours intensifs. Surtout, rien de ce qui a été appris sur Onyx n'a été perdu : "
      "sur cette base, WebKit, le moteur de Jet, a été porté en quelques heures seulement, et la chaîne de compilation "
      "GCC tourne désormais sur ce nouvel OS.")

# ---------------------------------------------------------------- for the editor
doc.add_page_break ()
h ("Illustrations (fichiers PNG fournis à part)")
bullets ([
	"`bureau-jet.png` – Jet, le navigateur d'Onyx basé sur WebKit, affiche le site de Programmez! sur un Raspberry Pi 4.",
	"`architecture.png` – L'architecture d'Onyx : le noyau et Circle, les serveurs, les kits, les applications.",
	"`bureau-bureautique.png` – Le mode bureau : Letters, le tableur et le terminal.",
	"`pocket-letters.png` – Le mode pocket (640 × 480) : la même application Letters, le même binaire.",
	"`bureau-koton.png` – Koton Studio, le studio de musique d'Onyx.",
	"`bureau-gpiolab.png` – GPIO Lab : les broches du Pi, leur mode et leur chronogramme.",
	"`bureau-turtle.png` – Turtle Quest : un niveau réussi.",
	"En réserve : `bureau-3dforge.png` (3DForge, la CAO paramétrique), `pocket-lanceur.png` (le lanceur du mode "
	"pocket), `pocket-tableur.png` (le tableur en mode pocket).",
])

doc.save (OUT)
print (OUT)
