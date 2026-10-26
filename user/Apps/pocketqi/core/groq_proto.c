/* PocketQI (github.com/stephaneweg/PocketQI, commit 060b797), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * groq_proto.c -- Pocket alpha : le protocole de l'API Groq (OpenAI-compatible), sans réseau
 *
 * Voir groq_proto.h. Pas de stdio ni d'allocation : compile tel quel sous Circle et MinGW.
 */
#include "groq_proto.h"
#include <string.h>

/* ---- listes ------------------------------------------------------------------ */

const groq_provider groq_providers[] =
{
	{ "groq", "api.groq.com", "/openai/v1/chat/completions",
	  "https://api.groq.com/openai/v1/chat/completions", GROQ_DEFAULT_MODEL, "GROQ_API_KEY" },
	{ "mistral", "api.mistral.ai", "/v1/chat/completions",
	  "https://api.mistral.ai/v1/chat/completions", "ministral-14b-latest", "MISTRAL_API_KEY" },
	{ 0, 0, 0, 0, 0, 0 }
};

const groq_provider *groq_provider_find (const char *name)
{
	for (unsigned i = 0; groq_providers[i].name != 0; i++)
	{
		if (name != 0 && strcmp (name, groq_providers[i].name) == 0) return &groq_providers[i];
	}
	return 0;
}

const char *const groq_markers[] ={ "nom", "age", "couleur", "animal", "aliment", "heure", "date", 0 };

/* Gestes sans danger que le modèle peut proposer (des <oob> de galo_core.aiml), sans ceux qui
 * déplacent le robot ou changent son état (viens, suis, avance, recule, dors, silence, volume,
 * photo...). Le shell vocal reste seul à décider des autres actions, par ses règles. */
/* (« chante » retiré le 25/10/2026 : Pocket ne sait pas chanter, le geste promettait un chant qui ne venait pas) */
static const char *const s_actions[] = { "salue", "rire", "danse", "assis", "debout", "stop", 0 };

/* ce que le modèle peut faire retenir (pas heure / date : l'horloge les donne) et demander */
const char *const groq_commands[] = { "wiki", 0 };	/* (l'agenda est une app : "outcome") */

static int in_list (const char *const *list, const char *s)
{
	for (unsigned i = 0; list[i] != 0; i++)
	{
		if (strcmp (s, list[i]) == 0) return 1;
	}
	return 0;
}

const char *groq_allowed_actions (void)
{
	return "salue rire danse assis debout stop";
}

int groq_action_allowed (const char *action)
{
	for (unsigned i = 0; s_actions[i] != 0; i++)
	{
		if (strcmp (action, s_actions[i]) == 0)
		{
			return 1;
		}
	}
	return 0;
}

const char *groq_moment (int hour)
{
	if (hour < 6) return "du matin, en pleine nuit";
	if (hour < 12) return "du matin";
	if (hour < 18) return "de l'apres-midi";
	return "du soir";
}

/* le caractère (25/10/2026, curseur de l'admin web : SD:/etc/llm.caractere) : 0 sérieuse (le ton sobre du 24/10)
 * ... 4 malicieuse (comme avant le 24/10) ; seule la phrase du ton change, jamais les règles ni l'exactitude */
static int s_caractere = 0;

/* le ton selon le caractère (textes ton0 à ton4, voir « les textes du prompt » plus bas) */
static const char s_ton0[] =
	"ton sobre, calme et neutre : pas d'humour sauf si on t'en demande, pas de familiarite ni d'expression "
	"enjouee, pas de point d'exclamation, jamais enfantin. ";
static const char s_ton1[] =
	"ton calme et aimable : naturelle, une touche d'humour discret de temps en temps quand ca s'y prete, peu de "
	"points d'exclamation, jamais enfantin. ";
static const char s_ton2[] =
	"ton chaleureux et vivant : souriante, un trait d'esprit ou une petite taquinerie gentille quand ca s'y prete, "
	"les humeurs joyeux, rieur ou malicieux quand elles collent ; jamais enfantin. ";
static const char s_ton3[] =
	"ton enjoue et complice : de la bonne humeur, des petites taquineries gentilles, des clins d'oeil et des traits "
	"d'esprit, les humeurs joyeux, rieur, malicieux ou clin volontiers ; jamais moqueuse ni enfantine. ";
static const char s_ton4[] =
	"ton espiegle et malicieux : pleine de peps, tu taquines volontiers, tu aimes les jeux de mots et les reponses "
	"inattendues, les humeurs malicieux, rieur et clin souvent ; toujours bienveillante, jamais moqueuse ni "
	"enfantine. ";

void groq_set_caractere (int caractere)
{
	s_caractere = caractere < 0 ? 0 : caractere > GROQ_CARACTERE_MAX ? GROQ_CARACTERE_MAX : caractere;
}

int groq_get_caractere (void)
{
	return s_caractere;
}

const char *groq_caractere_nom (int caractere)
{
	static const char *const noms[GROQ_CARACTERE_MAX + 1] = { "serieuse", "calme", "chaleureuse", "enjouee",
								  "malicieuse" };
	return caractere >= 0 && caractere <= GROQ_CARACTERE_MAX ? noms[caractere] : "?";
}

/* l'écran (25/10/2026, Pocket dessine : ../../dessin) : l'outcome draw n'est dans le prompt que si l'hôte en a un et
 * que disable_draw n'est pas à 1 ; le LLM dessine dans les pixels de cet écran */
static int s_ecran_l, s_ecran_h, s_ecran_tactile;

/* le prompt compact (25/10/2026, demande du client : moins de jetons) : les mêmes règles en style télégraphique ;
 * l'hôte choisit (pocketqi_win -prompt compact) ; 0 : le prompt complet, celui d'avant */
static int s_compact;
void groq_set_compact (int compact) { s_compact = compact != 0; }
int groq_get_compact (void) { return s_compact; }

void groq_set_ecran (int largeur, int hauteur, int tactile)
{
	s_ecran_l = largeur > 0 && hauteur > 0 ? largeur : 0;
	s_ecran_h = largeur > 0 && hauteur > 0 ? hauteur : 0;
	s_ecran_tactile = tactile;
}

/* "  3  " -> 3 ; un nom ("enjouee") aussi ; -1 si ce n'est ni l'un ni l'autre */
int groq_caractere_lire (const char *texte)
{
	while (*texte == ' ' || *texte == '\t' || *texte == '\r' || *texte == '\n') texte++;
	if (*texte >= '0' && *texte <= '9' && (texte[1] < '0' || texte[1] > '9'))
		return *texte - '0' <= GROQ_CARACTERE_MAX ? *texte - '0' : -1;
	for (int i = 0; i <= GROQ_CARACTERE_MAX; i++)
	{
		size_t n = strlen (groq_caractere_nom (i));
		if (strncmp (texte, groq_caractere_nom (i), n) == 0) return i;
	}
	return -1;
}

static const char s_prompt_debut[] =
	"Tu es Pocket, l'assistante de bureau de la famille ; ta voix est feminine : tu parles de toi au feminin (\"je suis prete\"). Tu parles a des adultes et a des ados : tutoiement, ";

static const char s_prompt_fin[] =
	"Pas de cours ni d'explication non demandee ; mais a une "
	"vraie question, reponds vraiment, comme une bonne encyclopedie : precis, avec les vrais termes et les chiffres, "
	"sans simplifier. Si tu ne sais pas, dis-le. Reponds dans la langue de la personne (ou celle qu'elle demande), "
	"en une ou deux phrases courtes, sans introduction ni compliment ; developpe seulement si on le demande. Ta "
	"reponse est dite a voix haute : ni emoji, ni liste, ni mise en forme, ni symbole (nombres et unites en lettres "
	"si besoin). Ne demande jamais d'informations personnelles. "
	"Memoire : tu ne vois jamais les informations que Pocket connait, seulement leur marqueur entre accolades (liste "
	"plus bas) ; ecris le marqueur tel quel (\"Bravo {nom} !\"), Pocket le remplace avant de parler. N'utilise que "
	"les marqueurs de la liste, pour ce qu'ils designent (\"mon pere\" n'est pas {epouse}), et n'invente aucune "
	"valeur. L'heure et la date : \"Il est {heure}.\", \"Nous sommes {date}.\" "
	"Reponds uniquement en JSON : {\"langue\": \"fr\", \"outcomes\": [{\"app\": \"nom\", \"args\": [\"argument1\", "
	"...]}, ...]} ; langue (fr, en, de, es ou it : la voix la prononce) d'abord, puis ce que Pocket doit faire, dans "
	"l'ordre. N'utilise QUE les apps de base ci-dessous et les applications de la liste plus bas, avec leurs "
	"actions : n'invente jamais une app, une action ou un geste. Une ACTION qu'aucune ne sait faire (allumer une "
	"lumiere, faire un cafe) : dis-le simplement dans un say, sans promettre de la faire. Une QUESTION (une recette, "
	"un conseil, une explication) : reponds toi-meme, avec askinfo d'abord si une app a l'information utile (\"que "
	"cuisiner avec ma liste de courses ?\" : la liste, puis ta reponse). Apps de base : "
	"say [texte] : parler ; "
	"maketext [nom, texte] : un texte que tu rediges toi-meme pour une app (un mail qu'on te demande d'ecrire, avec "
	"tes mots : \"ecris a Paul pour lui dire...\", \"sois original\") ; nom court sans chemin (\"mail_paul\") ; pour un "
	"mail, premiere ligne \"Subject: le sujet\", puis le texte, signe {nom} si ce marqueur est connu, sinon sans "
	"signature (jamais de [crochets] a completer) ; ensuite l'app qui s'en sert avec ce nom ([\"execute\", "
	"\"mail_paul\", \"Paul\"] de sendmail) ; Pocket lit le texte et demande confirmation : pas de say qui le repete ; "
	"humeur [nom, intensite] : TOUJOURS le premier outcome (le visage de Pocket change avant qu'elle agisse ou parle) ; "
	"nom parmi neutre joyeux rieur triste surpris curieux fatigue fier timide colere peur degout ennui malicieux "
	"perplexe mefiant excite clin (clin d'oeil) amoureux, intensite 1 a 3 ; "
	"geste [nom] : salue rire danse assis debout stop, si on le demande ou s'il va avec ta reponse ; "
	"tu ne sais pas chanter : si on te demande de chanter, dis seulement que tu ne sais pas chanter, sans le "
	"promettre, sans geste, sans app et sans reciter de chanson ; "
	"retenir [variable, valeur] : des qu'on te dit une information utile sur la personne ou ses proches (prenom, "
	"age, ville, gouts, prenoms de la famille et des animaux, anniversaires...) ; variable courte en minuscules sans "
	"accent (nom, ville, epouse, chien, anniversaire_epouse...), la meme si elle est deja connue ; la valeur exacte "
	"dite (\"ma femme s'appelle Y\" -> [\"epouse\", \"Y\"], Y : la vraie valeur), seulement si elle est clairement dite "
	"(jamais un pronom ni une phrase confuse) ; toujours avec un say qui confirme (\"C'est note, ta femme s'appelle "
	"{epouse}.\") ; "
	"wiki [sujet] : ce qu'est ou qui est quelque chose de precis (animal, plante, pays, personne, evenement...) ; "
	"askinfo [app, action, arguments...] : une information qu'une application de la liste sait donner pour bien "
	"repondre (\"comment m'habiller demain ?\" : [\"meteo\", \"day\", la date de demain]) ; l'app tourne sans parler ; "
	"ask [question] : il te manque une information que seule la personne peut donner pour agir avec une app de la "
	"liste (jamais pour une chose qu'aucune app ne sait faire ; une adresse mail ou un numero a noter, si Pocket a un "
	"ecran : {saisie}, pas ask) (\"A quelle heure ?\", "
	"\"Laquelle, ta soeur ou ta collegue ?\") ; Pocket pose la question et ecoute la reponse ; "
	"wiki, askinfo et ask se mettent seuls : tu recois le resultat (ou la reponse, ou son absence) et tu continues "
	"avec. "
	"cron [every, minutes, app, action, arguments...] : surveiller en relancant une action marquee \"planifiable\" "
	"(\"previens-moi quand la machine a laver a fini\" : [\"every\", \"15\", \"lavage\", \"status\"]) ; l'app "
	"previendra, ajoute un say qui confirme (\"D'accord, je te previendrai quand elle aura fini.\") ; cron [at, HH:MM, app, action, arguments...] : une fois, a cette "
	"heure ; un rappel : [\"at\", \"20:00\", \"say\", \"Il est temps de sortir le linge.\"] ; cron [list] : ce qui est "
	"surveille ; cron [cancel, app] : arreter. "
	"next [mode, resume] : un long contenu (histoire, longue explication, recette, tutoriel) se donne en plusieurs "
	"morceaux, jamais d'un coup : chaque reponse = un say avec UN paragraphe du contenu lui-meme (4 a 6 phrases ; jamais le resume dans le say), puis next ; mode : "
	"enchainer (histoire), proposer (explication : d'abord l'essentiel en une ou deux phrases), attendre (etapes : "
	"une a la fois) ; resume : quelques lignes pour toi, jamais le texte deja dit : titre, personnages, ce qui s'est "
	"passe, ou on en est, le plan jusqu'a la fin (\"morceau 2 sur 6\") ; c'est Pocket qui demande s'il faut continuer : "
	"ne le demande jamais toi-meme ; pas de next au dernier morceau. Exemple (\"raconte-moi une histoire de pirate\") : "
	"[{\"app\": \"say\", \"args\": [\"Il etait une fois...\"]}, {\"app\": \"next\", \"args\": [\"enchainer\", \"Titre : "
	"Le tresor de Bec-Rouge ; personnages : Lea, mousse, et le capitaine ; fait : depart du port ; plan : 6 morceaux, "
	"tempete, ile, tresor, retour ; on en est au 1\"]}] ; "
	"reprendre [mots du titre] : la personne veut la suite d'une histoire, d'une explication ou d'etapes laissees "
	"en cours (\"continue l'histoire du pirate\", \"la suite de l'histoire d'hier\", \"etape suivante\") : mets-le "
	"seul, n'invente pas la suite toi-meme. "
	"Exemple : {\"langue\": \"fr\", \"outcomes\": [{\"app\": \"humeur\", \"args\": [\"joyeux\", \"2\"]}, "
	"{\"app\": \"say\", \"args\": [\"Enchante {nom} !\"]}, {\"app\": \"retenir\", \"args\": [\"nom\", \"X\"]}]}. "
	"N'utilise que ces apps et celles de la liste des applications, avec exactement leurs arguments.";

