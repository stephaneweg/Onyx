# -*- coding: utf-8 -*-
"""Onyx web site -- the data: every word of the pages, in French and in English.

A text is a pair (French, English). The copy comes from the copywriter's content.md: change a word
here, then run `python website/_build/build.py`. MIT licence (ours)."""

# ----------------------------------------------------------------------------------------------
# THE ONE PLACE for what is not known yet. An empty string leaves the block out (or, for the
# download, shows the button as "coming soon"). Fill one in and rebuild: it becomes live.
# ----------------------------------------------------------------------------------------------
DOWNLOAD_URL = ""            # the archive of the card; "" = the button says "coming soon"
DOWNLOAD_NOTE = ("", "")     # under the button: version, size, e.g. ("Version 0.9 · 512 Mo", "Version 0.9 · 512 MB")
ONYX_REMOTE_URL = ""         # Onyx Remote for Windows   } the block "On your PC too" (Get Onyx) shows
KOTON_WINDOWS_URL = ""       # Koton for Windows         } only when both are filled
GITHUB_URL = ""              # the source code; "" = no open-source line in the footer
SITE_URL = ""                # the public address, with its final "/"; "" = no og:image / og:url
AUTHOR = "Stephan Wegener"
YEAR = "2026"

# ----------------------------------------------------------------------------------------------
# Pages and navigation
# ----------------------------------------------------------------------------------------------
SLUGS = {            # page -> (fr path, en path), relative to the language's folder
    "home": ("", ""),
    "apps": ("applications/", "apps/"),
    "desktop": ("bureau/", "desktop/"),
    "get": ("obtenir/", "get/"),
}
META = {
    "home": (("Onyx — Un ordinateur complet sur un Raspberry Pi 4", "Onyx — A complete computer on a Raspberry Pi 4"),
             ("Onyx est un système d'exploitation fait maison pour le Raspberry Pi 4 : un bureau soigné, une suite bureautique, un navigateur web et plus de 50 applications.",
              "Onyx is a homemade operating system for the Raspberry Pi 4: a polished desktop, an office suite, a web browser and more than 50 apps.")),
    "apps": (("Applications — Onyx", "Apps — Onyx"),
             ("Traitement de texte, tableur, présentations, navigateur, courrier, photos, musique, jeux : découvrez les applications d'Onyx.",
              "Word processor, spreadsheet, slides, browser, mail, photos, music, games: meet the apps of Onyx.")),
    "desktop": (("Le bureau — Onyx", "The desktop — Onyx"),
                ("Barre de menus, dock, espaces de travail, thèmes, fichiers et mises à jour : le tour du bureau d'Onyx.",
                 "Menu bar, dock, workspaces, themes, files and updates: a tour of the Onyx desktop.")),
    "get": (("Obtenir Onyx", "Get Onyx"),
            ("Un Raspberry Pi 4, une carte microSD, quatre étapes : installez Onyx chez vous.",
             "A Raspberry Pi 4, a microSD card, four steps: set Onyx up at home.")),
}
NAV = {"home": ("Accueil", "Home"), "apps": ("Applications", "Apps"),
       "desktop": ("Le bureau", "Desktop"), "get": ("Obtenir Onyx", "Get Onyx")}

UI = {
    "skip": ("Aller au contenu", "Skip to content"),
    "brand_label": ("Onyx, accueil", "Onyx, home"),
    "nav_label": ("Navigation principale", "Main"),
    "lang_label": ("Langue", "Language"),
    "theme_label": ("Mode clair", "Light mode"),
    "menu_label": ("Menu", "Menu"),
    "close": ("Fermer", "Close"),
    "soon": ("Bientôt disponible", "Coming soon"),
    "all": ("Tout", "All"),
    "filter_label": ("Filtrer les applications", "Filter the apps"),
    "shot_prefix": ("Capture d'écran : ", "Screenshot: "),
    "shots_english": ("Captures d'écran en anglais.", ""),
    "top": ("Haut de page", "Back to top"),
    "other_lang": ("English", "Français"),
    "apps_tabs": ("Applications", "Apps"),
    "themes_label": ("Ambiances", "Looks"),
    "window_of": ("La fenêtre de %s.", "The %s window."),
}

FOOTER = {
    "col_site": ("Onyx", "Onyx"),
    "col_lang": ("Langue", "Language"),
    "about": ("Onyx est conçu par %s, avec Claude, l'IA d'Anthropic.",
              "Onyx is designed by %s, working with Claude, Anthropic's AI."),
    "open_source": ("Onyx est un logiciel libre. Code source et licences sur %s.",
                    "Onyx is open-source software. Source code and licences on %s."),
    "trademark": ("Raspberry Pi est une marque de Raspberry Pi Ltd. Les autres marques citées appartiennent à leurs propriétaires respectifs. Onyx est un projet indépendant.",
                  "Raspberry Pi is a trademark of Raspberry Pi Ltd. Other names mentioned belong to their respective owners. Onyx is an independent project."),
}

GET_ONYX = ("Obtenir Onyx", "Get Onyx")

