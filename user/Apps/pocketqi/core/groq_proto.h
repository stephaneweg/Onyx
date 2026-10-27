/* PocketQI (github.com/stephaneweg/PocketQI, commit 060b797), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * groq_proto.h -- Pocket alpha : le protocole de l'API Groq (OpenAI-compatible), sans réseau
 *
 * C pur (seulement <string.h>), commun au noyau Circle (commande telnet groq) et au banc PC
 * pocket_ecoute.exe (option -groq) : construit le corps JSON de chat/completions et lit la
 * réponse. Le transport (HTTPS) est celui de chaque côté.
 *
 * Mémoire de Pocket : AUCUNE valeur personnelle ne part chez Groq (décision du client). Le modèle
 * ne connaît que des marqueurs, {nom} {age} {couleur} {animal} {aliment} {heure} {date} :
 *  - à l'envoi, chaque valeur connue est remplacée par son marqueur, dans la phrase et dans
 *    l'historique (groq_mask ; « je m'appelle Léa » -> « je m'appelle {nom} ») ;
 *  - le modèle écrit le marqueur tel quel (« Bravo {nom} ! ») ;
 *  - à la réception, chaque marqueur est remplacé par sa valeur, ou par un mot de repli
 *    (« toi » pour {nom}), ou retiré proprement (groq_unmask).
 * L'historique est gardé sous forme de marqueurs (groq_reply.texte_marqueurs).
 *
 * Le modèle répond en JSON (response_format json_object) :
 *   { "texte": "...", "humeur": "joyeux", "intensite": 2, "action": "" }
 * L'action n'est gardée que si elle est dans la liste blanche (groq_action_allowed) ; une
 * humeur inconnue donne « neutre » (pocket_humeur.h).
 *
 * Confidentialité : la phrase reconnue, masquée, part chez Groq. La reconnaissance vocale, elle,
 * reste locale.
 */
#ifndef GROQ_PROTO_H
#define GROQ_PROTO_H

#include <stddef.h>
#include "pocket_humeur.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GROQ_HOST		"api.groq.com"
#define GROQ_URL		"https://api.groq.com/openai/v1/chat/completions"
/* Modèles réellement ouverts à la clé gratuite du client (GET /openai/v1/models, 22/10/2026) :
 * qwen/qwen3.8-27b, openai/gpt-oss-20b, openai/gpt-oss-120b... mais PAS les Llama (404). */
#define GROQ_DEFAULT_MODEL	"qwen/qwen3.8-27b"
#define GROQ_MAX_HISTORY	6	/* échanges gardés (question + réponse) */
#define GROQ_TEXT_MAX		2048	/* une réponse */

/* Les fournisseurs : même protocole (chat/completions compatible OpenAI, mode JSON), adresse,
 * modèle, clé à part. Mesuré le 22/10/2026 avec les clés gratuites du client :
 *  - groq    : qwen/qwen3.8-27b, 230 à 340 ms ; 1000 requêtes/jour ; les Llama : 404 ;
 *  - mistral : ministral-14b-latest, 1,1 s, meilleur français ; 188 requêtes/min ;
 *              mistral-small / medium : 429 permanent, mistral-large : 403 (offre gratuite).
 * Clé : SD:/etc/<nom>.key (noyau) ou la variable <NOM>_API_KEY (PC) ; modèle : SD:/etc/<nom>.model. */
typedef struct
{
	const char *name;		/* « groq », « mistral » */
	const char *host;		/* « api.groq.com » */
	const char *path;		/* « /openai/v1/chat/completions » */
	const char *url;		/* https://host/path */
	const char *default_model;
	const char *key_env;		/* « GROQ_API_KEY » */
} groq_provider;

extern const groq_provider groq_providers[];	/* terminé par { 0 } */
const groq_provider *groq_provider_find (const char *name);	/* 0 si inconnu */

/* les marqueurs connus du modèle (noms sans accolades), terminés par 0 */
extern const char *const groq_markers[];

/* une valeur de la mémoire de Pocket (prédicat des règles) : JAMAIS envoyée */
typedef struct
{
	const char *name;		/* « nom », « age »... (un des groq_markers) */
	const char *value;		/* « Léa », « 7 »... (0 ou "" : inconnue) */
} groq_predicate;

/* un échange passé, sous forme de marqueurs */
typedef struct
{
	const char *user;
	const char *galo;
	const char *langue;		/* la langue de cette réponse (fr de en...), 0 : non dite */
	const char *apps;		/* les apps lancées, en JSON, avant le say : « {"app": "todo", "args": ["add", "x"]}, »
					 * (0 : aucune) ; sans elles, le modèle imite des réponses sans app et n'en lance plus */
} groq_exchange;