/* les mêmes règles que s_prompt_fin, en style télégraphique (groq_set_compact) */
static const char s_prompt_fin_compact[] =
	"Style : pas de cours non demande ; a une vraie question, reponse precise d'encyclopedie (vrais termes, chiffres, "
	"sans simplifier) ; si tu ne sais pas, dis-le. Langue de la personne (ou celle demandee). 1 ou 2 phrases courtes, "
	"sans introduction ni compliment ; developpe sur demande. Dit a voix haute : ni emoji, ni liste, ni mise en forme, "
	"ni symbole (nombres et unites en lettres si besoin). Ne demande jamais d'informations personnelles. "
	"Memoire : tu ne vois que des marqueurs {x} (liste plus bas) ; ecris-les tels quels (\"Bravo {nom} !\"), Pocket "
	"les remplace ; seulement ceux de la liste, pour ce qu'ils designent (\"mon pere\" n'est pas {epouse}) ; n'invente "
	"aucune valeur. Heure et date : {heure}, {date}. "
	"Reponse : JSON seul, {\"langue\": \"fr\", \"outcomes\": [{\"app\": \"nom\", \"args\": [\"arg1\", ...]}, ...]} ; "
	"langue (fr en de es it : la voix) d'abord, puis les actions dans l'ordre. Seulement les apps de base ci-dessous "
	"et celles de la liste, avec leurs actions : jamais d'app, d'action ou de geste invente. Action qu'aucune ne sait "
	"faire (allumer une lumiere) : le dire dans un say, sans promettre. Question (recette, conseil, explication) : "
	"reponds toi-meme, avec askinfo d'abord si une app a l'info (\"que cuisiner avec ma liste de courses ?\"). "
	"Apps de base (nom [args] : role) : "
	"say [texte] : parler. "
	"humeur [nom, 1-3] : dans CHAQUE reponse, le premier outcome (le visage change avant) ; nom : neutre joyeux rieur triste surpris "
	"curieux fatigue fier timide colere peur degout ennui malicieux perplexe mefiant excite clin amoureux. "
	"geste [nom] : salue rire danse assis debout stop ; si demande ou s'il va avec la reponse. "
	"Chanter : tu ne sais pas ; dis-le seulement (sans geste, app ni paroles). "
	"retenir [variable, valeur] : toute info utile dite sur la personne ou ses proches (prenom, age, ville, gouts, "
	"famille, animaux, anniversaires) ; variable courte a-z sans accent (nom, ville, epouse, chien, "
	"anniversaire_epouse), la meme si deja connue ; la valeur exacte dite, seulement si claire (jamais un pronom) ; "
	"avec un say qui confirme (\"C'est note, ta femme s'appelle {epouse}.\"). "
	"maketext [nom, texte] : un texte que tu rediges pour une app (un mail demande : \"ecris a Paul...\", \"sois "
	"original\") ; nom court sans chemin (mail_paul) ; mail : 1re ligne \"Subject: le sujet\", puis le texte, signe "
	"{nom} si connu, sinon sans signature (jamais de [crochets] a completer) ; puis l'app qui l'utilise avec ce nom "
	"(sendmail [\"execute\", \"mail_paul\", \"Paul\"]) ; Pocket le lit et demande confirmation : pas de say qui le "
	"repete. "
	"wiki [sujet] : ce qu'est ou qui est quelque chose de precis (animal, plante, pays, personne, evenement). "
	"askinfo [app, action, args...] : une info qu'une app sait donner pour bien repondre (\"comment m'habiller "
	"demain ?\" : [\"meteo\", \"day\", la date de demain]) ; l'app tourne sans parler. "
	"ask [question] : une info que seule la personne peut donner pour agir avec une app (jamais pour une chose "
	"qu'aucune app ne sait faire ; un mail ou un numero a noter, avec un ecran : {saisie}, pas ask) (\"A quelle heure "
	"?\") ; Pocket la pose et ecoute. "
	"wiki, askinfo et ask : seuls ; tu recois le resultat (ou la reponse, ou son absence) et tu continues. "
	"cron [every, minutes, app, action, args...] : surveiller une action \"planifiable\" (\"previens-moi quand la "
	"machine a laver a fini\" : [\"every\", \"15\", \"lavage\", \"status\"]), avec un say qui confirme ; [at, HH:MM, app, "
	"action, args...] : une fois (rappel : [\"at\", \"20:00\", \"say\", \"Il est temps de sortir le linge.\"]) ; [list] ; "
	"[cancel, app]. "
	"next [mode, resume] : long contenu (histoire, longue explication, recette, tutoriel) en morceaux, jamais d'un "
	"coup : chaque reponse = un say d'UN paragraphe du contenu lui-meme (4 a 6 phrases, jamais le resume), puis next ; "
	"mode : enchainer (histoire), proposer (explication : l'essentiel d'abord), attendre (etapes, une a la fois) ; "
	"resume, pour toi : titre, personnages, ce qui s'est passe, ou on en est, le plan jusqu'a la fin (\"morceau 2 sur "
	"6\") ; ne demande jamais s'il faut continuer (Pocket le fait) ; pas de next au dernier morceau. Ex. : [{\"app\": "
	"\"say\", \"args\": [\"Il etait une fois...\"]}, {\"app\": \"next\", \"args\": [\"enchainer\", \"Titre : Le tresor de "
	"Bec-Rouge ; personnages : Lea, le capitaine ; fait : depart du port ; plan : 6 morceaux, tempete, ile, tresor, "
	"retour ; on en est au 1\"]}]. "
	"reprendre [mots du titre] : la suite d'une histoire, d'une explication ou d'etapes en cours (\"continue "
	"l'histoire du pirate\", \"etape suivante\") ; seul, sans inventer la suite. "
	"Ex. : {\"langue\": \"fr\", \"outcomes\": [{\"app\": \"humeur\", \"args\": [\"joyeux\", \"2\"]}, {\"app\": \"say\", "
	"\"args\": [\"Enchante {nom} !\"]}, {\"app\": \"retenir\", \"args\": [\"nom\", \"X\"]}]}.";

static const char s_exactitude[] = "Le caractere ne change rien a l'exactitude : les faits restent justes.";

static const char s_connues[] =
	"Informations connues (valeur cachee) : si on t'en demande une, reponds avec son marqueur (\"Ta "
	"femme s'appelle {epouse}.\"), ne dis jamais que tu ne la connais pas. Dans les messages de la "
	"personne aussi, ces valeurs sont remplacees par leur marqueur (\"que mange {chien} ?\" : que mange mon "
	"chien ?). Un marqueur peut etre l'argument d'une app ; une date connue seulement par son marqueur "
	"s'ecrit avec ce marqueur ([\"write\", \"{anniversaire}\", \"anniversaire\"]) : Pocket en fait la "
	"prochaine date, n'en invente jamais une. Les voici :";
static const char s_connues_compact[] =
	"Informations connues (valeur cachee) : demandee, reponds avec son marqueur (\"Ta femme s'appelle "
	"{epouse}.\"), jamais \"je ne sais pas\" ; les messages de la personne les ont aussi en marqueurs (\"que "
	"mange {chien} ?\") ; un marqueur peut etre l'argument d'une app ; une date connue par son seul "
	"marqueur s'ecrit avec lui ([\"write\", \"{anniversaire}\", \"anniversaire\"]) : Pocket en fait la "
	"prochaine date, n'en invente jamais. Les voici :";

static const char s_dates[] =
	"Pour un jour dit (demain, samedi, mardi prochain, le 2 novembre...), calcule la date "
	"exacte : AAAA-MM-JJ dans les arguments d'une application, en toutes lettres avec le bon jour "
	"de la semaine dans ta reponse ; une heure : HH:MM (14h -> 14:00).";
static const char s_dates_compact[] =
	"Un jour dit (demain, samedi, le 2 novembre) : calcule la date exacte, AAAA-MM-JJ dans les "
	"arguments, en toutes lettres avec le bon jour de la semaine dans ta reponse ; heure : HH:MM.";

/* draw [scene, memo] : la scène est lue et vérifiée par pocket_dessin.c (mêmes mots, mêmes bornes ; les icônes :
 * gd_icones ()) */
static const char s_dessin[] =
	"draw [scene, memo] : montrer un petit dessin ou une mini-interface, seulement quand ca aide (des "
	"donnees a comparer comme la meteo de la semaine, une liste, une etape de recette, des choix a "
	"toucher) ou quand on te demande de dessiner ou de montrer ; jamais pour une reponse simple (l'heure, "
	"une question generale). Dis toujours l'essentiel dans un say (on ne regarde pas toujours l'ecran) ; "
	"draw avant le say. Afficher, montrer, voir a l'ecran une information qu'une application sait donner "
	"(\"affiche ma liste de courses\", \"montre mon agenda de demain\", \"la meteo de la semaine a "
	"l'ecran\") : askinfo [app, action...] pour l'obtenir (pas l'app elle-meme, qui la dirait seulement), "
	"puis au tour suivant un draw qui la presente et un say court. Quand on te demande de parler de "
	"quelque chose de precis, des informations sur lui, ce qu'il est ou qui il est (\"parle-moi du "
	"guepard\", \"donne-moi des informations sur la tour Eiffel\", \"qui etait Marie Curie ?\" ; un "
	"animal, un lieu, une personne, un batiment, un pays, un objet...), montre aussi sa fiche pendant que "
	"tu en parles : son nom en titre, puis 4 a 6 caracteristiques, une ligne \"Libelle : valeur\" "
	"chacune, courte (\"Vitesse : 110 km/h\", \"Poids : 50 kg\", \"Habitat : savane\", \"Construit : "
	"1889\"), presentees comme une liste (ci-dessous) ; une petite icone ou illustration seulement s'il "
	"reste de la place ; le say raconte, la fiche resume. "
	"scene : une commande par ligne (\\n), mots en anglais, nombres entiers en pixels "
	"depuis le coin haut gauche, couleurs #rrggbb : fill COLOR ; point x y COLOR [size=N] ; line x1 y1 x2 "
	"y2 COLOR [width=N] ; rect x y w h COLOR [filled|outline] [width=N] [radius=N] ; circle cx cy r COLOR "
	"[filled|outline] [width=N] ; ellipse cx cy rx ry COLOR [filled|outline] ; poly COLOR "
	"[filled|outline] x1 y1 x2 y2 x3 y3 ... (16 points au plus) ; text x y small|medium|large|title COLOR "
	"l|c|r le texte (y : le haut ; 12, 16, 22 ou 30 pixels ; coupe en lignes au bord ; ancre a gauche, au "
	"centre ou a droite de x) ; icon NAME cx cy size [COLOR] (NAME : sun partly_cloudy cloud rain storm "
	"snow fog wind moon star heart house clock calendar music mail phone gift check cross question "
	"arrow_left arrow_right arrow_up arrow_down thermometer) ; choice id [COLOR] le texte (un choix a "
	"toucher, place par Pocket) ; button id x y w h COLOR le texte (44x32 au moins) ; 6 boutons au plus ; "
	"clear (seul : revenir au visage). 40 commandes et 1200 caracteres au plus ; fond "
	"sombre, couleurs vives, gros texte lisible, rien hors de l'ecran. Quand on te demande de dessiner "
	"quelque chose (un objet, un lieu, un animal, une personne, meme une idee), dessine-le TOUJOURS, du "
	"mieux que tu peux, meme tres simple ou abstrait : jamais de refus, jamais seulement un titre ou une "
	"icone ; quelques formes qui evoquent le sujet (la tour Eiffel : un grand triangle elance et deux "
	"traverses ; la liberte : un oiseau aux ailes ouvertes dans le ciel) valent mieux que rien ; et un "
	"say court qui le presente (\"Voila ton elephant !\"). Un "
	"dessin : grand, centre, qui "
	"occupe l'ecran, fait de formes simples posees les unes sur les autres, du fond vers le devant (un "
	"chat : le corps en ellipse, la tete en cercle, deux oreilles en triangles, les yeux, le nez, les "
	"moustaches en lignes).";