# ----------------------------------------------------------------------------------------------
# Home
# A feature: id, eyebrow, title, body, points, link (text, page, anchor), shot (fr base, en base), alt
# ----------------------------------------------------------------------------------------------
HOME_HERO = {
    "title": ("Un ordinateur complet. Sur un Raspberry Pi 4.", "A complete computer. On a Raspberry Pi 4."),
    "lead": ("Onyx est un système d'exploitation fait maison, avec son propre bureau, sa suite bureautique, son navigateur web et plus de 50 applications.",
             "Onyx is a homemade operating system with its own desktop, its own office suite, its own web browser and more than 50 apps."),
    "secondary": ("Voir les applications", "See the apps"),
    "shots": [
        ("media-home", ("L'accueil du Lecteur multimédia : les albums et les vidéos récemment lus.",
                        "The Media Player's home: recently played albums and videos.")),
        ("mail", ("Mail : la boîte de réception et un message ouvert.",
                  "Mail with the inbox and a message open.")),
    ],
}
HOME_DESKTOP = {
    "eyebrow": ("Le bureau", "The desktop"),
    "title": ("Tout est à sa place.", "Everything in its place."),
    "body": ("Une barre de menus en haut, un dock en bas. Vos applications sont rangées par tiroirs, vos fenêtres réparties sur plusieurs espaces de travail. On s'y retrouve dès la première minute.",
             "A menu bar at the top, a dock at the bottom. Your apps sit in drawers, your windows spread across several workspaces. You find your way around in the first minute."),
    "link": ("Découvrir le bureau", "Tour the desktop"),
    "shot": "notes-desktop",
    "alt": ("Le bureau d'Onyx : la barre de menus, des fenêtres ouvertes et le dock.",
            "The Onyx desktop: the menu bar, open windows and the dock."),
}
HOME_FEATURES = [
    {"eyebrow": ("Travailler", "Work"),
     "title": ("Écrivez, calculez, présentez.", "Write it. Work it out. Present it."),
     "body": ("Letters pour vos textes, un tableur pour vos chiffres, Slides pour vos présentations. Ils ouvrent et enregistrent les fichiers Word, Excel et PowerPoint, exportent en PDF et impriment sur votre imprimante réseau.",
              "Letters for your writing, a spreadsheet for your numbers, Slides for your talks. They open and save Word, Excel and PowerPoint files, export to PDF and print to your network printer."),
     "points": [("Fichiers .docx, .xlsx, .pptx", ".docx, .xlsx and .pptx files"),
                ("Export PDF", "PDF export"),
                ("Impression sans pilote à installer", "Printing with no driver to install")],
     "link": (("Toutes les applications de bureau", "All the work apps"), "apps", "#work"),
     "shot": ("letters", "letters"),
     "alt": ("Letters avec un document ouvert : un titre, une image, une table des matières.",
             "Letters with a document open: a title, a picture, a table of contents.")},
    {"eyebrow": ("Le web et vos proches", "The web and your people"),
     "title": ("Le web d'aujourd'hui. Vos messages aussi.", "Today's web. And your messages."),
     "body": ("Jet Browser affiche les sites modernes, vidéo et son compris. Mail réunit vos comptes Gmail, Outlook, iCloud ou tout autre dans une seule boîte de réception. Et vos conversations Telegram vous suivent.",
              "Jet Browser shows modern websites, video and sound included. Mail brings your Gmail, Outlook, iCloud or any other account into one inbox. And your Telegram conversations come along."),
     "link": (("Internet sur Onyx", "The internet on Onyx"), "apps", "#web"),
     "shot": ("jet", "jet"),
     "alt": ("Jet Browser affichant un site web moderne.", "Jet Browser showing a modern website.")},
    {"eyebrow": ("Créer", "Create"),
     "title": ("Dessinez. Composez. Façonnez.", "Draw. Compose. Build."),
     "body": ("Peignez sur des calques dans Paint, retouchez vos photos, écrivez un morceau dans Koton, le studio de musique, ou dessinez une pièce à imprimer en 3D avec 3DForge.",
              "Paint on layers in Paint, touch up your photos, write a song in Koton, the music studio, or design a part for 3D printing in 3DForge."),
     "link": (("Les applications de création", "The creative apps"), "apps", "#create"),
     "shot": ("paint", "paint"),
     "alt": ("Paint avec un dessin en cours, ses outils et ses calques.",
             "Paint with a drawing in progress, its tools and its layers.")},
    {"eyebrow": ("Vos photos, votre musique, vos films", "Your photos, music and films"),
     "title": ("Votre bibliothèque, bien rangée.", "Your library, in order."),
     "body": ("Photos classe vos images par jour et par album. Le lecteur multimédia retrouve votre musique et vos vidéos tout seul, et reprend le film là où vous l'aviez laissé.",
              "Photos sorts your pictures by day and by album. Media Player finds your music and videos by itself, and picks the film up where you left it."),
     "shot": ("photos", "photos"),
     "alt": ("Photos : la photothèque rangée par jour, avec ses albums.",
             "Photos: the library sorted by day, with its albums.")},
    {"eyebrow": ("Jouer", "Play"),
     "title": ("Seize jeux pour commencer.", "Sixteen games to start with."),
     "body": ("Des cartes, des briques, un flipper, des énigmes : les jeux sont déjà là. Et si vous avez gardé les jeux de vos anciennes consoles, la Ludothèque les range et les lance.",
              "Cards, bricks, a pinball table, puzzles: the games are already there. And if you kept the games of your old consoles, the Game Library sorts and starts them."),
     "small": ("Aucun jeu de console n'est fourni avec Onyx.", "No console game ships with Onyx."),
     "link": (("Tous les jeux", "All the games"), "apps", "#play"),
     "shot": ("pinball-play", "pinball-play"),
     "alt": ("Le flipper d'Onyx, une partie en cours.", "The Onyx pinball table, a game in progress.")},
]
# "Make it yours" and "Always up to date": two features of their own, each with its capture.
HOME_MORE = [
    {"title": ("À vos couleurs.", "Make it yours."),
     "body": ("Sept ambiances, du pêche au café noir, des fonds d'écran qui prennent vos couleurs. Deux clics, et le bureau vous ressemble.",
              "Seven looks, from peach to dark coffee, and wallpapers that take your colours. Two clicks and the desktop is yours."),
     "shot": ("theme", "theme")},
    {"title": ("Toujours à jour.", "Always up to date."),
     "body": ("Onyx vérifie les mises à jour chaque jour et vous prévient. Le Gestionnaire de paquets installe le système et les applications en un clic, et en propose de nouvelles.",
              "Onyx checks for updates every day and tells you. The Package Manager installs the system and the apps in one click, and offers new ones."),
     "shot": ("pkgman-installed", "pkgman-installed")},
]
HOME_LEARN = {
    "title": ("Et si vous êtes curieux.", "And if you're the curious kind."),
    "body": ("Apprenez à programmer en guidant une tortue, assemblez des circuits logiques, écrivez vos propres applications en BASIC ou pilotez les broches du Raspberry Pi depuis l'écran.",
             "Learn to program by guiding a turtle, wire up logic circuits, write your own apps in BASIC or drive the Raspberry Pi's pins from the screen."),
    "link": (("Apprendre et bricoler", "Learn and tinker"), "apps", "#learn"),
    "shot": ("turtle-fr", "turtle"),
    "alt": ("Turtle Quest : un programme court et la tortue qui trace une étoile.",
            "Turtle Quest: a short program and the turtle finding its way through a maze."),
}
HOME_APPS = {
    "title": ("Plus de 50 applications, prêtes à l'emploi.", "More than 50 apps, ready to use."),
    "button": ("Voir toutes les applications", "See all the apps"),
    # tile: app id, shot, tile title (fr, en), tag (fr, en)
    "tiles": [
        ("letters", "letters-table", ("Letters", "Letters"), ("Traitement de texte", "Word processor")),
        ("sheet", "sheet", ("Tableur", "Spreadsheet"), ("Tableur", "Spreadsheet")),
        ("slides", "slides", ("Slides", "Slides"), ("Présentations", "Presentations")),
        ("jet", "jet", ("Jet Browser", "Jet Browser"), ("Navigateur web", "Web browser")),
        ("mail", "mail", ("Mail", "Mail"), ("Courrier", "E-mail")),
        ("telegram", "telegram", ("Telegram", "Telegram"), ("Messagerie", "Messaging")),
        ("paint", "paint", ("Paint", "Paint"), ("Dessin", "Drawing")),
        ("photos", "photos", ("Photos", "Photos"), ("Photothèque", "Photo library")),
        ("media", "media-albums", ("Lecteur multimédia", "Media Player"), ("Musique et vidéo", "Music and video")),
        ("koton", "koton-crop", ("Koton", "Koton"), ("Studio de musique", "Music studio")),
        ("3dforge", "3dforge", ("3DForge", "3DForge"), ("Modélisation 3D", "3D design")),
        ("calendar", "calendar", ("Calendrier", "Calendar"), ("Agenda", "Planner")),
    ],
}
HOME_CTA = {
    "title": ("Une carte microSD suffit.", "All it takes is a microSD card."),
    "body": ("Préparez la carte, branchez votre Raspberry Pi 4, et Onyx vous accueille.",
             "Prepare the card, plug in your Raspberry Pi 4, and Onyx welcomes you."),
}