typedef struct
{
	const char *model;		/* 0 : GROQ_DEFAULT_MODEL */
	const char *system;		/* 0 : groq_default_system () */
	const groq_predicate *predicates;	/* pour masquer seulement */
	unsigned npredicates;
	const groq_exchange *history;	/* du plus ancien au plus récent */
	unsigned nhistory;
	const char *user;		/* la phrase reconnue (UTF-8), en clair : masquée ici */
	unsigned max_tokens;		/* 0 : 300 (1024 pour les gpt-oss, qui raisonnent d'abord) */
	/* second tour après une commande : ce que le modèle a demandé et ce que Pocket a obtenu
	 * (masqué lui aussi) ; tool_name 0 : premier tour. tool_name « erreur » : un appel refusé par l'hôte, tool_arg
	 * « app \x1F arg \x1F arg... » (redonné au modèle tel qu'il l'a écrit), tool_result la raison du refus ; « echec » :
	 * de même, une app qui a tourné mais n'a pas pu (Fail de galo.bi), tool_result sa raison */
	const char *tool_name, *tool_arg, *tool_result;
	/* la date et l'heure du jour, en clair (pas une donnée personnelle), ajoutées au prompt système
	 * avec la consigne de calculer « demain », « dans deux jours »... ; ex. « vendredi 23 octobre
	 * 2026 (2026-10-23), 14 h 05 » ; 0 : non donnée (le modèle ne connaît alors que {date}) */
	const char *today;
	/* les applications que le modèle peut lancer : la section du prompt construite par l'hôte à partir
	 * des manifestes (SD:/apps/<app>/llm.txt) ; 0 : aucune */
	const char *apps;
} groq_request;

#define GROQ_MAX_ARGS		6
#define GROQ_MAX_OUTCOMES	8

/* Un élément de la réponse {"outcomes": [...]} : {"app": "agenda", "args": ["write", "2026-10-24", "14:00",
 * "dentiste"]}. Apps de base : say [texte, langue], humeur [nom, intensité], geste [nom], retenir [variable, valeur],
 * wiki [sujet], askinfo [app, action, args...], ask [question] (dite, puis la réponse au second tour) ; les autres sont des apps BASIC (SD:/apps/<app>/), que l'hôte vérifie contre leur manifeste
 * avant de lancer quoi que ce soit. Les arguments de say sont déjà démasqués ({nom} -> Léa). */
typedef struct
{
	char app[24];
	char args[GROQ_MAX_ARGS][1200];	/* (un say peut être un paragraphe d'histoire, next un résumé) */
	unsigned nargs;
} groq_outcome;

#define GROQ_MAX_RETENIR	4

/* ce que le modèle demande de retenir : un nom de variable libre (normalisé : epouse, chien...), la valeur en clair */
typedef struct
{
	char name[24];
	char value[64];
} groq_set;

typedef struct
{
	char texte[GROQ_TEXT_MAX];		/* à dire : marqueurs remplacés par les valeurs */
	char texte_marqueurs[GROQ_TEXT_MAX];	/* tel que reçu (pour l'historique) */
	char langue[4];				/* la langue du premier say : fr de en es it ("" : pas de say) ;
						 * chaque say a la sienne en args[1] */
	pocket_humeur humeur;
	int intensite;				/* 1..3 */
	int humeur_inconnue;			/* le modèle a proposé une humeur hors liste */
	char action[32];			/* "" ou une action de la liste blanche */
	int action_refusee;			/* le modèle a proposé une action hors liste */
	groq_set retenir[GROQ_MAX_RETENIR];	/* "retenir": {"nom": "Léa"} (noms permis seulement) */
	unsigned nretenir;
	int retenir_refuse;			/* une variable hors liste, ou une valeur invalide */
	char commande[16];			/* "" ou une commande de groq_commands[] */
	char argument[128];			/* son argument (« volcan ») */
	int commande_refusee;			/* une commande hors liste */
	groq_outcome outcomes[GROQ_MAX_OUTCOMES];	/* la réponse, dans l'ordre (les champs ci-dessus en
						 * sont tirés : texte = les say, humeur, action = geste...) */
	unsigned noutcomes;
	char erreur[256];			/* si groq_parse_response rend 0 */
} groq_reply;

/* les commandes que le modèle peut demander (« wiki »), terminées par 0. (Les variables à retenir sont
 * libres : un nom court normalisé, a-z 0-9 _, sauf heure et date ; les predicates de la requête sont la
 * mémoire connue, dont le prompt donne les marqueurs, jamais les valeurs.) */
extern const char *const groq_commands[];

/* le moment de la journée d'une heure 0..23, pour le prompt (« 1 h 47 » seul, le modèle le croit l'après-midi) :
 * « du matin, en pleine nuit », « du matin », « de l'apres-midi », « du soir » */
const char *groq_moment (int hour);

/* le prompt système par défaut (français : marqueurs, humeurs et actions permises), avec la phrase du ton du
 * caractère choisi */