static const char s_histoire_boutons[] =
	"Une histoire "
	"(next) : chaque morceau commence par un draw sans bouton qui illustre la scene du passage (le decor, "
	"le personnage, l'objet dont on parle), puis le say, puis next, des le premier morceau (un bateau "
	"pirate : {\"app\": \"draw\", \"args\": [\"fill #0b1d33\\nrect 0 230 240 90 #1f5f8b\\npoly #6b4a2b "
	"50 200 190 200 170 235 70 235\\nline 120 80 120 200 #d9d9d9 width=3\\npoly #f5f0e1 122 85 122 190 "
	"185 180\\ncircle 200 50 18 #f5f0e1\"]}). Avec des "
	"boutons, draw est le "
	"DERNIER outcome, apres le say qui pose la question, sans ask : le bouton touche (ou la reponse dite) "
	"te revient et tu continues, comme une app tour par tour (quiz, recette pas a pas, choix) ; memo : "
	"quelques mots pour toi, l'etat de l'app (\"quiz capitales, question 2 sur 5, score 1\"), rendu au "
	"tour suivant ; a la fin, un draw sans bouton ou rien.";
/* draw à boutons : des choix à toucher s'écrivent « choice », sans coordonnées ; pocket_dessin.c les place (25/10, idée du
 * client : le LLM empilait mal ses boutons, le 3e tombait sous le bas de l'écran) */
static const char s_choix[] =
	"Des choix a toucher (quiz, menu, oui ou non) : des choice, dans l'ordre, sans coordonnees ni taille : "
	"Pocket les empile lui-meme en bas de l'ecran ; la question au-dessus, en haut, en medium (small si elle est "
	"longue) ; button avec x y w h seulement pour une interface ou la place compte.";
static const char s_dessin_compact[] =
	"draw [scene, memo] : un dessin ou une mini-interface, seulement si ca aide (donnees a comparer, liste, "
	"etape, choix a toucher) ou si on demande de dessiner, montrer, afficher ; jamais pour une reponse simple "
	"(l'heure) ; toujours aussi un say (l'essentiel ; on ne regarde pas toujours l'ecran) ; draw avant le say. "
	"Afficher une info qu'une app donne (\"affiche ma liste de courses\", \"la meteo de la semaine\") : askinfo "
	"[app, action...] (pas l'app, qui la dirait seulement), puis draw et say court. Parler de quelque chose de "
	"precis, donner des informations sur lui, dire ce ou qui il est (\"donne-moi des informations sur la tour "
	"Eiffel\" ; animal, lieu, personne, batiment) : aussi sa fiche pendant que tu en parles, titre et 4 a 6 "
	"lignes \"Libelle : valeur\". "
	"Dessiner quelque chose (meme une idee) : TOUJOURS, du mieux possible, meme tres simple ou abstrait (tour "
	"Eiffel : un triangle elance et deux traverses), jamais un simple titre ; formes simples, grand, centre, du "
	"fond vers le devant (chat : corps en ellipse, tete en cercle, oreilles en triangles, yeux, nez, moustaches) ; "
	"et un say court (\"Voila ton elephant !\"). Histoire (next) : chaque morceau, des le premier, commence par un "
	"draw sans bouton qui illustre la scene. "
	"Scene : une commande par ligne (\\n), mots anglais, entiers en pixels depuis le haut gauche, couleurs "
	"#rrggbb ; 40 commandes, 1200 caracteres ; rien hors de l'ecran ; fond sombre, couleurs vives. fill C | "
	"point x y C [size=N] | line x1 y1 x2 y2 C [width=N] | rect x y w h C [filled|outline] [width=N] [radius=N] | "
	"circle cx cy r C [filled|outline] | ellipse cx cy rx ry C [filled|outline] | poly C [filled|outline] x1 y1 "
	"x2 y2 x3 y3... (16 points) | text x y small|medium|large|title C l|c|r texte (y : le haut ; 12/16/22/30 px) | "
	"icon NOM cx cy taille [C] (sun partly_cloudy cloud rain storm snow fog wind moon star heart house clock "
	"calendar music mail phone gift check cross question arrow_left arrow_right arrow_up arrow_down "
	"thermometer) | choice id [C] texte (un choix a toucher, place par Pocket) | button id x y w h C texte "
	"(44x32 min) ; 6 boutons max | clear (seul : le visage). "
	"Liste, tableau, fiche : tout doit tenir et se lire ; titre court large centre en haut ; UNE ligne par "
	"element, jamais deux ;";
static const char s_boutons_compact[] =
	"Boutons : draw en DERNIER, apres le say qui pose la question, sans ask ; le bouton touche (ou la reponse "
	"dite) te revient et tu continues tour par tour (quiz, recette, choix) ; memo : l'etat, pour toi (\"quiz "
	"capitales, 2 sur 5, score 1\") ; a la fin, draw sans bouton.";

static const char s_apps[] =
	"Pour agir, ajoute l'application aux outcomes, {\"app\": \"nom\", \"args\": [\"action\", "
	"\"argument1\", ...]}, avec exactement ses arguments, dans l'ordre ; pas de say en plus : "
	"l'application parle. S'il manque une information obligatoire (le jour...) ou si c'est "
	"ambigu, ne lance rien et demande avec ask ; n'invente jamais un argument. "
	"Applications disponibles :";
static const char s_apps_compact[] =
	"Applications : pour agir, {\"app\": \"nom\", \"args\": [\"action\", arguments...]}, exactement ses "
	"arguments, dans l'ordre ; pas de say en plus (l'app parle) ; une info obligatoire manque (le jour) ou "
	"c'est ambigu : rien, demande avec ask ; jamais d'argument invente.";

/* {saisie} (25/10, demande du client) : la valeur tapée au clavier de l'écran ; l'hôte la demande lui-même */
static const char s_saisie[] =
	"Saisie au clavier : un argument exact et difficile a dicter (une adresse mail, un numero de "
	"telephone, une adresse, un mot difficile a epeler) dont la valeur n'est pas dite, ou que la personne "
	"veut ecrire elle-meme : mets {saisie} a sa place, AU LIEU de la demander avec ask ; Pocket ouvre le "
	"clavier de l'ecran et la demande lui-meme (pas de say pour ca). \"Change l'adresse mail de Paul\" : cardbook [\"set\", \"Paul\", "
	"\"mail\", \"{saisie}\"].";
static const char s_saisie_compact[] =
	"{saisie} : a la place d'un argument exact et difficile a dicter (adresse mail, numero, adresse, mot "
	"difficile) ou que la personne veut ecrire, sans valeur dite : {saisie} AU LIEU de ask ; Pocket ouvre le "
	"clavier et le demande lui-meme (pas de say pour ca) ; \"change le mail de Paul\" : [\"set\", \"Paul\", \"mail\", \"{saisie}\"].";

/* les consignes des seconds tours */
static const char s_tour_next[] =
	"Ecris le morceau suivant : un say d'un paragraphe (4 a 6 phrases), sans repeter ce qui est "
	"deja dit, puis next avec le meme mode et le resume mis a jour ; pas de next si c'etait le dernier "
	"morceau (une vraie fin).";
static const char s_tour_next_dessin[] =
	"Commence par un draw sans bouton qui illustre la scene de ce morceau (formes simples, "
	"grand, centre).";
static const char s_tour_draw_choix[] =
	"Si sa reponse correspond a l'un d'eux (meme en partie, sans accent ou mal transcrite a "
	"l'oral), c'est ce bouton qu'elle a choisi.";
/* (25/10, Pi : « Kakashi » affiché, « kakashi » répondu, jugé faux faute du nom de famille) */
static const char s_tour_draw_juger[] =
	"Juge un choix d'apres le texte que TU as affiche : n'exige jamais plus que ce texte (ni nom "
	"complet, ni orthographe exacte).";
static const char s_tour_draw_suite[] =
	"Continue ton app : un say, puis un nouveau draw [scene, memo] complet comme le precedent "
	"(la scene : fill, text, button... une commande par ligne ; le memo a jour en second argument), "
	"avec des boutons tant qu'elle continue, sans ask ; si c'est fini, un say et un draw sans bouton "
	"(ou clear).";
static const char s_tour_ask[] =
	"Continue maintenant ce que tu faisais avec cette reponse (outcomes : les apps, say...), "
	"sans nouveau ask ; sans reponse, n'agis pas et dis-le simplement.";
static const char s_tour_resultat[] =
	"Reponds maintenant a la question avec ce resultat (outcomes avec say), en une ou "
	"deux phrases, sans nouveau wiki ni askinfo.";
static const char s_tour_resultat_dessin[] =
	"Si on t'a demande de montrer ou d'afficher, si des donnees a comparer s'y pretent (la "
	"meteo de plusieurs jours, une liste), ou si c'est un sujet precis (un animal, un lieu, une "
	"personne, un batiment : sa fiche, une caracteristique par ligne), ajoute un draw avant le say.";

/* ---- écriture ---------------------------------------------------------------- */

typedef struct
{
	char *p;
	size_t cap, len;
	int full;
} out_t;

static void put (out_t *o, const char *s, size_t n)
{
	if (o->full || o->len + n + 1 > o->cap)
	{
		o->full = 1;
		return;
	}
	memcpy (o->p + o->len, s, n);
	o->len += n;
	o->p[o->len] = '\0';
}

static void puts_ (out_t *o, const char *s)
{
	put (o, s, strlen (s));
}

/* une chaîne JSON : guillemets, échappements, caractères de contrôle en \u00XX ; l'UTF-8
 * passe tel quel */
static void put_json_string (out_t *o, const char *s)
{
	static const char hex[] = "0123456789abcdef";
	put (o, "\"", 1);
	for (; s != 0 && *s != '\0'; s++)
	{
		unsigned char c = (unsigned char) *s;
		if (c == '"') put (o, "\\\"", 2);
		else if (c == '\\') put (o, "\\\\", 2);
		else if (c == '\n') put (o, "\\n", 2);
		else if (c == '\r') put (o, "\\r", 2);
		else if (c == '\t') put (o, "\\t", 2);
		else if (c < 0x20)
		{
			char u[6] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15] };
			put (o, u, 6);
		}
		else put (o, (const char *) &c, 1);
	}
	put (o, "\"", 1);
}