# ----------------------------------------------------------------------------------------------
# Apps
# ----------------------------------------------------------------------------------------------
APPS_HERO = {
    "title": ("Des applications pour tout ce que vous faites.", "Apps for everything you do."),
    "lead": ("Elles sont faites pour Onyx, se ressemblent et s'entendent entre elles. La plupart sont déjà sur la carte ; les autres s'installent en un clic.",
             "They're made for Onyx, look alike and work together. Most are already on the card; the rest install in one click."),
}
GROUPS = [   # id, title, intro
    ("work", ("Travailler", "Work"),
     ("Une suite bureautique complète, compatible avec les fichiers que l'on vous envoie.", "A full office suite that reads the files people send you.")),
    ("web", ("Le web et vos proches", "The web and your people"), ("Naviguer, écrire, discuter.", "Browse, write, chat.")),
    ("create", ("Créer", "Create"), ("Des images, de la musique, des objets.", "Pictures, music, objects.")),
    ("media", ("Regarder et écouter", "Watch and listen"),
     ("Vos photos, votre musique, vos films.", "Your photos, your music, your films.")),
    ("play", ("Jouer", "Play"),
     ("Seize jeux, et une ludothèque pour vos consoles d'autrefois.", "Sixteen games, and a library for the consoles you grew up with.")),
    ("learn", ("Apprendre et bricoler", "Learn and tinker"),
     ("Pour comprendre comment ça marche, ou fabriquer le vôtre.", "To see how things work, or to make your own.")),
    ("system", ("Le système", "The system"), ("Les outils qui tiennent la maison.", "The tools that keep the house in order.")),
]
TOGETHER = {
    "title": ("Elles travaillent ensemble.", "They work together."),
    "body": ("Envoyez une photo par courrier depuis Photos. Fusionnez un fichier d'adresses de Cardfile dans une lettre. Un lien dans un PDF s'ouvre dans Jet. Ce que vous copiez ici se colle là, et le presse-papiers garde vos dix dernières copies.",
             "Send a photo by mail straight from Photos. Merge a Cardfile address list into a letter. A link in a PDF opens in Jet. What you copy here pastes there, and the clipboard keeps your last ten copies."),
    "shot": "photos-menu",
    "alt": ("Le menu d'une photo dans Photos, avec l'envoi par Mail.",
            "A photo's menu in Photos, with Send by Mail."),
}
EMULATORS = {
    "title": ("Émulateurs", "Emulators"),
    "systems": ["Game Boy / Game Boy Color", "Game Boy Advance", "NES", "Super Nintendo"],
    "body": ("La Ludothèque range les jeux que vous possédez et lance le bon émulateur : Game Boy et Game Boy Color, Game Boy Advance, NES et Super Nintendo. Les sauvegardes sont conservées à côté de chaque jeu, une manette USB suffit. Aucun jeu n'est fourni avec Onyx.",
             "The Game Library sorts the games you own and starts the right emulator: Game Boy and Game Boy Color, Game Boy Advance, NES and Super Nintendo. Saves are kept beside each game, and a USB gamepad is all you need. No game ships with Onyx."),
    "legal": ("Les noms de consoles sont des marques de leurs propriétaires respectifs ; Onyx n'est affilié à aucun fabricant.",
              "Console names are trademarks of their respective owners; Onyx is not affiliated with any manufacturer."),
}
APPS_CTA = {
    "title": ("Et d'autres arrivent.", "And more keep coming."),
    "body": ("Le Gestionnaire de paquets propose les nouvelles applications dès qu'elles sont publiées.",
             "The Package Manager offers new apps as soon as they are published."),
}


def F(group, id, name, shot, pitch, points, small=None):       # a featured app (large card)
    return {"group": group, "id": id, "name": name if isinstance(name, tuple) else (name, name),
            "shot": shot if isinstance(shot, tuple) else (shot, shot), "pitch": pitch, "points": points,
            "small": small, "featured": True}


def C(group, id, name, shot, pitch):                            # a compact app (small card)
    return {"group": group, "id": id, "name": name if isinstance(name, tuple) else (name, name),
            "shot": (shot if isinstance(shot, tuple) else (shot, shot)) if shot else None, "pitch": pitch, "featured": False}