const char *groq_default_system (void);

/* le caractère de Pocket : 0 sérieuse (défaut), 1 calme, 2 chaleureuse, 3 enjouée, 4 malicieuse ; l'hôte le lit
 * dans SD:/etc/llm.caractere (le curseur de l'admin web) avant chaque question */
#define GROQ_CARACTERE_MAX	4
void groq_set_caractere (int caractere);

/* l'écran de l'hôte (Pocket dessine, ../../dessin) : sa taille en pixels et s'il est tactile ; l'outcome draw [scène,
 * mémo] n'est décrit au LLM que si largeur > 0 (0 : pas d'écran, ou disable_draw). Second tour d'un draw à boutons :
 * tool_name « draw », tool_arg « scène  mémo » (le draw précédent), tool_result « bouton <id> <texte> » ou
 * « voix <ce qui a été dit> » */
void groq_set_ecran (int largeur, int hauteur, int tactile);

/* le prompt compact (moins de jetons, mêmes règles en style télégraphique) ; 0 : le complet (défaut) */
void groq_set_compact (int compact);

/* Les textes fixes du prompt, modifiables sans recompiler (25/10/2026) : chacun a un nom (« regles », « dessin »,
 * « tour_draw_juger »...) ; l'hôte donne une fonction qui lit SD:/etc/prompt/<nom>.txt dans buf (octets lus, 0 si
 * absent), appelée à chaque requête ; sans fichier, le texte du code. Un fichier a en tête des lignes « # ... » dont
 * « # version XXXXXXXX » : si le texte du code a changé depuis, le fichier est ignoré (groq_textes_ignores : les noms,
 * séparés par « , », depuis le dernier groq_build_request, pour le journal de l'hôte). */
typedef size_t groq_lire_texte (const char *nom, char *buf, size_t cap);
void groq_set_lire_texte (groq_lire_texte *lire);
const char *groq_textes_ignores (void);
/* le i-ème texte (0, 1, ... ; 0 au-delà du dernier) : son nom, le texte du code, son rôle */
int groq_texte (unsigned i, const char **nom, const char **defaut, const char **role);
/* le fichier du i-ème texte, prêt à écrire dans SD:/etc/prompt/<nom>.txt (en-tête et version compris) ; 0 si cap ne
 * suffit pas */
size_t groq_texte_fichier (unsigned i, char *out, size_t cap);
int groq_get_compact (void);
int groq_get_caractere (void);
const char *groq_caractere_nom (int caractere);		/* "serieuse"... "malicieuse" */
int groq_caractere_lire (const char *texte);		/* "3" ou "enjouee" -> 3 ; -1 sinon */

/* l'outcome maketext [nom, texte] : fichier = le nom sûr (« mail_alexandra.txt », à écrire dans SD:/tmp/ et nulle part
 * ailleurs), out = le brouillon de mail (« Subject: ... », « Lire: oui », ligne vide, texte) ; 0 si trop long */
int groq_maketext (const char *nom, const char *texte, char *fichier, size_t fcap, char *out, size_t ocap);

/* la liste blanche des actions (séparées par des espaces) */
const char *groq_allowed_actions (void);
int groq_action_allowed (const char *action);

/* valeurs connues -> marqueurs (mots entiers, casse et accents de la casse ignorés, plus longue
 * valeur d'abord) ; la longueur écrite, ou 0 si out est trop petit */
size_t groq_mask (const char *in, const groq_predicate *p, unsigned n, char *out, size_t cap);

/* marqueurs -> valeurs ; sans valeur : « toi » pour {nom} / {prenom}, sinon retiré (espaces
 * doublés et espaces avant la ponctuation nettoyés) */
size_t groq_unmask (const char *in, const groq_predicate *p, unsigned n, char *out, size_t cap);

/* corps JSON de la requête dans buf[cap] (terminé par 0) : phrase et historique masqués,
 * aucune valeur des prédicats ; la longueur, ou 0 si trop petit. Si sent n'est pas nul, la
 * phrase telle qu'envoyée y est copiée (pour le journal). */
size_t groq_build_request (const groq_request *req, char *buf, size_t cap, char *sent, size_t sent_cap);

/* La réponse HTTP (corps JSON de Groq) -> *r. 1 si une réponse a été trouvée ; 0 sinon, avec le
 * message d'erreur de l'API (ou une explication) dans r->erreur. Les prédicats servent à
 * remplacer les marqueurs. */
int groq_parse_response (const char *body, const groq_predicate *p, unsigned n, groq_reply *r);

/* Valeur de la chaîne JSON "key" (premier niveau rencontré dans le texte) décodée en UTF-8
 * dans out[cap] ; 1 si trouvée. Outil interne exposé pour les tests. */
int groq_json_string (const char *json, const char *key, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