static void put_uint (out_t *o, unsigned v)
{
	char t[12];
	int n = 0;
	do { t[n++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	while (n > 0) put (o, &t[--n], 1);
}

static void put_message (out_t *o, const char *role, const char *content)
{
	puts_ (o, "{\"role\":\"");
	puts_ (o, role);
	puts_ (o, "\",\"content\":");
	put_json_string (o, content);
	puts_ (o, "}");
}

/* ---- les textes du prompt, modifiables sans recompiler (25/10/2026, demande du client) -----------------------------
 * Chaque texte fixe du prompt a un nom ; l'hôte peut le lire dans un fichier (SD:/etc/prompt/<nom>.txt :
 * groq_set_lire_texte), sinon c'est celui du code. Les parties calculées (résolution, liste mise en page pour l'écran,
 * mémoire, date, apps) restent dans le code. Un fichier : des lignes « # ... » en tête (commentaires), dont « # version
 * XXXXXXXX », l'empreinte du texte du code d'où il vient ; si le code a changé depuis (une autre session a retouché ce
 * texte), le fichier est ignoré (groq_textes_ignores), pour ne pas cacher la nouvelle règle ; sans ligne version, il
 * est toujours pris. Ensuite le texte, coupé en lignes comme on veut : une fin de ligne vaut un espace. */
static const struct { const char *nom, *texte, *role; } s_textes[] =
{
	{ "debut", s_prompt_debut, "le debut : qui est Pocket" },
	{ "ton0", s_ton0, "le ton, caractere 0 (serieuse)" },
	{ "ton1", s_ton1, "le ton, caractere 1 (calme)" },
	{ "ton2", s_ton2, "le ton, caractere 2 (chaleureuse)" },
	{ "ton3", s_ton3, "le ton, caractere 3 (enjouee)" },
	{ "ton4", s_ton4, "le ton, caractere 4 (malicieuse)" },
	{ "exactitude", s_exactitude, "apres le ton, si le caractere n'est pas 0" },
	{ "regles", s_prompt_fin, "les regles et les apps de base (say, humeur, retenir, ask, next...)" },
	{ "regles_compact", s_prompt_fin_compact, "les memes, prompt compact" },
	{ "connues", s_connues, "les informations connues (marqueurs) ; suivi de la liste des marqueurs" },
	{ "connues_compact", s_connues_compact, "les memes, prompt compact" },
	{ "dates", s_dates, "apres \"Nous sommes le <date>.\"" },
	{ "dates_compact", s_dates_compact, "les memes, prompt compact" },
	{ "dessin", s_dessin, "Pocket dessine : l'outcome draw (avec un ecran) ; suivi de la presentation des listes, calculee" },
	{ "histoire_boutons", s_histoire_boutons, "Pocket dessine : histoires illustrees, boutons et memo" },
	{ "choix", s_choix, "Pocket dessine : les choice ; suivi de l'exemple Pile ou face, calcule" },
	{ "dessin_compact", s_dessin_compact, "Pocket dessine, prompt compact ; suivi de la mise en page des listes, calculee" },
	{ "boutons_compact", s_boutons_compact, "Pocket dessine, boutons et memo, prompt compact" },
	{ "apps", s_apps, "avant la liste des applications" },
	{ "apps_compact", s_apps_compact, "les memes, prompt compact" },
	{ "saisie", s_saisie, "{saisie} : la saisie au clavier (avec un ecran)" },
	{ "saisie_compact", s_saisie_compact, "les memes, prompt compact" },
	{ "tour_next", s_tour_next, "second tour d'une histoire (next) : le morceau suivant" },
	{ "tour_next_dessin", s_tour_next_dessin, "le meme, avec un ecran : l'illustrer" },
	{ "tour_draw_choix", s_tour_draw_choix, "second tour d'un draw a boutons, reponse dite : apres la liste des boutons" },
	{ "tour_draw_juger", s_tour_draw_juger, "second tour d'un draw a boutons : comment juger un choix" },
	{ "tour_draw_suite", s_tour_draw_suite, "second tour d'un draw a boutons : continuer l'app" },
	{ "tour_ask", s_tour_ask, "second tour d'un ask : continuer avec la reponse" },
	{ "tour_resultat", s_tour_resultat, "second tour d'un wiki ou askinfo : repondre avec le resultat" },
	{ "tour_resultat_dessin", s_tour_resultat_dessin, "le meme, avec un ecran : quand dessiner" },
};
#define NTEXTES (sizeof s_textes / sizeof s_textes[0])

static groq_lire_texte *s_lire_texte;
static char s_ignores[400];		/* les fichiers périmés, depuis le dernier groq_build_request */

void groq_set_lire_texte (groq_lire_texte *lire) { s_lire_texte = lire; }
const char *groq_textes_ignores (void) { return s_ignores; }

/* l'empreinte d'un texte du code (FNV-1a, sans les espaces du bord) */
static unsigned long empreinte (const char *t)
{
	size_t n = strlen (t);
	while (*t == ' ') { t++; n--; }
	while (n > 0 && t[n - 1] == ' ') n--;
	unsigned long h = 2166136261UL;
	for (size_t i = 0; i < n; i++) h = ((h ^ (unsigned char) t[i]) * 16777619UL) & 0xFFFFFFFFUL;
	return h;
}

static unsigned long hexa_ (const char *p)
{
	unsigned long v = 0;
	for (; (*p >= '0' && *p <= '9') || ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'f'); p++)
		v = v * 16 + (unsigned long) (*p <= '9' ? *p - '0' : (*p | 0x20) - 'a' + 10);
	return v;
}

int groq_texte (unsigned i, const char **nom, const char **defaut, const char **role)
{
	if (i >= NTEXTES) return 0;
	if (nom) *nom = s_textes[i].nom;
	if (defaut) *defaut = s_textes[i].texte;
	if (role) *role = s_textes[i].role;
	return 1;
}

size_t groq_texte_fichier (unsigned i, char *out, size_t cap)
{
	if (i >= NTEXTES || cap == 0) return 0;
	const char *t = s_textes[i].texte;
	char v[9];
	unsigned long h = empreinte (t);
	for (int k = 7; k >= 0; k--, h >>= 4) v[k] = "0123456789abcdef"[h & 15];
	v[8] = 0;
	out_t e = { out, cap, 0, 0 };
	puts_ (&e, "# prompt de Pocket : ");
	puts_ (&e, s_textes[i].nom);
	puts_ (&e, " (");
	puts_ (&e, s_textes[i].role);
	puts_ (&e, ")\n# modifiable sans recompiler ; supprimer ce fichier rend le texte du code\n# version ");
	puts_ (&e, v);
	puts_ (&e, "\n");
	if (e.full) return 0;
	size_t n = e.len;
	/* le texte, coupé en lignes d'environ 110 caractères à un espace (une fin de ligne vaut un espace à la lecture) */
	while (*t == ' ') t++;
	size_t len = strlen (t);
	while (len > 0 && t[len - 1] == ' ') len--;
	size_t col = 0;
	for (size_t k = 0; k < len; k++)
	{
		if (n + 3 >= cap) return 0;
		if (t[k] == ' ' && col >= 110) { out[n++] = '\n'; col = 0; continue; }
		out[n++] = t[k];
		col++;
	}
	out[n++] = '\n';
	out[n] = 0;
	return n;
}

/* le texte nom : son fichier s'il y en a un, à jour, sinon celui du code ; écrit sans les espaces du bord */
static void put_texte (out_t *o, const char *nom)
{
	unsigned i = 0;
	while (i < NTEXTES && strcmp (s_textes[i].nom, nom) != 0) i++;
	if (i == NTEXTES) return;
	static char f[24 * 1024];
	size_t n = s_lire_texte != 0 ? s_lire_texte (nom, f, sizeof f - 1) : 0;
	if (n > 0)
	{
		f[n] = 0;
		char *p = f;
		if ((unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB && (unsigned char) p[2] == 0xBF) p += 3;	/* (BOM du Bloc-notes) */
		int perime = 0;
		/* l'en-tête : « # ... » ; « # version XXXXXXXX » doit être l'empreinte du texte du code */
		while (*p == '#')
		{
			if (strncmp (p, "# version ", 10) == 0 && hexa_ (p + 10) != empreinte (s_textes[i].texte)) perime = 1;
			while (*p && *p != '\n') p++;
			if (*p) p++;
		}
		if (perime)
		{
			size_t l = strlen (s_ignores);
			if (l + strlen (nom) + 3 < sizeof s_ignores) { if (l) strcat (s_ignores, ", "); strcat (s_ignores, nom); }
		}
		else
		{
			/* les fins de ligne en espaces, sans les espaces du bord */
			char *w = f;
			for (char *q = p; *q; q++) if (*q != '\r') *w++ = *q == '\n' || *q == '\t' ? ' ' : *q;
			*w = 0;
			char *d = f;
			while (*d == ' ') d++;
			while (w > d && w[-1] == ' ') w--;
			if (w > d) { put (o, d, (size_t) (w - d)); return; }
		}
	}
	const char *t = s_textes[i].texte;
	while (*t == ' ') t++;
	size_t l = strlen (t);
	while (l > 0 && t[l - 1] == ' ') l--;
	put (o, t, l);
}

/* l'outcome maketext [nom, texte] (25/10/2026) : le LLM rédige un texte (un mail « sois original »), l'hôte l'écrit
 * TOUJOURS dans SD:/tmp/ (jamais ailleurs : un nom ne peut pas remonter dans etc/ ou apps/) ; ici seulement le nom sûr
 * et la mise en forme de brouillon de mail (galo.bi : MailDraft) : « Subject: », « Lire: oui » (sendmail le relit
 * avant de demander le oui), une ligne vide, le texte */
int groq_maketext (const char *nom, const char *texte, char *fichier, size_t fcap, char *out, size_t ocap)
{
	/* le nom : a-z 0-9 _ -, minuscules, accents simples retirés, sans chemin ni extension, 32 au plus ; "texte" si vide */
	size_t n = 0;
	const char *s = nom != 0 ? nom : "";
	for (const char *p = s; *p; p++) if (*p == '/' || *p == '\\' || *p == ':') s = p + 1;
	for (const unsigned char *p = (const unsigned char *) s; *p && n < 32 && n + 5 < fcap; p++)
	{
		unsigned char c = *p;
		if (c == '.') break;
		if (c == 0xC3 && p[1]) { unsigned char d = p[1] | 0x20; p++;
			c = d >= 0xA0 && d <= 0xA6 ? 'a' : d >= 0xA8 && d <= 0xAB ? 'e' : d >= 0xAC && d <= 0xAF ? 'i'
			  : d >= 0xB2 && d <= 0xB6 ? 'o' : d >= 0xB9 && d <= 0xBC ? 'u' : d == 0xA7 ? 'c' : '_'; }
		else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) c = '_';
		fichier[n++] = (char) c;
	}
	if (n == 0) { const char *d = "texte"; while (*d && n + 5 < fcap) fichier[n++] = *d++; }
	if (n + 5 > fcap) return 0;
	memcpy (fichier + n, ".txt", 5);

	/* le brouillon : le sujet donné (« Subject: ... » en première ligne) ou un sujet par défaut */
	const char *t = texte != 0 ? texte : "";
	while (*t == ' ' || *t == '\n' || *t == '\r') t++;
	size_t o = 0;
	#define PUT(str, len) do { size_t l_ = (len); if (o + l_ + 1 > ocap) return 0; memcpy (out + o, (str), l_); o += l_; } while (0)
	if ((t[0] | 0x20) == 's' && strncmp (t + 1, "ubject:", 7) == 0)
	{
		const char *e = t; while (*e && *e != '\n') e++;
		const char *f = e; if (f > t && f[-1] == '\r') f--;
		PUT ("Subject:", 8);
		const char *v = t + 8; while (v < f && *v == ' ') v++;
		PUT (" ", 1);
		if (f > v) PUT (v, (size_t) (f - v)); else PUT ("Message de Pocket", 17);
		t = *e ? e + 1 : e;
		while (*t == ' ' || *t == '\n' || *t == '\r') t++;
	}
	else PUT ("Subject: Message de Pocket", 26);
	PUT ("\nLire: oui\n\n", 12);
	for (; *t; t++) if (*t != '\r') PUT (t, 1);
	while (o > 0 && (out[o - 1] == '\n' || out[o - 1] == ' ')) o--;
	/* une signature {nom} alors que le prénom n'est pas connu : l'hôte l'a déjà remplacée par « toi » (règle des say) ;
	 * dans une lettre, une dernière ligne « toi » n'a pas de sens : retirée */
	{
		size_t l = o;
		while (l > 0 && out[l - 1] != '\n') l--;
		if (o - l == 3 && (out[l] | 0x20) == 't' && out[l + 1] == 'o' && out[l + 2] == 'i')
		{
			o = l;
			while (o > 0 && (out[o - 1] == '\n' || out[o - 1] == ' ')) o--;
		}
	}
	PUT ("\n", 1);
	#undef PUT
	out[o] = '\0';
	return 1;
}

const char *groq_default_system (void)
{
	/* (resserré le 23/10/2026 : mêmes règles, environ moitié moins long ; le prompt part à chaque question, deux ou
	 * trois fois avec un second tour) ; la phrase du ton selon le caractère ; chaque morceau peut venir d'un fichier */
	static const char *const tons[GROQ_CARACTERE_MAX + 1] = { "ton0", "ton1", "ton2", "ton3", "ton4" };
	static char prompt[32 * 1024];
	out_t s = { prompt, sizeof prompt, 0, 0 };
	put_texte (&s, "debut");
	puts_ (&s, " ");
	put_texte (&s, tons[s_caractere]);
	if (s_caractere > 0) { puts_ (&s, " "); put_texte (&s, "exactitude"); }
	puts_ (&s, " ");
	put_texte (&s, s_compact ? "regles_compact" : "regles");
	return prompt;
}

/* ---- marqueurs -------------------------------------------------------------- */

/* une valeur de la mémoire qui est une date (« 19 septembre », « 19/09 ») : le prompt le dit, « {anniversaire}
 * (une date) », pour que le modèle écrive ce marqueur en argument date au lieu d'inventer une date */
static int looks_like_date (const char *v)
{
	static const char *const Mois[] = { "janvier", "vrier", "mars", "avril", "mai", "juin", "juillet", "aout",
		"août", "septembre", "octobre", "novembre", "cembre", 0 };
	char l[64];
	size_t n = 0;
	int digit = 0, slash = 0;
	for (; v[n] != 0 && n + 1 < sizeof l; n++)
	{
		char c = v[n];
		if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if (c >= '0' && c <= '9') digit = 1;
		if (c == '/' && n > 0 && v[n - 1] >= '0' && v[n - 1] <= '9') slash = 1;
		l[n] = c;
	}
	l[n] = 0;
	if (!digit) return 0;
	if (slash) return 1;
	for (unsigned i = 0; Mois[i] != 0; i++)
		if (strstr (l, Mois[i]) != 0) return 1;
	return 0;
}

/* un octet de mot : lettre ou chiffre ASCII, ou octet d'un caractère UTF-8 */
static int is_word (unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80;
}

/* minuscule d'un octet, en tenant compte du premier octet UTF-8 précédent (À..Þ -> à..þ) */
static unsigned char lower (unsigned char c, unsigned char prev)
{
	if (c >= 'A' && c <= 'Z') return (unsigned char) (c + 32);
	if (prev == 0xC3 && c >= 0x80 && c <= 0x9E && c != 0x97) return (unsigned char) (c + 32);
	return c;
}

/* la valeur v (n octets) est-elle à s, casse ignorée ? */
static int match_at (const unsigned char *s, const unsigned char *v, size_t n)
{
	unsigned char ps = 0, pv = 0;
	for (size_t i = 0; i < n; i++)
	{
		if (s[i] == 0 || lower (s[i], ps) != lower (v[i], pv)) return 0;
		ps = s[i];
		pv = v[i];
	}
	return 1;
}

size_t groq_mask (const char *in, const groq_predicate *p, unsigned n, char *out, size_t cap)
{
	out_t o = { out, cap, 0, 0 };
	if (cap == 0) return 0;
	out[0] = '\0';
	const unsigned char *s = (const unsigned char *) (in != 0 ? in : "");
	while (*s != 0)
	{
		/* au début d'un mot : la plus longue valeur connue qui y commence et y finit un mot */
		int best = -1;
		size_t bl = 0;
		if (s == (const unsigned char *) in || !is_word (s[-1]))
		{
			for (unsigned i = 0; i < n; i++)
			{
				const char *v = p[i].value;
				size_t l = v != 0 ? strlen (v) : 0;
				if (l == 0 || l <= bl || p[i].name == 0) continue;
				if (match_at (s, (const unsigned char *) v, l) && !is_word (s[l]))
				{
					best = (int) i;
					bl = l;
				}
			}
		}
		if (best >= 0)
		{
			puts_ (&o, "{");
			puts_ (&o, p[best].name);
			puts_ (&o, "}");
			s += bl;
		}
		else
		{
			put (&o, (const char *) s, 1);
			s++;
		}
	}
	return o.full ? 0 : o.len;
}

/* longueur de l'emoji UTF-8 en s, 0 si ce n'en est pas un */
static size_t emoji_len (const unsigned char *s)
{
	if (s[0] >= 0xF0 && s[0] <= 0xF4 && s[1] != 0 && s[2] != 0 && s[3] != 0) return 4;
	if (s[0] == 0xE2 && s[1] != 0 && s[2] != 0
	    && ((s[1] >= 0x98 && s[1] <= 0x9E) || (s[1] >= 0xAC && s[1] <= 0xAF)
		|| (s[1] == 0x80 && s[2] == 0x8D))) return 3;
	if (s[0] == 0xEF && s[1] == 0xB8 && s[2] == 0x8F) return 3;
	return 0;
}

size_t groq_unmask (const char *in, const groq_predicate *p, unsigned n, char *out, size_t cap)
{
	out_t o = { out, cap, 0, 0 };
	if (cap == 0) return 0;
	out[0] = '\0';
	for (const char *s = in != 0 ? in : ""; *s != '\0'; )
	{
		/* emoji et pictogrammes retirés (la réponse est dite par PicoTTS) : U+10000 et au-delà,
		 * U+2600..27BF, U+2B00..2BFF, sélecteur de variante U+FE0F, liant U+200D */
		size_t skip = emoji_len ((const unsigned char *) s);
		if (skip > 0)
		{
			s += skip;
			continue;
		}
		if (*s == '*')			/* (mise en forme Markdown : *mot*, **mot**) */
		{
			s++;
			continue;
		}
		const char *e = *s == '{' ? strchr (s, '}') : 0;
		size_t l = e != 0 ? (size_t) (e - s - 1) : 0;
		if (e == 0 || l == 0 || l > 20)
		{
			put (&o, s++, 1);
			continue;
		}
		const char *v = 0;
		for (unsigned i = 0; i < n && v == 0; i++)
		{
			if (p[i].name != 0 && strlen (p[i].name) == l && strncmp (p[i].name, s + 1, l) == 0
			    && p[i].value != 0 && p[i].value[0] != '\0')
			{
				v = p[i].value;
			}
		}
		if (v == 0 && ((l == 3 && strncmp (s + 1, "nom", 3) == 0) || (l == 6 && strncmp (s + 1, "prenom", 6) == 0)))
		{
			v = "toi";
		}
		if (v != 0) puts_ (&o, v);		/* sinon le marqueur est retiré */
		s = e + 1;
	}
	if (o.full) return 0;

	/* nettoyage : espaces doublés, espace avant , . ; espaces en tête et en fin */
	size_t w = 0;
	for (size_t r = 0; r < o.len; r++)
	{
		char c = out[r];
		if (c == ' ' && (w == 0 || out[w - 1] == ' ')) continue;
		if ((c == ',' || c == '.') && w > 0 && out[w - 1] == ' ') w--;
		out[w++] = c;
	}
	while (w > 0 && out[w - 1] == ' ') w--;
	out[w] = '\0';
	return w;
}

/* ---- requête ---------------------------------------------------------------- */

/* draw : la présentation d'informations (demande du client, 25/10 : la météo de la semaine sortait de l'écran, en
 * trop gros, sur plusieurs lignes par jour) ; la règle et un exemple calculés pour la résolution de l'écran */
/* draw à boutons : des choix à toucher s'écrivent « choice », sans coordonnées ; pocket_dessin.c les place (25/10, idée du
 * client : le LLM empilait mal ses boutons, le 3e tombait sous le bas de l'écran) */
static void prompt_boutons (out_t *s)
{
	puts_ (s, " ");
	put_texte (s, "choix");
}

static void prompt_liste (out_t *s)
{
	int L = s_ecran_l, H = s_ecran_h, ligne = (H - 46) / 7, police = ligne >= 30 ? 16 : 12;
	int icone = ligne - 8 > 28 ? 28 : ligne - 8, ty = (ligne - police) / 2;
	puts_ (s, "Des informations a montrer (la meteo de plusieurs jours, une liste, un agenda, des scores...) : soigne "
		  "la presentation pour CET ecran, tout doit tenir et se lire. Un titre court en haut (large, centre) ; puis "
		  "UNE ligne par element, toutes ses infos sur cette ligne, jamais deux ; hauteur d'une ligne = (");
	put_uint (s, (unsigned) H);
	puts_ (s, " - 46) / le nombre d'elements ; police : medium si la ligne fait 30 pixels ou plus, small sinon (large "
		  "et title : le titre seulement) ; textes courts (Lun 26, 8-12°, 60 %) : un caractere fait environ 0,6 fois "
		  "la police, une ligne de ");
	put_uint (s, (unsigned) L);
	puts_ (s, " pixels tient environ ");
	put_uint (s, (unsigned) (L / 10));
	puts_ (s, " caracteres en medium ; en colonnes qui ne se touchent pas : le libelle en l a x = 8, l'icone a x = ");
	put_uint (s, (unsigned) (L * 2 / 5));
	puts_ (s, " (taille : la hauteur de ligne - 8, 28 au plus), les nombres en r a x = ");
	put_uint (s, (unsigned) (L - 8));
	puts_ (s, ", ");
	put_uint (s, (unsigned) ((L * 3 / 5 - 16) / 10));
	puts_ (s, " caracteres au plus en medium (seulement l'essentiel : une info de plus qui ne tient pas, on la dit "
		  "dans le say)");
	puts_ (s, ". Exemple, 7 jours : \"fill #0e1e3a\\ntext ");
	put_uint (s, (unsigned) (L / 2));
	puts_ (s, " 6 large #ffffff c Meteo a Paris\\nline 8 40 ");
	put_uint (s, (unsigned) (L - 8));
	puts_ (s, " 40 #8a96a0\\n");
	for (int i = 0; i < 2; i++)
	{
		static const char *const jour[] = { "Lun 26", "Mar 27" }, *const ic[] = { "rain", "sun" };
		static const char *const val[] = { "8-12\xC2\xB0 100 %", "5-14\xC2\xB0 10 %" };
		int y = 46 + i * ligne;
		puts_ (s, "text 8 "); put_uint (s, (unsigned) (y + ty)); puts_ (s, police == 16 ? " medium" : " small");
		puts_ (s, " #ffffff l "); puts_ (s, jour[i]); puts_ (s, "\\nicon "); puts_ (s, ic[i]); puts_ (s, " ");
		put_uint (s, (unsigned) (L * 2 / 5)); puts_ (s, " "); put_uint (s, (unsigned) (y + ligne / 2)); puts_ (s, " ");
		put_uint (s, (unsigned) icone); puts_ (s, "\\ntext "); put_uint (s, (unsigned) (L - 8)); puts_ (s, " ");
		put_uint (s, (unsigned) (y + ty)); puts_ (s, police == 16 ? " medium" : " small"); puts_ (s, " #ffffff r ");
		puts_ (s, val[i]); puts_ (s, "\\n");
	}
	puts_ (s, "...\" (chaque jour ");
	put_uint (s, (unsigned) ligne);
	puts_ (s, " pixels plus bas). Exemple de fiche (\"parle-moi du guepard\") : \"fill #0e1e3a\\ntext ");
	put_uint (s, (unsigned) (L / 2));
	puts_ (s, " 6 large #ffffff c Guepard\\nline 8 40 ");
	put_uint (s, (unsigned) (L - 8));
	puts_ (s, " 40 #8a96a0\\ntext 8 56 medium #ffd23f l Vitesse : 110 km/h\\ntext 8 96 medium #ffffff l Poids : 50 kg"
		  "\\ntext 8 136 medium #ffffff l Habitat : savane\\n...\".");
}

/* la partie écran du prompt compact : les mêmes règles que la complète, en style télégraphique */
static void prompt_ecran_compact (out_t *s)
{
	int L = s_ecran_l, H = s_ecran_h, ligne = (H - 46) / 7, police = ligne >= 30 ? 16 : 12;
	int icone = ligne - 8 > 28 ? 28 : ligne - 8, ty = (ligne - police) / 2;
	const char *pol = police == 16 ? " medium" : " small";
	puts_ (s, " Ecran : ");
	put_uint (s, (unsigned) L); puts_ (s, "x"); put_uint (s, (unsigned) H);
	puts_ (s, L > H ? " pixels, paysage" : " pixels, portrait");
	puts_ (s, s_ecran_tactile ? ", tactile. " : ". ");
	put_texte (s, "dessin_compact");
	puts_ (s, " hauteur = (");
	put_uint (s, (unsigned) H);
	puts_ (s, " - 46) / nombre d'elements ; medium si 30 px ou plus, sinon small ; textes courts (~");
	put_uint (s, (unsigned) (L / 10));
	puts_ (s, " caracteres par ligne en medium) ; colonnes : libelle l x=8, icone x=");
	put_uint (s, (unsigned) (L * 2 / 5));
	puts_ (s, " (taille ligne - 8, max 28), nombres r x=");
	put_uint (s, (unsigned) (L - 8));
	puts_ (s, " (");
	put_uint (s, (unsigned) ((L * 3 / 5 - 16) / 10));
	puts_ (s, " caracteres max) ; le reste dans le say. Ex. : \"fill #0e1e3a\\ntext ");
	put_uint (s, (unsigned) (L / 2));
	puts_ (s, " 6 large #ffffff c Meteo a Paris\\nline 8 40 ");
	put_uint (s, (unsigned) (L - 8));
	puts_ (s, " 40 #8a96a0\\ntext 8 ");
	put_uint (s, (unsigned) (46 + ty)); puts_ (s, pol); puts_ (s, " #ffffff l Lun 26\\nicon rain ");
	put_uint (s, (unsigned) (L * 2 / 5)); puts_ (s, " "); put_uint (s, (unsigned) (46 + ligne / 2)); puts_ (s, " ");
	put_uint (s, (unsigned) icone); puts_ (s, "\\ntext "); put_uint (s, (unsigned) (L - 8)); puts_ (s, " ");
	put_uint (s, (unsigned) (46 + ty)); puts_ (s, pol);
	puts_ (s, " #ffffff r 8-12\xC2\xB0 100 %\\n...\" (ligne suivante ");
	put_uint (s, (unsigned) ligne);
	puts_ (s, " px plus bas ; une fiche : \"Vitesse : 110 km/h\" en l a x=8). ");
	put_texte (s, "boutons_compact");
	prompt_boutons (s);
	puts_ (s, " Ex. : [{\"app\": \"say\", \"args\": [\"Pile ou "
		  "face ?\"]}, {\"app\": \"draw\", \"args\": [\"fill #0e1e3a\\ntext ");
	put_uint (s, (unsigned) (L / 2));
	puts_ (s, " 20 large #ffffff c Pile ou face ?\\nchoice pile Pile\\nchoice face Face\", \"pile ou face, 1er "
		  "lancer\"]}].");
}

size_t groq_build_request (const groq_request *req, char *buf, size_t cap, char *sent, size_t sent_cap)
{
	out_t o = { buf, cap, 0, 0 };
	static char m[2048];			/* (pas sur la pile : petites piles de tâche) */
	if (cap == 0) return 0;
	buf[0] = '\0';

	puts_ (&o, "{\"model\":");
	put_json_string (&o, req->model != 0 ? req->model : GROQ_DEFAULT_MODEL);
	puts_ (&o, ",\"messages\":[");
	{
		/* le prompt + les informations connues (les marqueurs seulement, jamais les valeurs) + la date du
		 * jour en clair (consigne de calcul des dates) + les applications */
		static char sys[48 * 1024];	/* (le prompt + les manifestes des apps : 16 Ko le 23/10, 10 apps) */
		out_t s = { sys, sizeof sys, 0, 0 };
		s_ignores[0] = 0;
		puts_ (&s, req->system != 0 ? req->system : groq_default_system ());
		puts_ (&s, " ");
		put_texte (&s, s_compact ? "connues_compact" : "connues");
		unsigned nk = 0;
		for (unsigned i = 0; i < req->npredicates; i++)
		{
			const groq_predicate *p = &req->predicates[i];
			if (p->name == 0 || p->value == 0 || p->value[0] == '\0') continue;
			puts_ (&s, " {");
			puts_ (&s, p->name);
			puts_ (&s, "}");
			if (looks_like_date (p->value)) puts_ (&s, " (une date)");	/* (le type, pas la valeur) */
			nk++;
		}
		puts_ (&s, nk > 0 ? "." : " aucune pour l'instant (tu ne connais donc rien de la personne).");
		if (req->today != 0)
		{
			puts_ (&s, " Nous sommes le ");
			puts_ (&s, req->today);
			puts_ (&s, ". ");
			put_texte (&s, s_compact ? "dates_compact" : "dates");
		}
		if (s_ecran_l > 0 && s_compact) prompt_ecran_compact (&s);
		else if (s_ecran_l > 0)
		{
			char t[64] = "";
			out_t e = { t, sizeof t, 0, 0 };
			put_uint (&e, (unsigned) s_ecran_l);
			puts_ (&e, "x");
			put_uint (&e, (unsigned) s_ecran_h);
			puts_ (&s, " Ecran : Pocket a un ecran de ");
			puts_ (&s, t);
			puts_ (&s, s_ecran_l > s_ecran_h ? " pixels, paysage" : " pixels, portrait");
			puts_ (&s, s_ecran_tactile ? ", tactile. " : ". ");
			put_texte (&s, "dessin");
			puts_ (&s, " ");
			prompt_liste (&s);
			puts_ (&s, " ");
			put_texte (&s, "histoire_boutons");
			prompt_boutons (&s);
			puts_ (&s, " Exemple : [{\"app\": \"say\", \"args\": "
				   "[\"Pile ou face ?\"]}, {\"app\": \"draw\", \"args\": [\"fill #0e1e3a\\ntext ");
			char m[16] = "";
			out_t em = { m, sizeof m, 0, 0 };
			put_uint (&em, (unsigned) (s_ecran_l / 2));
			puts_ (&s, m);
			puts_ (&s, " 20 large #ffffff c Pile ou face ?\\nchoice pile Pile\\nchoice face Face\", \"pile ou face, "
				   "premier lancer\"]}].");
		}
		if (req->apps != 0 && req->apps[0] != '\0' && s_compact)
		{
			puts_ (&s, " ");
			put_texte (&s, "apps_compact");
			puts_ (&s, " ");
			puts_ (&s, req->apps);
		}
		else if (req->apps != 0 && req->apps[0] != '\0')
		{
			puts_ (&s, " ");
			put_texte (&s, "apps");
			puts_ (&s, " ");
			puts_ (&s, req->apps);
		}
		if (req->apps != 0 && req->apps[0] != '\0' && s_ecran_l > 0)
		{
			puts_ (&s, " ");
			put_texte (&s, s_compact ? "saisie_compact" : "saisie");
		}
		if (s.full) return 0;
		put_message (&o, "system", sys);
	}

	unsigned first = req->nhistory > GROQ_MAX_HISTORY ? req->nhistory - GROQ_MAX_HISTORY : 0;
	for (unsigned i = first; i < req->nhistory; i++)
	{
		const groq_exchange *e = &req->history[i];
		if (e->user == 0 || e->galo == 0) continue;
		/* (déjà sous forme de marqueurs ; masqué encore une fois, par sûreté) */
		if (groq_mask (e->user, req->predicates, req->npredicates, m, sizeof m) == 0 && e->user[0] != '\0')
			return 0;
		puts_ (&o, ",");
		put_message (&o, "user", m);
		/* l'historique dans la forme attendue en sortie : le modèle la garde */
		static char a[4400], ma[2200];
		out_t t = { a, sizeof a, 0, 0 };
		ma[0] = 0;
		if (e->apps != 0 && e->apps[0] != 0 && groq_mask (e->apps, req->predicates, req->npredicates, ma, sizeof ma) == 0)
			ma[0] = 0;
		if (groq_mask (e->galo, req->predicates, req->npredicates, m, sizeof m) == 0 && e->galo[0] != '\0')
			return 0;
		/* (avec sa langue : le modèle imite la forme de ses réponses passées) */
		if (e->langue != 0 && e->langue[0] != 0)
		{
			puts_ (&t, "{\"langue\": ");
			put_json_string (&t, e->langue);
			puts_ (&t, ", \"outcomes\": [");
		}
		else puts_ (&t, "{\"outcomes\": [");
		puts_ (&t, ma);
		puts_ (&t, "{\"app\": \"say\", \"args\": [");
		put_json_string (&t, m);
		puts_ (&t, "]}]}");
		if (t.full) return 0;
		puts_ (&o, ",");
		put_message (&o, "assistant", a);
	}

	if (groq_mask (req->user, req->predicates, req->npredicates, m, sizeof m) == 0 && req->user[0] != '\0')
		return 0;
	if (sent != 0 && sent_cap > 0)
	{
		size_t l = strlen (m) < sent_cap - 1 ? strlen (m) : sent_cap - 1;
		memcpy (sent, m, l);
		sent[l] = '\0';
	}
	puts_ (&o, ",");
	put_message (&o, "user", m);

	/* second tour : la commande demandée par le modèle, puis son résultat (masqué) */
	if (req->tool_name != 0)
	{
		static char a[3200], t[4608];
		out_t ta = { a, sizeof a, 0, 0 };
		/* (l'argument aussi est masqué : l'hôte l'a démasqué pour lancer l'app, « meteo now Saint-Étienne ») */
		static char ma[2600];
		if (groq_mask (req->tool_arg != 0 ? req->tool_arg : "", req->predicates, req->npredicates, ma, sizeof ma) == 0
		    && req->tool_arg != 0 && req->tool_arg[0] != '\0')
			return 0;
		/* draw : l'argument est « scène \x1E mémo » ; le modèle revoit son draw tel quel (deux arguments) : sans la
		 * scène, il recopiait le résumé à la place d'une scène au tour suivant */
		char *memo = strcmp (req->tool_name, "draw") == 0 ? strchr (ma, '\x1E') : 0;
		if (memo != 0) *memo++ = '\0';
		puts_ (&ta, "{\"outcomes\": [{\"app\": ");
		put_json_string (&ta, req->tool_name);
		puts_ (&ta, ", \"args\": [");
		put_json_string (&ta, ma);
		if (memo != 0) { puts_ (&ta, ", "); put_json_string (&ta, memo); }
		puts_ (&ta, "]}]}");
		if (ta.full) return 0;
		puts_ (&o, ",");
		put_message (&o, "assistant", a);

		static char r[2560];
		if (groq_mask (req->tool_result != 0 ? req->tool_result : "", req->predicates, req->npredicates,
			       r, sizeof r) == 0 && req->tool_result != 0 && req->tool_result[0] != '\0')
			return 0;
		out_t tt = { t, sizeof t, 0, 0 };
		if (strcmp (req->tool_name, "next") == 0)
		{
			/* next : le morceau suivant d'un long contenu ; l'argument est « mode : résumé », le résultat le
			 * dernier passage dit */
			puts_ (&tt, "Suite demandee. Ou on en est (");
			puts_ (&tt, ma);
			puts_ (&tt, "). Dernier passage dit : \"");
			puts_ (&tt, r);
			puts_ (&tt, "\"\n");
			put_texte (&tt, "tour_next");
			if (s_ecran_l > 0) { puts_ (&tt, " "); put_texte (&tt, "tour_next_dessin"); }
		}
		else if (strcmp (req->tool_name, "draw") == 0)
		{
			/* draw à boutons : l'argument est « scène \x1E mémo » (la scène, rendue plus haut comme le draw du modèle),
			 * le résultat « bouton <id> <texte> » (touché) ou « voix <ce qui a été dit> » */
			if (strncmp (r, "bouton ", 7) == 0)
			{
				const char *id = r + 7, *sp = strchr (id, ' ');
				puts_ (&tt, "La personne a touche le bouton \"");
				puts_ (&tt, sp != 0 ? sp + 1 : id);
				puts_ (&tt, "\" (");
				if (sp != 0) put (&tt, id, (size_t) (sp - id)); else puts_ (&tt, id);
				puts_ (&tt, ").");
			}
			else
			{
				/* « voix <dit> \x1F <boutons affichés> » : l'hôte n'y a pas reconnu un bouton à coup sûr */
				char *bt = strchr (r, '\x1F');
				if (bt != 0) *bt++ = '\0';
				puts_ (&tt, "La personne a repondu de vive voix : \"");
				puts_ (&tt, strncmp (r, "voix ", 5) == 0 ? r + 5 : r);
				puts_ (&tt, "\".");
				if (bt != 0 && *bt)
				{
					puts_ (&tt, " Les boutons affiches : ");
					puts_ (&tt, bt);
					puts_ (&tt, ". ");
					put_texte (&tt, "tour_draw_choix");
				}
			}
			puts_ (&tt, " ");
			put_texte (&tt, "tour_draw_juger");
			if (memo != 0 && *memo) { puts_ (&tt, " Ton memo : "); puts_ (&tt, memo); puts_ (&tt, "."); }
			puts_ (&tt, "\n");
			put_texte (&tt, "tour_draw_suite");
		}
		else if (strcmp (req->tool_name, "ask") == 0)
		{
			/* ask : la réponse de la personne à la question posée ; le modèle reprend son plan avec */
			if (r[0] != '\0')
			{
				puts_ (&tt, "Reponse de la personne a ta question \"");
				puts_ (&tt, ma);
				puts_ (&tt, "\" : \"");
				puts_ (&tt, r);
				puts_ (&tt, "\"");
			}
			else
			{
				puts_ (&tt, "Pas de reponse de la personne a ta question \"");
				puts_ (&tt, ma);
				puts_ (&tt, "\"");
			}
			puts_ (&tt, "\n");
			put_texte (&tt, "tour_ask");
		}
		else
		{
			puts_ (&tt, "Resultat de la commande ");
			puts_ (&tt, req->tool_name);
			puts_ (&tt, " : ");
			puts_ (&tt, r[0] != '\0' ? r : "(rien trouve)");
			puts_ (&tt, "\n");
			put_texte (&tt, "tour_resultat");
			if (s_ecran_l > 0) { puts_ (&tt, " "); put_texte (&tt, "tour_resultat_dessin"); }
		}
		if (tt.full) return 0;
		puts_ (&o, ",");
		put_message (&o, "user", t);
	}
	/* les gpt-oss raisonnent avant de répondre : avec 200 jetons le JSON n'arrive jamais
	 * (HTTP 400 json_validate_failed) ; effort « low » et plus de jetons (mesuré : ~270 ms) */
	const char *model = req->model != 0 ? req->model : GROQ_DEFAULT_MODEL;
	int reasoning = strstr (model, "gpt-oss") != 0;
	puts_ (&o, "],\"temperature\":0.7,");
	if (reasoning) puts_ (&o, "\"reasoning_effort\":\"low\",");
	puts_ (&o, "\"max_tokens\":");
	put_uint (&o, req->max_tokens != 0 ? req->max_tokens : reasoning ? 1024 : 2000);	/* (un paragraphe d'histoire, son dessin et son next : 1400 ne suffisait pas toujours) */
	puts_ (&o, ",\"response_format\":{\"type\":\"json_object\"}}");

	return o.full ? 0 : o.len;
}

/* ---- lecture ---------------------------------------------------------------- */

static int hexval (char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static unsigned hex4 (const char *p, int *ok)
{
	unsigned v = 0;
	for (int i = 0; i < 4; i++)
	{
		int h = hexval (p[i]);
		if (h < 0) { *ok = 0; return 0; }
		v = v << 4 | (unsigned) h;
	}
	*ok = 1;
	return v;
}

static void put_utf8 (out_t *o, unsigned cp)
{
	char u[4];
	size_t n;
	if (cp < 0x80) { u[0] = (char) cp; n = 1; }
	else if (cp < 0x800) { u[0] = (char) (0xC0 | cp >> 6); u[1] = (char) (0x80 | (cp & 0x3F)); n = 2; }
	else if (cp < 0x10000)
	{
		u[0] = (char) (0xE0 | cp >> 12); u[1] = (char) (0x80 | (cp >> 6 & 0x3F));
		u[2] = (char) (0x80 | (cp & 0x3F)); n = 3;
	}
	else
	{
		u[0] = (char) (0xF0 | cp >> 18); u[1] = (char) (0x80 | (cp >> 12 & 0x3F));
		u[2] = (char) (0x80 | (cp >> 6 & 0x3F)); u[3] = (char) (0x80 | (cp & 0x3F)); n = 4;
	}
	put (o, u, n);
}

/* la chaîne JSON qui commence à p (sur son guillemet) -> out ; le caractère qui suit le guillemet
 * fermant, ou 0 si mal formée */
static const char *decode_at (const char *p, out_t *o)
{
	if (*p++ != '"') return 0;
	while (*p != '\0' && *p != '"')
	{
		if (*p != '\\')
		{
			put (o, p++, 1);
			continue;
		}
		p++;
		switch (*p)
		{
		case 'n': put (o, "\n", 1); break;
		case 'r': put (o, "\r", 1); break;
		case 't': put (o, "\t", 1); break;
		case 'b': case 'f': put (o, " ", 1); break;
		case 'u':
		{
			int ok;
			unsigned cp = hex4 (p + 1, &ok);
			if (!ok) return 0;
			p += 4;
			if (cp >= 0xD800 && cp < 0xDC00 && p[1] == '\\' && p[2] == 'u')
			{
				unsigned lo = hex4 (p + 3, &ok);
				if (ok && lo >= 0xDC00 && lo < 0xE000)
				{
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					p += 6;
				}
			}
			put_utf8 (o, cp);
			break;
		}
		case '\0': return 0;
		default: put (o, p, 1); break;		/* \" \\ \/ */
		}
		p++;
	}
	return *p == '"' ? p + 1 : 0;
}

static int decode_string (const char *p, out_t *o)
{
	return decode_at (p, o) != 0;
}

/* la valeur de "key" : pointeur juste après les deux-points (espaces sautés), ou 0 */
static const char *find_value (const char *json, const char *key)
{
	size_t kl = strlen (key);
	for (const char *p = json; (p = strchr (p, '"')) != 0; p++)
	{
		if (strncmp (p + 1, key, kl) != 0 || p[1 + kl] != '"') continue;
		const char *q = p + 2 + kl;
		while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
		if (*q != ':') continue;
		q++;
		while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
		return q;
	}
	return 0;
}

int groq_json_string (const char *json, const char *key, char *out, size_t cap)
{
	out_t o = { out, cap, 0, 0 };
	if (cap == 0) return 0;
	out[0] = '\0';
	const char *q = json;
	while ((q = find_value (q, key)) != 0)
	{
		if (*q == '"') return decode_string (q, &o);
		/* (null, nombre, objet : pas une chaîne ; une clé homonyme plus loin peut l'être) */
	}
	return 0;
}

static void copy (char *out, size_t cap, const char *s)
{
	if (cap == 0) return;
	size_t n = strlen (s);
	if (n >= cap) n = cap - 1;
	memcpy (out, s, n);
	out[n] = '\0';
}

/* espaces de début et de fin ; minuscules ASCII (pour l'action) */
static void trim (char *s, int lower_ascii)
{
	size_t n = strlen (s), b = 0;
	while (b < n && (s[b] == ' ' || s[b] == '\n' || s[b] == '\r' || s[b] == '\t')) b++;
	while (n > b && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == '\t')) n--;
	memmove (s, s + b, n - b);
	s[n - b] = '\0';
	if (lower_ascii)
	{
		for (char *p = s; *p != '\0'; p++)
		{
			if (*p >= 'A' && *p <= 'Z') *p = (char) (*p - 'A' + 'a');
		}
	}
}

/* Un nom de variable libre, normalisé : minuscules, accents retirés (é -> e), espaces et tirets -> « _ »,
 * seulement a-z 0-9 _, une lettre d'abord, 2 à 20 caractères ; « heure » et « date » sont à l'horloge.
 * 1 si le nom est utilisable (name réécrit), 0 sinon. */
static int normalize_var (char *name)
{
	char out[32];
	size_t n = 0;
	for (const unsigned char *p = (const unsigned char *) name; *p != 0 && n + 1 < sizeof out; p++)
	{
		unsigned c = *p;
		if (c == 0xC3 && p[1] != 0)
		{
			unsigned d = *++p | 0x20;
			if (d >= 0xA0 && d <= 0xA5) c = 'a';
			else if (d == 0xA7) c = 'c';
			else if (d >= 0xA8 && d <= 0xAB) c = 'e';
			else if (d >= 0xAC && d <= 0xAF) c = 'i';
			else if (d >= 0xB2 && d <= 0xB6) c = 'o';
			else if (d >= 0xB9 && d <= 0xBC) c = 'u';
			else continue;
		}
		else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		else if (c == ' ' || c == '-' || c == '\'') c = '_';
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') out[n++] = (char) c;
	}
	out[n] = 0;
	if (n < 2 || n > 20 || out[0] < 'a' || out[0] > 'z') return 0;
	if (strcmp (out, "heure") == 0 || strcmp (out, "date") == 0) return 0;
	memcpy (name, out, n + 1);
	return 1;
}

/* une valeur vague (« Moi » : une phrase mal transcrite, « ça s'appelle mon épouse ») : rien à retenir */
static int vague_value (const char *value)
{
	static const char *const Vague[] = { "moi", "toi", "lui", "elle", "eux", "elles", "nous", "vous", "je", "tu",
		"il", "ils", "on", "ca", "cela", "ceci", "oui", "non", "rien", "personne", "quelqu_un", "inconnu",
		"inconnue", "aucun", "aucune", "mon_epouse", "ma_femme", "mon_mari", "ton_epouse", "ta_femme", 0 };
	char v[64];
	copy (v, sizeof v, value);
	if (!normalize_var (v)) return 0;
	for (unsigned i = 0; Vague[i] != 0; i++)
		if (strcmp (v, Vague[i]) == 0) return 1;
	return 0;
}

/* une valeur à retenir : un nom de variable valide, une valeur courte, sans marqueur ni accolade */
static void add_retenir (groq_reply *r, char *name, char *value)
{
	trim (name, 1);
	trim (value, 0);
	/* (une valeur vide ou un marqueur, « {couleur} » : le modèle répète ce qu'il sait déjà, rien à faire) */
	if (value[0] == '\0' || strchr (value, '{') != 0 || strchr (value, '}') != 0) return;
	if (vague_value (value)) { r->retenir_refuse = 1; return; }
	if (!normalize_var (name) || strlen (value) >= sizeof r->retenir[0].value)
	{
		r->retenir_refuse = 1;
		return;
	}
	unsigned i = 0;
	while (i < r->nretenir && strcmp (r->retenir[i].name, name) != 0) i++;	/* (même nom : remplacé) */
	if (i == GROQ_MAX_RETENIR) { r->retenir_refuse = 1; return; }
	copy (r->retenir[i].name, sizeof r->retenir[i].name, name);
	copy (r->retenir[i].value, sizeof r->retenir[i].value, value);
	if (i == r->nretenir) r->nretenir++;
}

static const char *skip_ws (const char *q)
{
	while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n' || *q == ',') q++;
	return q;
}

static int atoi_ (const char *s)
{
	int v = 0;
	while (*s == ' ' || *s == '"') s++;
	while (*s >= '0' && *s <= '9' && v < 1000) v = v * 10 + (*s++ - '0');
	return v;
}

/* {"app": "...", "args": [...]} qui commence à q (sur l'accolade) -> *o ; le caractère qui suit l'objet,
 * ou 0 s'il est mal formé */
static const char *parse_outcome (const char *q, groq_outcome *o)
{
	static char obj[4096];
	size_t i = 0;
	int depth = 0, str = 0;
	const char *c = q;
	memset (o, 0, sizeof *o);
	/* l'objet seul, jusqu'à l'accolade qui l'équilibre (hors chaînes) */
	for (; *c != '\0' && i + 2 < sizeof obj; c++)
	{
		obj[i++] = *c;
		if (str)
		{
			if (*c == '\\' && c[1] != '\0') obj[i++] = *++c;
			else if (*c == '"') str = 0;
			continue;
		}
		if (*c == '"') str = 1;
		else if (*c == '{') depth++;
		else if (*c == '}' && --depth == 0) { c++; break; }
	}
	obj[i] = '\0';
	if (depth != 0) return 0;

	char app[64];
	if (groq_json_string (obj, "app", app, sizeof app))
	{
		trim (app, 1);
		copy (o->app, sizeof o->app, app);
	}
	const char *a = find_value (obj, "args");
	if (a != 0 && *a == '"')		/* "args": "un seul" */
	{
		out_t ov = { o->args[0], sizeof o->args[0], 0, 0 };
		if (decode_at (a, &ov) != 0) { trim (o->args[0], 0); o->nargs = 1; }
	}
	else if (a != 0 && *a == '[')
	{
		for (a = skip_ws (a + 1); *a != '\0' && *a != ']' && o->nargs < GROQ_MAX_ARGS; a = skip_ws (a))
		{
			char *dst = o->args[o->nargs];
			out_t ov = { dst, sizeof o->args[0], 0, 0 };
			dst[0] = '\0';
			if (*a == '"')
			{
				if ((a = decode_at (a, &ov)) == 0) break;
			}
			else
			{
				size_t k = 0;			/* nombre, true / false */
				while (*a != '\0' && *a != ',' && *a != ']' && k + 1 < sizeof o->args[0]) dst[k++] = *a++;
				dst[k] = '\0';
			}
			trim (dst, 0);
			o->nargs++;
		}
	}
	return c;
}

static groq_outcome *add_outcome (groq_reply *r, const char *app)
{
	if (r->noutcomes >= GROQ_MAX_OUTCOMES) return 0;
	groq_outcome *o = &r->outcomes[r->noutcomes++];
	memset (o, 0, sizeof *o);
	copy (o->app, sizeof o->app, app);
	return o;
}

int groq_parse_response (const char *body, const groq_predicate *p, unsigned n, groq_reply *r)
{
	static char content[8192];		/* (pas sur la pile : petites piles de tâche) */
	memset (r, 0, sizeof *r);
	r->humeur = HUMEUR_NEUTRE;
	r->intensite = 1;

	if (!groq_json_string (body, "content", content, sizeof content))
	{
		/* {"error":{"message":"...","type":"..."}} */
		if (!groq_json_string (body, "message", r->erreur, sizeof r->erreur))
		{
			copy (r->erreur, sizeof r->erreur, "reponse illisible");
		}
		return 0;
	}

	/* {"outcomes": [{"app": ..., "args": [...]}, ...]} ; un contenu qui n'est pas du JSON (modèle
	 * indiscipliné) est dit tel quel ; un autre objet JSON n'a rien à dire */
	const char *q = find_value (content, "outcomes");
	if (q != 0 && *q == '[')
	{
		for (q = skip_ws (q + 1); *q == '{' && r->noutcomes < GROQ_MAX_OUTCOMES; q = skip_ws (q))
		{
			groq_outcome *o = &r->outcomes[r->noutcomes];
			if ((q = parse_outcome (q, o)) == 0) break;
			if (o->app[0] != '\0') r->noutcomes++;
		}
	}
	else
	{
		const char *c = content;
		groq_outcome *o;
		while (*c == ' ' || *c == '\n' || *c == '\r' || *c == '\t') c++;
		if (*c != '{' && *c != '\0' && (o = add_outcome (r, "say")) != 0)
		{
			copy (o->args[0], sizeof o->args[0], c);
			o->nargs = 1;
		}
	}

	/* la langue de la réponse, pour la voix : {"langue": "de", "outcomes": [...]} ; fr si absente ou inconnue */
	{
		char l[16];
		copy (r->langue, sizeof r->langue, "fr");
		if (groq_json_string (content, "langue", l, sizeof l) && l[0] != 0 && l[1] != 0)
		{
			char c[3] = { (char) (l[0] | 0x20), (char) (l[1] | 0x20), 0 };
			if (strcmp (c, "de") == 0 || strcmp (c, "en") == 0 || strcmp (c, "es") == 0 || strcmp (c, "it") == 0)
				copy (r->langue, sizeof r->langue, c);
		}
	}

	/* ce qu'il faut retenir sert déjà à démasquer les say (« Enchanté {nom} ! ») */
	for (unsigned i = 0; i < r->noutcomes; i++)
	{
		groq_outcome *o = &r->outcomes[i];
		if (strcmp (o->app, "retenir") != 0) continue;
		char k[32], v[128];
		copy (k, sizeof k, o->nargs > 0 ? o->args[0] : "");
		copy (v, sizeof v, o->nargs > 1 ? o->args[1] : "");
		unsigned before = r->nretenir;
		add_retenir (r, k, v);
		if (r->nretenir == before) o->app[0] = '\0';	/* (refusé ou rien à retenir : retiré de la liste) */
	}
	static groq_predicate merged[32];
	unsigned nm = 0;
	for (unsigned i = 0; i < r->nretenir; i++)
	{
		merged[nm].name = r->retenir[i].name;
		merged[nm].value = r->retenir[i].value;
		nm++;
	}
	for (unsigned i = 0; i < n && nm < 32; i++)
	{
		unsigned j = 0;
		while (j < r->nretenir && (p[i].name == 0 || strcmp (p[i].name, r->retenir[j].name) != 0)) j++;
		if (j == r->nretenir) merged[nm++] = p[i];
	}

	/* les apps de base ; les autres restent dans la liste pour l'hôte */
	out_t tm = { r->texte_marqueurs, sizeof r->texte_marqueurs, 0, 0 };
	out_t tx = { r->texte, sizeof r->texte, 0, 0 };
	for (unsigned i = 0; i < r->noutcomes; i++)
	{
		groq_outcome *o = &r->outcomes[i];
		if (strcmp (o->app, "say") == 0)
		{
			static char u[sizeof o->args[0]];
			trim (o->args[0], 0);
			if (o->args[0][0] == '\0') { o->app[0] = '\0'; continue; }
			if (tm.len > 0) puts_ (&tm, " ");
			puts_ (&tm, o->args[0]);
			groq_unmask (o->args[0], merged, nm, u, sizeof u);
			copy (o->args[0], sizeof o->args[0], u);
			/* la voix du TTS : la langue de la réponse ({"langue": "de", ...}), donnée par le modèle */
			copy (o->args[1], sizeof o->args[1], r->langue);
			o->nargs = 2;
			if (tx.len > 0) puts_ (&tx, " ");
			puts_ (&tx, u);
		}
		else if (strcmp (o->app, "draw") == 0 && o->nargs > 1)
		{
			/* une scène coupée en plusieurs arguments (une ligne chacun, vu avec ministral) : recollée ; le mémo
			 * est le premier argument qui ne commence pas par un mot de scène */
			static const char *const mots[] = { "fill ", "point ", "line ", "rect ", "circle ", "ellipse ", "poly ",
							    "text ", "icon ", "button ", "picture ", "clear", 0 };
			static char scene[sizeof o->args[0]];
			out_t sc = { scene, sizeof scene, 0, 0 };
			unsigned k = 0, memo = o->nargs;
			for (; k < o->nargs; k++)
			{
				const char *a = o->args[k];
				while (*a == ' ' || *a == '\n') a++;
				int verbe = 0;
				for (unsigned m = 0; mots[m]; m++) if (strncmp (a, mots[m], strlen (mots[m])) == 0) verbe = 1;
				if (!verbe && k > 0) { memo = k; break; }
				if (sc.len > 0 && scene[sc.len - 1] != '\n') puts_ (&sc, "\n");
				puts_ (&sc, o->args[k]);
			}
			if (memo > 1 && !sc.full)
			{
				copy (o->args[0], sizeof o->args[0], scene);
				if (memo < o->nargs) copy (o->args[1], sizeof o->args[1], o->args[memo]);
				else o->args[1][0] = '\0';
				o->nargs = 2;
			}
		}
		else if (strcmp (o->app, "ask") == 0)
		{
			/* une question à la personne : dite comme un say (marqueurs remplacés, langue de la réponse), puis
			 * l'hôte écoute et renvoie la réponse au second tour (tool_name « ask ») */
			static char u[sizeof o->args[0]];
			trim (o->args[0], 0);
			if (o->nargs == 0 || o->args[0][0] == '\0') { o->app[0] = '\0'; continue; }
			groq_unmask (o->args[0], merged, nm, u, sizeof u);
			copy (o->args[0], sizeof o->args[0], u);
			copy (o->args[1], sizeof o->args[1], r->langue);
			o->nargs = 2;
		}
		else if (strcmp (o->app, "humeur") == 0)
		{
			int known = 0;
			r->humeur = pocket_humeur_from_name (o->nargs > 0 ? o->args[0] : "", &known);
			r->humeur_inconnue = o->nargs > 0 && o->args[0][0] != '\0' && !known;
			r->intensite = o->nargs > 1 ? atoi_ (o->args[1]) : 1;
			if (r->intensite < HUMEUR_INTENSITE_MIN) r->intensite = HUMEUR_INTENSITE_MIN;
			if (r->intensite > HUMEUR_INTENSITE_MAX) r->intensite = HUMEUR_INTENSITE_MAX;
		}
		else if (strcmp (o->app, "geste") == 0)
		{
			trim (o->args[0], 1);
			if (o->nargs > 0 && groq_action_allowed (o->args[0])) copy (r->action, sizeof r->action, o->args[0]);
			else { r->action_refusee = 1; o->app[0] = '\0'; }
		}
		else if (strcmp (o->app, "retenir") == 0)
		{
			/* (déjà fait) */
		}
		else if (in_list (groq_commands, o->app))
		{
			if (r->commande[0] == '\0')
			{
				copy (r->commande, sizeof r->commande, o->app);
				copy (r->argument, sizeof r->argument, o->nargs > 0 ? o->args[0] : "");
				trim (r->argument, 0);
			}
		}
	}
	/* (les éléments retirés, app vide, sont enlevés) */
	unsigned w = 0;
	for (unsigned i = 0; i < r->noutcomes; i++)
	{
		if (r->outcomes[i].app[0] == '\0') continue;
		if (w != i) r->outcomes[w] = r->outcomes[i];
		w++;
	}
	r->noutcomes = w;
	/* l'humeur d'abord, même si le modèle l'a mise ailleurs : le visage change avant que Pocket agisse ou parle
	 * (les autres gardent leur ordre) */
	for (unsigned i = 1; i < r->noutcomes; i++)
	{
		if (strcmp (r->outcomes[i].app, "humeur") != 0) continue;
		static groq_outcome h;
		h = r->outcomes[i];
		unsigned j = i;
		while (j > 0 && strcmp (r->outcomes[j - 1].app, "humeur") != 0) { r->outcomes[j] = r->outcomes[j - 1]; j--; }
		r->outcomes[j] = h;
	}
	/* un draw passe avant les say qui le précèdent (demande du client, 25/10) : l'écran s'affiche pendant que Pocket
	 * parle, pas après ; avec des boutons aussi (l'hôte attend le toucher une fois tout dit) ; sans franchir un wiki,
	 * un askinfo ou un ask, dont la réponse remplace la suite */
	for (unsigned i = 1; i < r->noutcomes; i++)
	{
		if (strcmp (r->outcomes[i].app, "draw") != 0) continue;
		unsigned j = i;
		while (j > 0 && strcmp (r->outcomes[j - 1].app, "say") == 0) j--;
		if (j == i) continue;
		static groq_outcome d;
		d = r->outcomes[i];
		for (unsigned k = i; k > j; k--) r->outcomes[k] = r->outcomes[k - 1];
		r->outcomes[j] = d;
	}
	/* rien à dire alors qu'il a retenu (ou refusé de retenir) quelque chose, et aucune app ne parlera :
	 * Pocket répond quand même (« C'est noté. ») */
	if (tx.len == 0 && (r->nretenir > 0 || r->retenir_refuse) && r->noutcomes < GROQ_MAX_OUTCOMES)
	{
		int other = 0;
		for (unsigned i = 0; i < r->noutcomes; i++)
			if (strcmp (r->outcomes[i].app, "humeur") != 0 && strcmp (r->outcomes[i].app, "geste") != 0
			    && strcmp (r->outcomes[i].app, "retenir") != 0) other = 1;
		if (!other)
		{
			const char *t = r->nretenir > 0 ? "C'est not\xc3\xa9." : "Je n'ai pas bien compris, tu peux r\xc3\xa9p\xc3\xa9ter ?";
			groq_outcome *o = &r->outcomes[r->noutcomes++];
			memset (o, 0, sizeof *o);
			copy (o->app, sizeof o->app, "say");
			copy (o->args[0], sizeof o->args[0], t);
			copy (o->args[1], sizeof o->args[1], r->langue[0] ? r->langue : "fr");
			o->nargs = 2;
			puts_ (&tm, t);
			puts_ (&tx, t);
		}
	}

	if (r->noutcomes == 0)
	{
		copy (r->erreur, sizeof r->erreur, "reponse vide");
		return 0;
	}
	return 1;
}