CATALOGUE = [
    # ---- Work
    F("work", "letters", "Letters", "letters",
      ("Le traitement de texte. Des lettres, des rapports, de vrais documents mis en page.",
       "The word processor. Letters, reports, properly laid\u2011out documents."),
      [("Ouvre et enregistre les fichiers Word (.docx), OpenDocument (.odt) et RTF", "Opens and saves Word (.docx), OpenDocument (.odt) and RTF files"),
       ("Tableaux, en-têtes et pieds de page, table des matières, images", "Tables, headers and footers, a table of contents, pictures"),
       ("Publipostage et export PDF", "Mail merge and PDF export")]),
    F("work", "sheet", ("Tableur", "Spreadsheet"), "sheet",
      ("Le tableur. Vos comptes, vos listes, vos graphiques.", "The spreadsheet. Your budgets, your lists, your charts."),
      [("237 fonctions, écrites comme dans Excel", "237 functions, written the way Excel writes them"),
       ("Graphiques, mise en forme conditionnelle, filtres", "Charts, conditional formatting, filters"),
       ("Ouvre et enregistre les fichiers Excel (.xlsx) et CSV", "Opens and saves Excel (.xlsx) and CSV files")]),
    F("work", "slides", "Slides", "slides",
      ("Les présentations. Des diapositives soignées, projetées en plein écran.", "Presentations. Polished slides, shown full screen."),
      [("Six thèmes, des formes, des tableaux, des graphiques", "Six themes, shapes, tables, charts"),
       ("Transitions, animations et vue du présentateur", "Transitions, animations and a presenter view"),
       ("Ouvre et enregistre les fichiers PowerPoint (.pptx) ; export PDF", "Opens and saves PowerPoint (.pptx) files; PDF export")]),
    F("work", "calendar", ("Calendrier", "Calendar"), "calendar",
      ("L'agenda. Vos rendez-vous par jour, par semaine ou par mois.", "The planner. Your appointments by day, week or month."),
      [("Répétitions, rappels et listes de tâches", "Repeats, reminders and task lists"),
       ("Plusieurs calendriers, chacun sa couleur", "Several calendars, each in its colour"),
       ("Importe et exporte les calendriers Google et Outlook (.ics)", "Imports and exports Google and Outlook calendars (.ics)")]),
    F("work", "ledger", "Ledger", "ledger",
      ("La comptabilité des indépendants et des sociétés belges.", "Accounting for Belgian businesses and the self-employed."),
      [("Devis, commandes, factures et notes de crédit, imprimés par Letters", "Quotes, orders, invoices and credit notes, printed through Letters"),
       ("Extraits bancaires importés, paiements des fournisseurs préparés pour votre banque", "Bank statements imported, supplier payments prepared for your bank"),
       ("Déclarations de TVA, bilan et compte de résultats", "VAT returns, balance sheet and income statement")]),
    F("work", "cardfile", "Cardfile", "cardfile",
      ("Une petite base de données, sans jargon. Vos contacts, vos collections, vos inventaires.", "A small database, no jargon. Your contacts, collections, inventories."),
      [("Dessinez votre fiche, puis remplissez-la", "Design your card, then fill it in"),
       ("Vue fiche ou vue liste, tri et recherche", "Card view or list view, sorting and search"),
       ("Import et export CSV ; alimente le publipostage de Letters", "CSV import and export; feeds the mail merge in Letters")]),
    F("work", "pdf", ("Lecteur PDF", "PDF Viewer"), "pdf",
      ("Le lecteur de documents PDF. Un onglet par document, et il se souvient de votre page.", "The PDF reader. One tab per document, and it remembers your page."),
      [("Miniatures, sommaire et recherche dans le texte", "Thumbnails, contents and text search"),
       ("Une page, en continu ou deux pages côte à côte ; plein écran", "One page, continuous or two pages side by side; full screen"),
       ("Texte à copier, liens à suivre", "Copy the text, follow the links")]),
    C("work", "clock", ("Horloge", "Clock"), ("clock-world-fr", "clock-world"),
      ("L'heure ici et dans le monde, des alarmes, un minuteur et un chronomètre.", "The time here and around the world, alarms, a timer and a stopwatch.")),
    C("work", "notes", "Notes", "notes",
      ("Des notes rapides, enregistrées toutes seules. Épinglez les plus importantes sur le bureau.", "Quick notes that save themselves. Pin the ones that matter to the desktop.")),
    C("work", "tinypad", ("Éditeur de texte", "Text Editor"), None, ("Pour le texte brut, simplement.", "For plain text, simply.")),
    C("work", "tinycalc", ("Calculatrice", "Calculator"), "tinycalc",
      ("Une calculatrice scientifique, au clavier ou à la souris.", "A scientific calculator, by keyboard or mouse.")),
    C("work", "graphcalc", ("Calculatrice graphique", "Graphing Calculator"), "graphcalc",
      ("Tracez jusqu'à quatre fonctions, en direct pendant que vous les tapez.", "Plot up to four functions, live as you type them.")),
    C("work", "archiver", "Archiver", "archiver",
      ("Ouvrez, créez et modifiez vos archives ZIP en glissant des fichiers.", "Open, create and change ZIP archives by dragging files in and out.")),
    C("work", "rtfview", ("Lecteur RTF", "RTF Reader"), None, ("Un coup d'œil rapide à un document RTF.", "A quick look at an RTF document.")),
    # ---- The web and your people
    F("web", "jet", "Jet Browser", "jet",
      ("Le navigateur web d'Onyx, bâti sur WebKit, le moteur de Safari.", "The Onyx web browser, built on WebKit, the engine behind Safari."),
      [("Affiche les sites d'aujourd'hui, vidéo et son compris", "Shows today's websites, video and sound included"),
       ("Une page par fenêtre, pour rester concentré", "One page per window, to keep your focus"),
       ("Recherche dans la page, zoom, téléchargements", "Find in page, zoom, downloads")]),
    F("web", "mail", "Mail", "mail",
      ("Tous vos comptes de courrier dans une seule boîte de réception.", "All your mail accounts in one inbox."),
      [("Gmail, Outlook, iCloud, Yahoo, ou le compte de courrier que vous avez déjà", "Gmail, Outlook, iCloud, Yahoo, or the mail account you already have"),
       ("Messages regroupés en conversations ; recherche dans tous les comptes", "Messages grouped into conversations; search across every account"),
       ("Carnet d'adresses, pièces jointes, notification à l'arrivée d'un message", "Contacts, attachments, a notification when mail arrives")]),
    F("web", "telegram", "Telegram", "telegram",
      ("Votre compte Telegram, dans une messagerie au charme des années 2000.", "Your Telegram account, in a messenger with early-2000s charm."),
      [("Une fenêtre par conversation", "One window per conversation"),
       ("Émoticônes dessinées, photos envoyées et reçues", "Hand-drawn emoticons, photos sent and received"),
       ("Notifications et statut en ligne", "Notifications and online status")],
      small=("Client non officiel ; Telegram est une marque de son propriétaire.", "Unofficial client; Telegram is a trademark of its owner.")),
    C("web", "irc", "IRC", "irc-pm",
      ("Les salons de discussion IRC, présentés comme une messagerie moderne.", "IRC chat rooms, presented like a modern messenger.")),
    # ---- Create
    F("create", "paint", "Paint", "paint",
      ("Dessin et peinture sur calques.", "Drawing and painting on layers."),
      [("Pinceaux, aérographe, crayon gras, marqueur, calligraphie", "Brushes, airbrush, crayon, marker, calligraphy"),
       ("Calques et modes de fusion, dégradés, texte, sélections", "Layers and blend modes, gradients, text, selections"),
       ("Ouvre PNG, JPEG, GIF et BMP ; réglages de couleurs et filtres", "Opens PNG, JPEG, GIF and BMP; colour adjustments and filters")]),
    F("create", "koton", "Koton", "koton-crop",
      ("Le studio de musique. Composez un morceau en partant des accords.", "The music studio. Write a song starting from its chords."),
      [("Une piste d'accords qui guide l'accompagnement, la mélodie et la batterie", "A chord track that drives the accompaniment, the melody and the drums"),
       ("Instruments et effets en modules ; clavier MIDI USB reconnu", "Instruments and effects as plugins; a USB MIDI keyboard just works"),
       ("Export WAV ; un manuel complet en français et en anglais", "WAV export; a full manual in English and French")]),
    F("create", "3dforge", "3DForge", "3dforge",
      ("Concevez des pièces à imprimer en 3D, étape par étape.", "Design parts for 3D printing, step by step."),
      [("Dessinez une forme, donnez-lui du volume, assemblez, percez, arrondissez", "Sketch a shape, give it depth, join, cut, round the edges"),
       ("Chaque étape reste modifiable : changez une cote, la pièce se refait", "Every step stays editable: change a measure and the part rebuilds"),
       ("Préparation pour l'impression 3D et l'usinage", "Preparation for 3D printing and for machining")]),
    C("create", "screenshot", ("Capture d'écran", "Screenshot"), "screenshot-edit",
      ("Capturez une zone, une fenêtre ou tout l'écran, puis annotez.", "Capture an area, a window or the whole screen, then mark it up.")),
    C("create", "iconedit", ("Éditeur d'icônes", "Icon Editor"), None, ("Dessinez des icônes, pixel par pixel.", "Draw icons, pixel by pixel.")),
    C("create", "fmtracker", "FM Tracker", None,
      ("De la musique aux sonorités des ordinateurs d'autrefois, sur huit voix.", "Music with the sound of vintage computers, on eight voices.")),
    C("create", "mandelbrot", "Mandelbrot", None,
      ("Plongez dans les fractales, un clic après l'autre.", "Dive into fractals, one click at a time.")),
    # ---- Watch and listen
    F("media", "photos", "Photos", "photos",
      ("Toutes vos photos au même endroit, rangées par jour.", "All your photos in one place, sorted by day."),
      [("Albums, favoris et recherche par date ou par appareil", "Albums, favourites and search by date or camera"),
       ("Recadrer, redresser, ajuster la lumière, appliquer un filtre", "Crop, straighten, adjust the light, apply a filter"),
       ("Diaporama, envoi par courrier, fond d'écran en un clic", "Slideshow, send by mail, set as wallpaper in one click")]),
    F("media", "media", ("Lecteur multimédia", "Media Player"), "media-home",
      ("Votre musique et vos vidéos, dans une seule bibliothèque.", "Your music and your videos, in one library."),
      [("Musique par artiste, album ou genre ; vos listes de lecture", "Music by artist, album or genre; your own playlists"),
       ("Films et séries, repris là où vous vous étiez arrêté", "Films and series, resumed where you stopped"),
       ("Lit MP3, FLAC, OGG, MP4, MKV, WebM et d'autres", "Plays MP3, FLAC, OGG, MP4, MKV, WebM and more")]),
    C("media", "imageview", ("Visionneuse d'images", "Image Viewer"), None,
      ("Ouvrez une image d'un double-clic, zoomez, passez à la suivante.", "Open a picture with a double click, zoom, move on to the next.")),
    # ---- Play
    F("play", "pinball", ("Flipper", "Pinball"), "pinball-play",
      ("Un flipper à la physique réaliste.", "A pinball table with real physics."),
      [("Trois tables : station spatiale, manoir hanté, volcan", "Three tables: a space station, a haunted manor, a volcano"),
       ("Au clavier ou à la manette ; un classement par table", "Keyboard or gamepad; a top five for each table"),
       ("En français et en anglais", "In English and French")]),
    F("play", "critters", ("Bestioles", "Critters"), ("critters-play-fr", "critters-play"),
      ("Menez de petites créatures jusqu'à la sortie en confiant un rôle à certaines.", "Lead little creatures to the exit by giving some of them a job."),
      [("Grimpeur, bâtisseur, creuseur et d'autres", "Climber, builder, digger and more"),
       ("Douze niveaux, de l'entraînement à l'expédition", "Twelve levels, from training to expedition"),
       ("En français et en anglais", "In English and French")]),
    C("play", "solitaire", "Solitaire", "solitaire", ("La patience classique, cartes à glisser.", "Classic patience, with cards you drag.")),
    C("play", "freecell", "FreeCell", "freecell", ("Toutes les cartes visibles, 32\u00a0000 donnes numérotées.", "Every card face up, 32,000 numbered deals.")),
    C("play", "doom", "Doom", None,
      ("Le classique du jeu de tir, fourni avec les niveaux libres de Freedoom.", "The classic shooter, shipped with the free Freedoom levels.")),
    C("play", "arkanoid", "Arkanoid", None,
      ("Un casse-briques avec bonus, écrit en BASIC sur Onyx.", "A brick breaker with power-ups, written in BASIC on Onyx.")),
    C("play", "invaders", "Invaders", None,
      ("Repoussez la flotte avant qu'elle ne touche le sol.", "Hold off the fleet before it reaches the ground.")),
    C("play", "pipes", "Pipes", None, ("Posez les tuyaux avant que l'eau n'arrive.", "Lay the pipes before the water comes.")),
    C("play", "tetris", "Tetris", None, ("Empilez les pièces, complétez les lignes.", "Stack the pieces, clear the lines.")),
    C("play", "2048", "2048", "2048", ("Fusionnez les tuiles jusqu'à 2048.", "Merge the tiles up to 2048.")),
    C("play", "minesweeper", ("Démineur", "Minesweeper"), "minesweeper",
      ("Dégagez le terrain sans toucher une mine.", "Clear the field without touching a mine.")),
    C("play", "sokoban", "Sokoban", None, ("Poussez chaque caisse à sa place.", "Push every crate onto its spot.")),
    C("play", "snake", "Snake", None, ("Mangez, grandissez, ne vous mordez pas.", "Eat, grow, don't bite yourself.")),
    C("play", "same", "SameGame", None, ("Faites disparaître les groupes de même couleur.", "Clear groups of the same colour.")),
    C("play", "pong", "Pong", None, ("À deux sur le même clavier, premier à neuf.", "Two players on one keyboard, first to nine.")),
    C("play", "life", ("Jeu de la vie", "Game of Life"), None,
      ("Dessinez des cellules et regardez-les évoluer.", "Draw some cells and watch them evolve.")),
    F("play", "gamelib", ("Ludothèque", "Game Library"), "gamelib-crop",
      ("Tous vos jeux de console sur une seule étagère.", "All your console games on one shelf."),
      [("Une vignette par jeu, console par console", "A tile per game, sorted by console"),
       ("Lance le bon émulateur ; se pilote à la manette", "Starts the right emulator; works from a gamepad"),
       ("Surveille les dossiers que vous choisissez", "Watches the folders you choose")]),
    # ---- Learn and tinker
    F("learn", "turtle", "Turtle Quest", ("turtle-fr", "turtle"),
      ("Apprendre à programmer en guidant une tortue.", "Learn to program by guiding a turtle."),
      [("48 niveaux, des premiers pas aux fractales", "48 levels, from first steps to fractals"),
       ("Pas à pas, avec un indice et une leçon à chaque étape", "Step by step, with a hint and a lesson along the way"),
       ("En français (AVANCE, REPETE, SI) ou en anglais", "In English or French")]),
    F("learn", "circuits", "Circuits", ("circuits-fr", "circuits"),
      ("Un jeu d'énigmes pour comprendre comment un ordinateur calcule.", "A puzzle game about how a computer computes."),
      [("20 niveaux, d'un simple fil à un additionneur", "20 levels, from a single wire to an adder"),
       ("Le circuit s'anime en direct", "The circuit comes alive as you build"),
       ("En français et en anglais", "In English and French")]),
    F("learn", "qbstudio", "QBStudio", "qbstudio",
      ("Créez vos propres applications à fenêtres, en BASIC.", "Make your own windowed apps, in BASIC."),
      [("Dessinez la fenêtre à la souris", "Draw the window with the mouse"),
       ("Écrivez seulement ce que fait chaque bouton", "Write only what each button does"),
       ("Un clic, et elle rejoint vos applications", "One click and it joins your apps")]),
    C("learn", "qbasic", "QBasic", None,
      ("Le BASIC d'antan, avec son éditeur bleu et des exemples à lancer.", "Old-school BASIC, with its blue editor and examples to run.")),
    C("learn", "gpiolab", "GPIO Lab", "gpiolab",
      ("Les 40 broches du Raspberry Pi à l'écran : essayez une LED ou un bouton, même sans rien brancher.",
       "The Raspberry Pi's 40 pins on screen: try an LED or a button, even with nothing plugged in.")),
    C("learn", "courier", "Courier", "courier",
      ("Pour les développeurs web : envoyez des requêtes et examinez les réponses.", "For web developers: send requests and inspect the answers.")),
    # ---- The system
    F("system", "fileviewer", ("Fichiers", "File Viewer"), "fileviewer-crop",
      ("Vos dossiers en colonnes, avec aperçu.", "Your folders in columns, with a preview."),
      [("Glisser-déposer, copier, couper, coller", "Drag and drop, copy, cut, paste"),
       ("Corbeille avec restauration", "A Trash you can restore from"),
       ("Clés USB et dossiers favoris dans la barre latérale", "USB sticks and favourite folders in the sidebar")]),
    C("system", "control", ("Panneau de configuration", "Control Panel"), "control",
      ("Thème, écran, son, clavier, Wi-Fi, imprimantes : tous les réglages.", "Theme, display, sound, keyboard, Wi-Fi, printers: every setting.")),
    C("system", "pkgman", ("Gestionnaire de paquets", "Package Manager"), "pkgman-installed",
      ("Installez, mettez à jour et retirez des applications en un clic.", "Install, update and remove apps in one click.")),
    C("system", "disks", ("Disques", "Disks"), "disks",
      ("Vos cartes et clés USB : espace libre, éjection, formatage.", "Your cards and USB sticks: free space, eject, format.")),
    C("system", "taskman", ("Gestionnaire des tâches", "Task Manager"), "taskman",
      ("Ce qui tourne, la mémoire utilisée, et de quoi arrêter une application bloquée.", "What's running, the memory in use, and a way to stop an app that's stuck.")),
    C("system", "terminal", "Terminal", "terminal",
      ("La ligne de commande, pour ceux qui l'aiment.", "The command line, for those who like it.")),
]

# ----------------------------------------------------------------------------------------------
# Desktop
# kind: "window" (bare, on a stage), "screen" (in a display), "crop" (a strip), None (text only)
# ----------------------------------------------------------------------------------------------
DESKTOP_HERO = {
    "title": ("Un bureau qui se fait oublier.", "A desktop that gets out of your way."),
    "lead": ("Classique dans l'esprit, soigné dans le détail : Onyx reprend ce qui marche depuis toujours et l'adoucit.",
             "Classic in spirit, careful in the details: Onyx keeps what has always worked and softens the edges."),
    "shot": "notes-desktop",
    "alt": ("Le bureau d'Onyx : la barre de menus, les prochains rendez-vous sur le fond d'écran, deux fenêtres et le dock.",
            "The Onyx desktop: the menu bar, the next appointments on the wallpaper, two windows and the dock."),
}
DESKTOP_FEATURES = [
    {"title": ("En haut, l'essentiel.", "Up top, the essentials."),
     "body": ("Les menus de l'application en cours, l'heure, le son, le Wi-Fi. Un clic sur l'heure ouvre le calendrier du mois, un clic sur le Wi-Fi vous connecte à un réseau, sans redémarrer.",
              "The current app's menus, the time, the sound, the Wi-Fi. Click the time for the month's calendar, click the Wi-Fi to join a network, no restart needed."),
     "kind": "crop", "shot": "menubar",
     "alt": ("La barre de menus avec un menu ouvert ; à droite, le son, le Wi-Fi et l'heure.",
             "The menu bar with a menu open; on the right, the sound, the Wi-Fi and the time.")},
    {"title": ("En bas, vos applications.", "Down below, your apps."),
     "body": ("Un tiroir par famille : bureautique, Internet, graphisme, jeux. Un clic lance l'application ou la ramène devant vous. Déposez un fichier sur une icône pour l'ouvrir, sur la corbeille pour le jeter.",
              "One drawer per family: productivity, internet, graphics, games. A click starts an app or brings it back in front of you. Drop a file on an icon to open it, on the Trash to throw it away."),
     "kind": "crop", "shot": "dock",
     "alt": ("Le dock, avec le tiroir des jeux ouvert au-dessus de lui.", "The dock, with the games drawer open above it.")},
    {"title": ("Vos rendez\u2011vous et vos pense\u2011bêtes, sous les yeux.", "Your appointments and reminders, in plain sight."),
     "body": ("Les prochains rendez-vous du calendrier s'affichent sur le fond d'écran. Épinglez une note, elle apparaît à côté. Un clic ouvre le jour ou la note.",
              "Your next calendar appointments show right on the wallpaper. Pin a note and it appears beside them. A click opens the day or the note."),
     "kind": "crops", "shot": ["agenda", "stickies"],
     "alt": ("Le bureau avec les prochains rendez-vous et trois notes épinglées sur le fond d'écran, derrière la fenêtre de Notes.",
             "The desktop with the next appointments and three pinned notes on the wallpaper, behind the Notes window.")},
    {"title": ("Vos fichiers, colonne après colonne.", "Your files, column by column."),
     "body": ("Chaque dossier s'ouvre dans la colonne suivante : vous voyez toujours d'où vous venez. Un aperçu des images et des textes, une corbeille qui ne perd rien, le glisser-déposer partout. Branchez une clé USB, elle apparaît toute seule.",
              "Each folder opens in the next column, so you always see where you came from. Previews of pictures and text, a Trash that loses nothing, drag and drop everywhere. Plug in a USB stick and it shows up by itself."),
     "kind": "window", "shot": "fileviewer-crop",
     "alt": ("Le gestionnaire de fichiers : des dossiers en colonnes et l'aperçu d'un fichier texte.",
             "The File Viewer: folders in columns and the preview of a text file.")},
    {"themes": True,
     "title": ("Pêche, acier, sauge ou café noir.", "Peach, steel, sage or dark coffee."),
     "body": ("Sept ambiances prêtes à l'emploi, classiques ou modernes, ou vos propres couleurs. Huit motifs de fond d'écran qui se teintent à votre goût, ou l'une de vos photos.",
              "Seven ready-made looks, classic or modern, or colours of your own. Eight wallpaper patterns that take any tint you like, or one of your photos."),
     # swatch: the scheme's name in the system, its colour, the capture, the alt
     "swatches": [
         ("Peach", "#F2B27E", "desktop",
          ("Le bureau dans l'ambiance Peach : des cadres de fenêtre pêche, un style classique.",
           "The desktop in the Peach look: peach window frames, a classic style.")),
         ("Milk", "#ECEAE4", "milk",
          ("Le bureau dans l'ambiance Milk : des fenêtres claires aux boutons ronds, un style moderne.",
           "The desktop in the Milk look: light windows with round buttons, a modern style.")),
     ]},
    {"title": ("Tous les réglages au même endroit.", "Every setting in one place."),
     "body": ("Écran, son, clavier, Wi-Fi, imprimantes, manettes : le Panneau de configuration réunit tout. Onyx parle anglais et français, et connaît huit dispositions de clavier.",
              "Display, sound, keyboard, Wi-Fi, printers, gamepads: the Control Panel gathers it all. Onyx speaks English and French, and knows eight keyboard layouts."),
     "small": ("La traduction française avance application par application ; certaines sont encore en anglais.",
               "The French translation is arriving app by app; some apps are still in English only."),
     "kind": "window", "shot": "control",
     "alt": ("Le Panneau de configuration : thème, écran, son, clavier, langue, imprimantes, Wi-Fi, paquets.",
             "The Control Panel: theme, display, sound, keyboard, language, printers, Wi-Fi, packages.")},
    {"title": ("Les mises à jour viennent à vous.", "Updates come to you."),
     "body": ("Chaque jour, Onyx regarde s'il y a du nouveau et vous le signale. Choisissez pour chaque application : mise à jour automatique, sur demande, ou jamais. Le système, lui, se met à jour au redémarrage suivant.",
              "Every day Onyx looks for what is new and lets you know. Choose for each app: update automatically, on request, or never. The system itself updates at the next restart."),
     "kind": "window", "shot": "pkgman-installed",
     "alt": ("Le Gestionnaire de paquets : les applications installées, chacune avec son choix de mise à jour.",
             "The Package Manager: the installed apps, each with its update choice.")},

]
# The sections no capture exists for (assets.md): text-led, as cards with a line icon.
# ("Onyx, from your PC" -- Onyx Remote -- is left out: remote access has no password, see the user guide.)
DESKTOP_ALSO = [
    ("spaces", ("Plusieurs bureaux en un.", "Several desktops in one."),
     ("Quatre espaces de travail par défaut, jusqu'à six. Le courrier ici, un document là : chaque chose a sa place, et un raccourci clavier vous fait passer de l'un à l'autre.",
      "Four workspaces by default, up to six. Mail here, a document there: each thing has its place, and a keyboard shortcut takes you from one to the next."), None),
    ("printer", ("Imprimez, tout simplement.", "Printing, the simple way."),
     ("Onyx trouve les imprimantes de votre réseau compatibles AirPrint, sans pilote à installer. Sinon, tout s'imprime en PDF.",
      "Onyx finds the AirPrint-compatible printers on your network, with no driver to install. If you don't have one, everything prints to PDF."), None),
]
DESKTOP_CTA = {"title": ("Prêt à l'essayer ?", "Ready to try it?")}

# ----------------------------------------------------------------------------------------------
# Get Onyx
# ----------------------------------------------------------------------------------------------
GET_HERO = {
    "title": ("Onyx chez vous, en quatre étapes.", "Onyx at home, in four steps."),
    "lead": ("Il vous faut un Raspberry Pi 4 et une carte microSD. Le reste, Onyx s'en occupe.",
             "You need a Raspberry Pi 4 and a microSD card. Onyx takes care of the rest."),
    "button": ("Télécharger Onyx", "Download Onyx"),
}
GET_NEED = {
    "title": ("Ce qu'il vous faut", "What you need"),
    "items": [   # line icon, text
        ("pi", ("Un Raspberry Pi 4", "A Raspberry Pi 4")),
        ("card", ("Une carte mémoire microSD", "A microSD memory card")),
        ("display", ("Un écran HDMI et son câble micro\u2011HDMI", "An HDMI display and a micro-HDMI cable")),
        ("keyboard", ("Un clavier et une souris USB", "A USB keyboard and mouse")),
        ("wifi", ("Une connexion Wi-Fi, pour le web et les mises à jour", "A Wi-Fi connection, for the web and the updates")),
    ],
    "optional": ("En option : une manette USB pour les jeux, un casque USB, une imprimante réseau.",
                 "Optional: a USB gamepad for the games, a USB headset, a network printer."),
    "card_note": ("La carte doit être au format FAT32.", "The card needs to be in the FAT32 format."),
    "unsupported": ("Le Raspberry Pi 5 n'est pas encore pris en charge.", "The Raspberry Pi 5 isn't supported yet."),
}
GET_STEPS = {
    "label": ("Les quatre étapes", "The four steps"),
    "steps": [
        (("Préparez la carte", "Prepare the card"),
         ("Téléchargez Onyx et copiez tout son contenu sur la carte microSD. Aucun logiciel d'installation n'est nécessaire : c'est une simple copie de fichiers.",
          "Download Onyx and copy everything in it onto the microSD card. No installer needed: it's a plain copy of files.")),
        (("Branchez et allumez", "Plug in and power on"),
         ("Glissez la carte dans le Raspberry Pi, branchez l'écran, le clavier et la souris, puis l'alimentation. Onyx démarre.",
          "Slide the card into the Raspberry Pi, connect the display, the keyboard and the mouse, then the power. Onyx starts.")),
        (("Laissez-vous guider", "Follow the guide"),
         ("Au premier démarrage, l'assistant vous demande votre langue, votre clavier, votre réseau Wi-Fi, la taille de l'écran et vos couleurs. Tout se change plus tard dans le Panneau de configuration.",
          "On first start, Setup asks for your language, your keyboard, your Wi-Fi network, the screen size and your colours. Everything can be changed later in the Control Panel.")),
        (("Complétez et restez à jour", "Add more, stay current"),
         ("Ouvrez le Gestionnaire de paquets pour installer Jet Browser et les autres applications proposées. Ensuite, Onyx vérifie les mises à jour chaque jour et vous prévient.",
          "Open the Package Manager to install Jet Browser and the other apps on offer. From then on, Onyx checks for updates every day and lets you know.")),
    ],
    "shot": "setup-0",
    "alt": ("L'assistant du premier démarrage : la page d'accueil, avec le choix de la langue et les étapes à venir.",
            "The first-start assistant: its welcome page, with the choice of language and the steps to come."),
}
GET_FAQ = {
    "title": ("Bon à savoir", "Good to know"),
    "items": [
        (("Faut-il effacer ma carte ?", "Do I have to erase my card?"),
         ("Utilisez une carte dédiée à Onyx : c'est elle qui fait démarrer le Raspberry Pi. Vos fichiers personnels peuvent aussi vivre sur une clé USB.",
          "Use a card set aside for Onyx: it's what the Raspberry Pi starts from. Your own files can also live on a USB stick.")),
        (("Puis-je revenir en arrière ?", "Can I go back?"),
         ("Oui. Onyx vit entièrement sur sa carte : remettez votre carte habituelle dans le Raspberry Pi et rien n'a changé.",
          "Yes. Onyx lives entirely on its card: put your usual card back in the Raspberry Pi and nothing has changed.")),
        (("Mes fichiers sont-ils lisibles ailleurs ?", "Can other computers read my files?"),
         ("Oui. La carte et les clés USB utilisent les formats courants de Windows et des appareils photo, et vos documents sont enregistrés en .docx, .xlsx, .pptx, PDF, PNG ou JPEG.",
          "Yes. The card and USB sticks use the common formats of Windows and cameras, and your documents are saved as .docx, .xlsx, .pptx, PDF, PNG or JPEG.")),
    ],
}
GET_PC = {       # shown only when ONYX_REMOTE_URL and KOTON_WINDOWS_URL are filled
    "title": ("Sur votre PC aussi", "On your PC too"),
    "body": ("Onyx Remote affiche les fenêtres de votre Raspberry Pi sur un PC sous Windows. Koton, le studio de musique, existe aussi pour Windows : vos morceaux passent de l'un à l'autre.",
             "Onyx Remote shows your Raspberry Pi's windows on a Windows PC. Koton, the music studio, also exists for Windows: your songs move from one to the other."),
    "remote": ("Onyx Remote pour Windows", "Onyx Remote for Windows"),
    "koton": ("Koton pour Windows", "Koton for Windows"),
}

ROOT = {
    "title": "Onyx",
    "lead": ("Un ordinateur complet. Sur un Raspberry Pi 4.", "A complete computer. On a Raspberry Pi 4."),
}

# Alt texts: the asset curator's are in alts.py; the ones here are for the captures made by cutting one of
# those (make_assets.py), and win over alts.py. A capture in neither gets the text its section gives.
ALTS = {
    "clock-world": ("L'Horloge d'Onyx : l'heure locale en grand et celle de Tokyo, New York et Londres.",
                    "The Onyx Clock: the local time in large figures and the time in Tokyo, New York and London."),
    "clock-world-fr": ("L'Horloge d'Onyx : l'heure locale en grand et celle de Tokyo, New York et Londres.",
                       "The Onyx Clock: the local time in large figures and the time in Tokyo, New York and London."),
    "fileviewer-crop": ("Le gestionnaire de fichiers d'Onyx : la barre latérale et les dossiers en colonnes.",
                        "The Onyx File Viewer: the sidebar and the folders in columns."),
    "gamelib-crop": ("La Ludothèque d'Onyx : une vignette par jeu, classées par console.",
                     "The Onyx Game Library: one tile per game, sorted by console."),
    "koton-crop": ("Koton, le studio de musique d'Onyx : cinq pistes colorées, la piste d'accords et l'éditeur d'accord avec son clavier.",
                   "Koton, the Onyx music studio: five coloured tracks, the chord track and the chord editor with its keyboard."),
}
# How a cut capture stands on its stage: its cut edge runs off the stage ("r" = the right, "tl" = top and left).
CUTS = {"fileviewer-crop": "r", "gamelib-crop": "tl"}
