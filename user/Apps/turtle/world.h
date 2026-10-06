//
// world.h -- Turtle Quest's engine: the levels (their packs, a text file), the turtle's world, and a program's run
// recorded. No user interface here: the app (main.cpp) plays the record back, the host test
// (tools/tests/turtle/turtletest.cpp) checks every level's solution with it.
//
// A program is Onyx BASIC (user/Libs/basic) with the turtle's words (bas::setDialect): FORWARD, BACK, LEFT, RIGHT,
// PENUP, PENDOWN, COLOR, PICK, the sensors WALL (), WALLLEFT (), WALLRIGHT (), FRONT (), ONGOAL (), ITEM (), KEYS (),
// HEADING (), and REPEAT n ... END REPEAT; French names beside (AVANCE, GAUCHE, REPETE, SI, TANTQUE...) when the
// player chose French. run_program () compiles it and runs it at once on a copy of the level, with the VM's
// statement hook (Host::lineHook): every statement started, every move, turn, pick is an event of the record --
// the app shows them one by one (the line lit, the turtle walking), at the speed chosen or a statement a step.
// A wall hit, a locked door, nothing to pick: a run-time error at that line; a run that never ends is stopped.
//
// The packs ("*.turtle"): sections [pack] and [level], "key = value" lines; a value on several lines is given by
// the lines that follow, each starting with "|" (a map, a solution). The texts have a French version under the
// same key + ".fr". A level:
//   id, title, text (what to do), hint, concept (the help card shown the first time: move, turn, pick, door,
//   repeat, for, if, sensor, while, maze, variable, sub, pen, draw), words (the turtle's words the level knows:
//   the palette; another of them is refused), par (the instruction counts for 3 and 2 stars: "3 5"), draw (1: a
//   drawing level -- reproduce the solution's figure), start (the code the player starts with), solution, map:
//     #  a wall          .  the floor           *  the goal (a flag)       k  a key        c  a coin to pick
//     D  a door (a key opens it, the key is used)                         p  a tile to paint (walk on it, the pen down)
//     ^ > v <  the turtle at the start, its heading (north, east, south, west)
// A level is won when the program ends with the turtle on the goal (if there is one), every coin picked, every
// tile painted and, a drawing level, the figure drawn (the pen's lines are the solution's, in any order).
// Stars: 1 won; 2 in no more instructions than par's second number; 3 no more than its first (one statement,
// or one line of a block, is an instruction: a loop counts as its lines, not its turns).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _turtle_world_h
#define _turtle_world_h

#include "basic/bas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace turtle {

enum { LANG_EN = 0, LANG_FR = 1 };
enum { MAXW = 24, MAXH = 18 };

// ---- a growable array ---------------------------------------------------------------------------------------------------
template <class T> struct Arr
{
	T *d = 0; int n = 0, cap = 0;
	Arr () {}
	~Arr () { delete [] d; }
	void push (const T &v) { if (n == cap) grow (cap ? cap * 2 : 16); d[n++] = v; }
	void grow (int c) { T *nd = new T[c]; for (int i = 0; i < n; i++) nd[i] = d[i]; delete [] d; d = nd; cap = c; }
	void clear () { n = 0; }
	void erase (int i) { for (int k = i; k + 1 < n; k++) d[k] = d[k + 1]; n--; }
	T &operator[] (int i) { return d[i]; }
	const T &operator[] (int i) const { return d[i]; }
	void copyFrom (const Arr &o) { clear (); if (cap < o.n) grow (o.n); for (int i = 0; i < o.n; i++) d[i] = o.d[i]; n = o.n; }
private:
	Arr (const Arr &);
	Arr &operator= (const Arr &);
};

static inline char *tdup (const char *s) { size_t n = strlen (s ? s : ""); char *d = (char *) malloc (n + 1); memcpy (d, s ? s : "", n + 1); return d; }
static inline void tcpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; if (cap > 0) d[i] = 0; }
static inline char tup (char c) { return c >= 'a' && c <= 'z' ? (char) (c - 32) : c; }

// ---- the words ---------------------------------------------------------------------------------------------------------
enum
{
	W_FORWARD = 1, W_BACK, W_LEFT, W_RIGHT, W_PENUP, W_PENDOWN, W_COLOR, W_PICK,
	W_WALL, W_WALLLEFT, W_WALLRIGHT, W_FRONT, W_ONGOAL, W_ITEM, W_KEYS, W_HEADING,
	W_COUNT_
};
static const bas::ExtWord WORDS[] = {
	{ "FORWARD", W_FORWARD, 's', "[N" }, { "BACK", W_BACK, 's', "[N" }, { "LEFT", W_LEFT, 's', "[N" }, { "RIGHT", W_RIGHT, 's', "[N" },
	{ "PENUP", W_PENUP, 's', "" }, { "PENDOWN", W_PENDOWN, 's', "" }, { "COLOR", W_COLOR, 's', "N" }, { "PICK", W_PICK, 's', "" },
	{ "WALL", W_WALL, 'n', "" }, { "WALLLEFT", W_WALLLEFT, 'n', "" }, { "WALLRIGHT", W_WALLRIGHT, 'n', "" }, { "FRONT", W_FRONT, 'n', "" },
	{ "ONGOAL", W_ONGOAL, 'n', "" }, { "ITEM", W_ITEM, 'n', "" }, { "KEYS", W_KEYS, 'n', "" }, { "HEADING", W_HEADING, 'n', "" },
	{ 0, 0, 0, 0 } };
// The French names: the turtle's (by id: W_FORWARD ...), then BASIC's keywords
static const char *const FR_NAME[W_COUNT_] = { "", "AVANCE", "RECULE", "GAUCHE", "DROITE", "LEVECRAYON", "BAISSECRAYON", "COULEUR", "RAMASSE",
	"MUR", "MURGAUCHE", "MURDROITE", "DEVANT", "SURBUT", "OBJET", "CLES", "CAP" };
static const char *const ALIASES_FR[] = {
	"AVANCE", "FORWARD", "AV", "FORWARD", "RECULE", "BACK", "RE", "BACK", "GAUCHE", "LEFT", "TG", "LEFT", "DROITE", "RIGHT", "TD", "RIGHT",
	"LEVECRAYON", "PENUP", "LC", "PENUP", "BAISSECRAYON", "PENDOWN", "BC", "PENDOWN", "COULEUR", "COLOR", "RAMASSE", "PICK",
	"MUR", "WALL", "MURGAUCHE", "WALLLEFT", "MURDROITE", "WALLRIGHT", "DEVANT", "FRONT", "SURBUT", "ONGOAL", "OBJET", "ITEM",
	"CLES", "KEYS", "CAP", "HEADING",
	"REPETE", "REPEAT", "SI", "IF", "ALORS", "THEN", "SINON", "ELSE", "SINONSI", "ELSEIF", "FIN", "END", "POUR", "FOR", "JUSQUA", "TO",
	"SUIVANT", "NEXT", "TANTQUE", "WHILE", "FINTANTQUE", "WEND", "FAIRE", "DO", "BOUCLE", "LOOP", "JUSQUE", "UNTIL",
	"PROCEDURE", "SUB", "APPELLE", "CALL", "ET", "AND", "OU", "OR", "NON", "NOT", "AFFICHE", "PRINT", "SORTIR", "EXIT", 0 };
static const bas::Dialect DIALECT_EN = { WORDS, 0, true };
static const bas::Dialect DIALECT_FR = { WORDS, ALIASES_FR, true };
static inline void set_language (int lang) { bas::setDialect (lang == LANG_FR ? &DIALECT_FR : &DIALECT_EN); }
static inline int word_id (const char *w)
{
	char u[24]; int n = 0; for (; w[n] && n < 23; n++) u[n] = tup (w[n]); u[n] = 0;
	for (int i = 0; WORDS[i].name; i++) if (!strcmp (WORDS[i].name, u)) return WORDS[i].id;
	for (int i = 1; i < W_COUNT_; i++) if (!strcmp (FR_NAME[i], u)) return i;
	for (int i = 0; ALIASES_FR[i]; i += 2) if (!strcmp (ALIASES_FR[i], u)) { for (int k = 0; WORDS[k].name; k++) if (!strcmp (WORDS[k].name, ALIASES_FR[i + 1])) return WORDS[k].id; }
	return 0;
}
static inline const char *word_name (int id, int lang)
{
	if (lang == LANG_FR && id > 0 && id < W_COUNT_) return FR_NAME[id];
	for (int i = 0; WORDS[i].name; i++) if (WORDS[i].id == id) return WORDS[i].name;
	return "";
}
// A word's short help, in the language (its form, what it does)
static inline const char *word_help (int id, int lang)
{
	static const char *const EN[W_COUNT_] = { "",
		"FORWARD [n] -- moves n squares ahead (1 if no n)", "BACK [n] -- moves n squares back",
		"LEFT [degrees] -- turns left (90 if none)", "RIGHT [degrees] -- turns right (90 if none)",
		"PENUP -- lifts the pen: no more trail", "PENDOWN -- puts the pen down: the turtle draws",
		"COLOR n -- the pen's colour (0 to 15)", "PICK -- picks up the key or the coin under the turtle",
		"WALL () -- true when a wall (or a locked door) is just ahead", "WALLLEFT () -- true when a wall is on the left",
		"WALLRIGHT () -- true when a wall is on the right", "FRONT () -- what is ahead: 0 free, 1 wall, 2 something to pick, 3 the goal, 4 a door",
		"ONGOAL () -- true when the turtle is on the goal", "ITEM () -- true when there is something to pick under the turtle",
		"KEYS () -- how many keys the turtle carries", "HEADING () -- where the turtle looks, in degrees (0 north, 90 east)" };
	static const char *const FR[W_COUNT_] = { "",
		"AVANCE [n] -- avance de n cases (1 sans n)", "RECULE [n] -- recule de n cases",
		"GAUCHE [degrés] -- tourne à gauche (90 sans nombre)", "DROITE [degrés] -- tourne à droite (90 sans nombre)",
		"LEVECRAYON -- lève le crayon : plus de tracé", "BAISSECRAYON -- baisse le crayon : la tortue dessine",
		"COULEUR n -- la couleur du crayon (0 à 15)", "RAMASSE -- ramasse la clé ou la pièce sous la tortue",
		"MUR () -- vrai quand un mur (ou une porte fermée) est juste devant", "MURGAUCHE () -- vrai quand un mur est à gauche",
		"MURDROITE () -- vrai quand un mur est à droite", "DEVANT () -- ce qu'il y a devant : 0 libre, 1 mur, 2 objet, 3 l'arrivée, 4 une porte",
		"SURBUT () -- vrai quand la tortue est sur l'arrivée", "OBJET () -- vrai quand il y a quelque chose à ramasser ici",
		"CLES () -- le nombre de clés que porte la tortue", "CAP () -- où regarde la tortue, en degrés (0 nord, 90 est)" };
	return id > 0 && id < W_COUNT_ ? (lang == LANG_FR ? FR[id] : EN[id]) : "";
}

// ---- the concepts' help cards ------------------------------------------------------------------------------------------
struct Concept { const char *key; const char *title[2]; const char *text[2]; };
static const Concept CONCEPTS[] = {
	{ "move", { "Moving", "Avancer" },
	  { "The turtle obeys your program, one line after the other.\n\nFORWARD 3 moves it three squares ahead. FORWARD alone: one square.\n\nWrite the instructions, then press Run.",
	    "La tortue obéit à ton programme, une ligne après l'autre.\n\nAVANCE 3 la fait avancer de trois cases. AVANCE seul : une case.\n\nÉcris les instructions, puis appuie sur Lancer." } },
	{ "turn", { "Turning", "Tourner" },
	  { "LEFT and RIGHT turn the turtle a quarter of a turn, where it stands.\n\nFORWARD then goes the new way. Think as if you were the turtle: its left is not always yours!",
	    "GAUCHE et DROITE font tourner la tortue d'un quart de tour, sur place.\n\nAVANCE part alors dans la nouvelle direction. Mets-toi à la place de la tortue : sa gauche n'est pas toujours la tienne !" } },
	{ "pick", { "Picking up", "Ramasser" },
	  { "Coins and keys lie on the floor. Stop on one and write PICK to take it.\n\nA level is won only when every coin is picked.",
	    "Des pièces et des clés traînent par terre. Arrête-toi dessus et écris RAMASSE pour la prendre.\n\nUn niveau n'est gagné que si toutes les pièces sont ramassées." } },
	{ "door", { "Doors and keys", "Portes et clés" },
	  { "A door is locked. Walk into it with a key and it opens (the key stays in the lock).\n\nPick the key first!",
	    "Une porte est fermée à clé. Avance dedans avec une clé et elle s'ouvre (la clé reste dans la serrure).\n\nRamasse d'abord la clé !" } },
	{ "repeat", { "Loops: REPEAT", "Les boucles : REPETE" },
	  { "Writing the same lines again and again is long. A loop repeats them for you:\n\n  REPEAT 4\n    FORWARD 2\n    RIGHT\n  END REPEAT\n\nFewer instructions = more stars.",
	    "Écrire les mêmes lignes encore et encore, c'est long. Une boucle les répète pour toi :\n\n  REPETE 4\n    AVANCE 2\n    DROITE\n  FIN REPETE\n\nMoins d'instructions = plus d'étoiles." } },
	{ "for", { "Loops that count: FOR", "Les boucles qui comptent : POUR" },
	  { "FOR counts for you, and the counter is a variable you can use:\n\n  FOR i = 1 TO 5\n    FORWARD i\n    RIGHT\n  NEXT\n\ni is 1, then 2, then 3...",
	    "POUR compte pour toi, et le compteur est une variable que tu peux utiliser :\n\n  POUR i = 1 JUSQUA 5\n    AVANCE i\n    DROITE\n  SUIVANT\n\ni vaut 1, puis 2, puis 3..." } },
	{ "if", { "Deciding: IF", "Décider : SI" },
	  { "IF tests something and does the lines after THEN only when it is true:\n\n  IF ITEM () THEN PICK\n\nWith ELSE, other lines when it is false. A block IF ends with END IF.",
	    "SI teste quelque chose et fait les lignes après ALORS seulement quand c'est vrai :\n\n  SI OBJET () ALORS RAMASSE\n\nAvec SINON, d'autres lignes quand c'est faux. Un bloc SI se termine par FIN SI." } },
	{ "sensor", { "The turtle's senses", "Les sens de la tortue" },
	  { "The turtle can look around: WALL () is true when a wall is just ahead, WALLLEFT () and WALLRIGHT () look aside.\n\n  IF WALL () THEN RIGHT\n\nThe program works whatever the level looks like.",
	    "La tortue peut regarder autour d'elle : MUR () est vrai quand un mur est juste devant, MURGAUCHE () et MURDROITE () regardent sur les côtés.\n\n  SI MUR () ALORS DROITE\n\nLe programme marche quelle que soit la forme du niveau." } },
	{ "while", { "Until it is done: WHILE", "Tant que : TANTQUE" },
	  { "WHILE repeats its lines as long as a condition is true -- no need to count:\n\n  WHILE NOT ONGOAL ()\n    FORWARD\n  WEND",
	    "TANTQUE répète ses lignes aussi longtemps qu'une condition est vraie -- pas besoin de compter :\n\n  TANTQUE NON SURBUT ()\n    AVANCE\n  FINTANTQUE" } },
	{ "maze", { "Mazes", "Les labyrinthes" },
	  { "A trick to leave any maze: keep a hand on the wall on your right.\n\nIf there is no wall on the right, turn right and step. Else, if the way ahead is free, step. Else turn left.",
	    "Une astuce pour sortir de n'importe quel labyrinthe : garde une main sur le mur de droite.\n\nS'il n'y a pas de mur à droite, tourne à droite et avance. Sinon, si la voie est libre, avance. Sinon tourne à gauche." } },
	{ "variable", { "Variables", "Les variables" },
	  { "A variable is a box with a name that holds a number:\n\n  n = 1\n  REPEAT 6\n    FORWARD n\n    RIGHT\n    n = n + 1\n  END REPEAT\n\nEach turn, n grows: a spiral!",
	    "Une variable est une boîte avec un nom qui garde un nombre :\n\n  n = 1\n  REPETE 6\n    AVANCE n\n    DROITE\n    n = n + 1\n  FIN REPETE\n\nÀ chaque tour n grandit : une spirale !" } },
	{ "sub", { "Your own words: SUB", "Tes propres mots : PROCEDURE" },
	  { "Teach the turtle a new word with SUB, then use it as many times as you like:\n\n  SUB Step3\n    FORWARD 3\n    PICK\n  END SUB\n\n  Step3\n  Step3\n\nA SUB can take values: SUB Square (size).",
	    "Apprends un nouveau mot à la tortue avec PROCEDURE, puis utilise-le autant que tu veux :\n\n  PROCEDURE Pas3\n    AVANCE 3\n    RAMASSE\n  FIN PROCEDURE\n\n  Pas3\n  Pas3\n\nUne procédure peut recevoir des valeurs : PROCEDURE Carre (cote)." } },
	{ "pen", { "The pen", "Le crayon" },
	  { "The turtle draws where it walks: its pen is down. PENUP lifts it (to move without drawing), PENDOWN puts it back, COLOR changes its colour.\n\nPaint every marked tile!",
	    "La tortue dessine là où elle passe : son crayon est baissé. LEVECRAYON le lève (pour bouger sans dessiner), BAISSECRAYON le remet, COULEUR change sa couleur.\n\nPeins toutes les cases marquées !" } },
	{ "draw", { "Drawing figures", "Dessiner des figures" },
	  { "Reproduce the grey figure. The turtle can turn by any angle: RIGHT 120 for a triangle's corner, RIGHT 60 for a hexagon's.\n\nThe turns of a closed figure always add up to 360 degrees.",
	    "Reproduis la figure grise. La tortue peut tourner de n'importe quel angle : DROITE 120 pour le coin d'un triangle, DROITE 60 pour un hexagone.\n\nLes virages d'une figure fermée font toujours 360 degrés en tout." } },
	{ 0, { 0, 0 }, { 0, 0 } } };
static inline const Concept *find_concept (const char *k)
{
	for (int i = 0; CONCEPTS[i].key; i++) if (!strcmp (CONCEPTS[i].key, k)) return &CONCEPTS[i];
	return 0;
}

// ---- the levels ----------------------------------------------------------------------------------------------------------
struct Level
{
	char id[32];
	char title[2][80], text[2][480], hint[2][480];
	char topic[16];				// (its key in the pack: concept)
	char words[256];				// the turtle's words the level knows (empty: all)
	int  par3, par2;				// instruction counts for 3 and 2 stars
	bool draw;					// a drawing level
	int  w, h;
	char map[MAXH][MAXW + 1];
	char *start, *solution;				// (malloc'd)
	Level () { memset (this, 0, sizeof *this); }
	~Level () { free (start); free (solution); }
	void set (const Level &o)
	{
		char *s = start, *so = solution;
		memcpy (this, &o, sizeof *this);
		start = o.start ? tdup (o.start) : 0; solution = o.solution ? tdup (o.solution) : 0;
		free (s); free (so);
	}
	const char *titleOf (int lang) const { return title[lang][0] ? title[lang] : title[0]; }
	const char *textOf (int lang) const { return text[lang][0] ? text[lang] : text[0]; }
	const char *hintOf (int lang) const { return hint[lang][0] ? hint[lang] : hint[0]; }
	char at (int x, int y) const { return x < 0 || y < 0 || x >= w || y >= h ? '#' : map[y][x]; }
	bool knows (int wid) const
	{
		if (!words[0]) return true;
		const char *nm = word_name (wid, LANG_EN);
		for (const char *p = words; *p; )
		{
			while (*p == ' ' || *p == ',') p++;
			char t[24]; int n = 0; while (*p && *p != ' ' && *p != ',') { if (n < 23) t[n++] = tup (*p); p++; } t[n] = 0;
			if (n && !strcmp (t, nm)) return true;
		}
		return false;
	}
};
struct Pack
{
	char title[2][80];
	char path[256];
	bool user;					// the player's own (the editor writes it)
	Arr<Level *> levels;
	Pack () { title[0][0] = title[1][0] = 0; path[0] = 0; user = false; }
	~Pack () { for (int i = 0; i < levels.n; i++) delete levels[i]; }
	const char *titleOf (int lang) const { return title[lang][0] ? title[lang] : title[0]; }
};

// A pack's text -> its levels (false: not one; why)
static inline bool parse_pack (Pack &pk, const char *src, char *why, int cap)
{
	Level *L = 0; bool inPack = false;
	char key[40] = ""; int lang = 0;
	char *multi = 0; size_t mlen = 0;		// the value being gathered from "|" lines
	auto flush = [&] () {
		if (!multi) return;
		if (L && !strcmp (key, "map"))
		{
			L->h = 0; L->w = 0;
			for (char *p = multi; *p && L->h < MAXH; )
			{
				char *e = strchr (p, '\n'); size_t n = e ? (size_t) (e - p) : strlen (p);
				if (n > MAXW) n = MAXW;
				memcpy (L->map[L->h], p, n); L->map[L->h][n] = 0;
				if ((int) n > L->w) L->w = (int) n;
				L->h++;
				p = e ? e + 1 : p + strlen (p);
			}
			for (int y = 0; y < L->h; y++) { int n = (int) strlen (L->map[y]); for (int x = n; x < L->w; x++) L->map[y][x] = ' '; L->map[y][L->w] = 0; }
		}
		else if (L && !strcmp (key, "solution")) { free (L->solution); L->solution = multi; multi = 0; }
		else if (L && !strcmp (key, "start")) { free (L->start); L->start = multi; multi = 0; }
		else if (L && !strcmp (key, "text")) tcpy (L->text[lang], multi, sizeof L->text[0]);
		else if (L && !strcmp (key, "hint")) tcpy (L->hint[lang], multi, sizeof L->hint[0]);
		free (multi); multi = 0; mlen = 0;
	};
	for (const char *p = src; *p; )
	{
		const char *e = strchr (p, '\n'); size_t n = e ? (size_t) (e - p) : strlen (p);
		char line[600]; if (n >= sizeof line) n = sizeof line - 1;
		memcpy (line, p, n); line[n] = 0;
		p = e ? e + 1 : p + strlen (p);
		if (n && line[n - 1] == '\r') line[--n] = 0;
		if (line[0] == '|')
		{
			const char *v = line[1] == ' ' ? line + 2 : line + 1;
			size_t vl = strlen (v);
			multi = (char *) realloc (multi, mlen + vl + 2);
			if (mlen) multi[mlen++] = '\n';
			memcpy (multi + mlen, v, vl); mlen += vl; multi[mlen] = 0;
			continue;
		}
		flush ();
		char *s = line; while (*s == ' ' || *s == '\t') s++;
		if (!*s || *s == '#' || *s == ';') continue;
		if (*s == '[')
		{
			if (!strncmp (s, "[pack]", 6)) { inPack = true; L = 0; }
			else if (!strncmp (s, "[level]", 7)) { inPack = false; L = new Level; L->par3 = L->par2 = 0; pk.levels.push (L); }
			continue;
		}
		char *eq = strchr (s, '='); if (!eq) continue;
		*eq = 0; char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
		char *ke = eq; while (ke > s && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = 0;
		size_t vl = strlen (v); while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\t')) v[--vl] = 0;
		lang = 0;
		size_t kl = strlen (s);
		if (kl > 3 && !strcmp (s + kl - 3, ".fr")) { s[kl - 3] = 0; lang = 1; }
		tcpy (key, s, sizeof key);
		if (inPack) { if (!strcmp (key, "title")) tcpy (pk.title[lang], v, sizeof pk.title[0]); continue; }
		if (!L) continue;
		if (!strcmp (key, "id")) tcpy (L->id, v, sizeof L->id);
		else if (!strcmp (key, "title")) tcpy (L->title[lang], v, sizeof L->title[0]);
		else if (!strcmp (key, "text")) tcpy (L->text[lang], v, sizeof L->text[0]);
		else if (!strcmp (key, "hint")) tcpy (L->hint[lang], v, sizeof L->hint[0]);
		else if (!strcmp (key, "concept")) tcpy (L->topic, v, sizeof L->topic);
		else if (!strcmp (key, "words")) tcpy (L->words, v, sizeof L->words);
		else if (!strcmp (key, "par")) { L->par3 = atoi (v); const char *q = strchr (v, ' '); L->par2 = q ? atoi (q) : L->par3 * 2; }
		else if (!strcmp (key, "draw")) L->draw = atoi (v) != 0;
		else if (!strcmp (key, "solution") && *v) { free (L->solution); L->solution = tdup (v); }
		else if (!strcmp (key, "start") && *v) { free (L->start); L->start = tdup (v); }
		// (an empty "map =", "solution =": its "|" lines follow)
	}
	flush ();
	for (int i = 0; i < pk.levels.n; i++)
	{
		Level *l = pk.levels[i];
		if (!l->w || !l->h) { snprintf (why, (size_t) cap, "level %d (%s) has no map", i + 1, l->id); return false; }
		if (!l->id[0]) snprintf (l->id, sizeof l->id, "level%d", i + 1);
	}
	if (!pk.levels.n) { snprintf (why, (size_t) cap, "no [level] in the pack"); return false; }
	return true;
}
// A pack written back as text (the editor's): the caller frees it
static inline char *write_pack (const Pack &pk)
{
	size_t cap = 4096, n = 0; char *o = (char *) malloc (cap);
	auto put = [&] (const char *s) { size_t l = strlen (s); while (n + l + 1 > cap) { cap *= 2; o = (char *) realloc (o, cap); } memcpy (o + n, s, l); n += l; o[n] = 0; };
	auto kv = [&] (const char *k, const char *v) { if (!v || !*v) return; put (k); put (" = "); put (v); put ("\n"); };
	auto block = [&] (const char *k, const char *v) {
		if (!v || !*v) return;
		put (k); put (" =\n");
		for (const char *p = v; *p; ) { const char *e = strchr (p, '\n'); put ("| "); char line[600]; size_t l = e ? (size_t) (e - p) : strlen (p); if (l >= sizeof line) l = sizeof line - 1; memcpy (line, p, l); line[l] = 0; put (line); put ("\n"); p = e ? e + 1 : p + strlen (p); }
	};
	put ("# Turtle Quest -- a pack of levels (user/Apps/turtle/world.h says the format)\n[pack]\n");
	kv ("title", pk.title[0]); kv ("title.fr", pk.title[1]);
	for (int i = 0; i < pk.levels.n; i++)
	{
		const Level &L = *pk.levels[i];
		char t[64];
		put ("\n[level]\n");
		kv ("id", L.id); kv ("title", L.title[0]); kv ("title.fr", L.title[1]);
		kv ("text", L.text[0]); kv ("text.fr", L.text[1]); kv ("hint", L.hint[0]); kv ("hint.fr", L.hint[1]);
		kv ("concept", L.topic); kv ("words", L.words);
		if (L.par3) { snprintf (t, sizeof t, "%d %d", L.par3, L.par2); kv ("par", t); }
		if (L.draw) kv ("draw", "1");
		put ("map =\n");
		for (int y = 0; y < L.h; y++) { put ("| "); put (L.map[y]); put ("\n"); }
		block ("start", L.start); block ("solution", L.solution);
	}
	return o;
}

// ---- counting the instructions --------------------------------------------------------------------------------------------
// Every statement of the program but the ends of blocks (NEXT, END REPEAT, END IF, WEND, LOOP, END SUB, ELSE...) and
// the comments; "a: b" are two.
static inline int count_instructions (const char *src)
{
	int count = 0;
	for (const char *p = src; *p; )
	{
		const char *e = strchr (p, '\n'); int n = e ? (int) (e - p) : (int) strlen (p);
		char line[600]; if (n >= (int) sizeof line) n = sizeof line - 1;
		memcpy (line, p, n); line[n] = 0;
		p = e ? e + 1 : p + strlen (p);
		// cut the comment, split at ':' outside strings
		bool q = false; int st = 0;
		for (int i = 0; ; i++)
		{
			char c = line[i];
			if (c == '"') q = !q;
			bool cut = !c || (!q && (c == ':' || c == '\''));
			if (!cut) continue;
			char part[600]; int k = 0;
			for (int j = st; j < i; j++) part[k++] = tup (line[j]);
			part[k] = 0;
			char *s = part; while (*s == ' ' || *s == '\t') s++;
			int l = (int) strlen (s); while (l && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\r')) s[--l] = 0;
			char w1[24] = ""; sscanf (s, "%23s", w1);
			bool closing = !*s || !strcmp (w1, "NEXT") || !strcmp (w1, "WEND") || !strcmp (w1, "LOOP") || !strcmp (w1, "ELSE") || !strcmp (w1, "REM")
				|| !strcmp (w1, "END") || !strcmp (w1, "FIN") || !strcmp (w1, "SUIVANT") || !strcmp (w1, "FINTANTQUE") || !strcmp (w1, "BOUCLE") || !strcmp (w1, "SINON");
			if (!strcmp (w1, "END") && !s[3]) closing = false;	// END alone: the program's end, a statement
			if (!closing) count++;
			if (!c || c == '\'') break;
			st = i + 1;
		}
	}
	return count;
}

// ---- the world -----------------------------------------------------------------------------------------------------------
struct Seg { float x1, y1, x2, y2; int color; };
struct World
{
	const Level *L;
	char cell[MAXH][MAXW + 1];			// the map now (doors opened, things picked)
	unsigned char painted[MAXH][MAXW];
	double x, y, h;					// the turtle: its place (cells' centres at whole numbers), its heading (0 north, clockwise)
	bool pen; int color; int keys, coins, coinsTotal;
	bool hasGoal; int gx, gy;
	Arr<Seg> segs;
	World () : L (0) {}
	void reset (const Level &lv)
	{
		L = &lv; x = y = 0; h = 90; pen = true; color = 1; keys = coins = coinsTotal = 0; hasGoal = false; gx = gy = 0;
		segs.clear ();
		memset (painted, 0, sizeof painted);
		for (int r = 0; r < lv.h; r++)
			for (int c = 0; c < lv.w; c++)
			{
				char k = lv.map[r][c];
				if (k == '^' || k == '>' || k == 'v' || k == '<') { x = c; y = r; h = k == '^' ? 0 : k == '>' ? 90 : k == 'v' ? 180 : 270; k = '.'; }
				if (k == '*') { hasGoal = true; gx = c; gy = r; }
				if (k == 'c') coinsTotal++;
				cell[r][c] = k;
			}
		paintHere ();
	}
	void copyFrom (const World &o)
	{
		L = o.L; memcpy (cell, o.cell, sizeof cell); memcpy (painted, o.painted, sizeof painted);
		x = o.x; y = o.y; h = o.h; pen = o.pen; color = o.color; keys = o.keys; coins = o.coins; coinsTotal = o.coinsTotal;
		hasGoal = o.hasGoal; gx = o.gx; gy = o.gy; segs.copyFrom (o.segs);
	}
	char at (int c, int r) const { return c < 0 || r < 0 || !L || c >= L->w || r >= L->h ? '#' : cell[r][c]; }
	int cx () const { return (int) floor (x + 0.5); }
	int cy () const { return (int) floor (y + 0.5); }
	void paintHere () { int c = cx (), r = cy (); if (pen && L && c >= 0 && r >= 0 && c < L->w && r < L->h) painted[r][c] = 1; }
	static void dir (double hd, double &dx, double &dy) { double a = hd * 3.14159265358979 / 180; dx = sin (a); dy = -cos (a); if (fabs (dx) < 1e-9) dx = 0; if (fabs (dy) < 1e-9) dy = 0; }
	// The cell one step away at an angle off the heading (0 ahead, -90 left, 90 right)
	char look (double off, int *oc = 0, int *orow = 0) const
	{
		double dx, dy; dir (h + off, dx, dy);
		int c = (int) floor (x + dx + 0.5), r = (int) floor (y + dy + 0.5);
		if (oc) *oc = c;
		if (orow) *orow = r;
		return at (c, r);
	}
	bool blocks (char k) const { return k == '#' || k == ' ' || (k == 'D' && keys == 0); }
	bool won () const
	{
		if (hasGoal && (cx () != gx || cy () != gy)) return false;
		if (coins < coinsTotal) return false;
		for (int r = 0; L && r < L->h; r++) for (int c = 0; c < L->w; c++) if (L->map[r][c] == 'p' && !painted[r][c]) return false;
		return true;
	}
};

// ---- the record of a run ---------------------------------------------------------------------------------------------------
enum { EV_LINE, EV_MOVE, EV_TURN, EV_PEN, EV_COLOR, EV_PICK, EV_DOOR, EV_BUMP, EV_PRINT };
struct Ev
{
	char kind; int line;
	float a, b, c, d;				// MOVE: from (a, b) to (c, d); TURN: from a to b; BUMP: the turtle at (a, b) towards (c, d)
	int i;						// PEN: down; COLOR: the colour; PICK / DOOR: the cell (x + y * 256); PRINT: the text's place in Run::out
};
enum { R_WON, R_LOST, R_ERROR, R_COMPILE, R_ENDLESS };
struct Run
{
	Arr<Ev> ev;
	int result;					// R_*
	int errLine; char msg[240];			// an error: its line, a sentence for the player
	char *out; int outLen, outCap;			// what the program printed
	int count, stars;				// the instructions; the stars won (0: not won)
	Run () : result (R_LOST), errLine (0), out (0), outLen (0), outCap (0), count (0), stars (0) { msg[0] = 0; }
	~Run () { free (out); }
	void reset () { ev.clear (); result = R_LOST; errLine = 0; msg[0] = 0; outLen = 0; if (out) out[0] = 0; count = 0; stars = 0; }
	int addOut (const char *s, int n)
	{
		if (outLen + n + 1 > outCap) { outCap = (outLen + n + 1) * 2 + 256; out = (char *) realloc (out, outCap); }
		int at = outLen; memcpy (out + outLen, s, n); outLen += n; out[outLen] = 0;
		return at;
	}
};

// One event applied to a world (the recorder's and the player's alike)
static inline void apply (World &w, const Ev &e)
{
	switch (e.kind)
	{
	case EV_MOVE:
		if (w.pen) { Seg s = { e.a, e.b, e.c, e.d, w.color }; w.segs.push (s); }
		w.x = e.c; w.y = e.d; w.paintHere (); break;
	case EV_TURN: w.h = e.b; break;
	case EV_PEN: w.pen = e.i != 0; if (w.pen) w.paintHere (); break;
	case EV_COLOR: w.color = e.i; break;
	case EV_PICK: { int c = e.i & 255, r = e.i >> 8; char k = w.at (c, r); if (k == 'k') w.keys++; if (k == 'c') w.coins++; if (w.L && c < w.L->w && r < w.L->h) w.cell[r][c] = '.'; break; }
	case EV_DOOR: { int c = e.i & 255, r = e.i >> 8; if (w.keys > 0) w.keys--; if (w.L && c < w.L->w && r < w.L->h) w.cell[r][c] = '.'; break; }
	}
}

// The texts the player reads (English, French)
static inline const char *T (int lang, const char *en, const char *fr) { return lang == LANG_FR ? fr : en; }

// A compiler's or the VM's message made friendlier (the turtle's own errors are already)
static inline void friendly (int lang, int line, const char *msg, char *out, int cap)
{
	struct M { const char *in; const char *en; const char *fr; };
	static const M MAP[] = {
		{ "Syntax error", "I do not understand line %d.", "Je ne comprends pas la ligne %d." },
		{ "FOR without NEXT", "Line %d: this FOR has no NEXT to close it.", "Ligne %d : ce POUR n'a pas de SUIVANT pour le fermer." },
		{ "REPEAT without END REPEAT", "Line %d: this REPEAT has no END REPEAT to close it.", "Ligne %d : ce REPETE n'a pas de FIN REPETE pour le fermer." },
		{ "IF without END IF", "Line %d: this IF has no END IF to close it.", "Ligne %d : ce SI n'a pas de FIN SI pour le fermer." },
		{ "WHILE without WEND", "Line %d: this WHILE has no WEND to close it.", "Ligne %d : ce TANTQUE n'a pas de FINTANTQUE pour le fermer." },
		{ "SUB without END SUB", "Line %d: this SUB has no END SUB.", "Ligne %d : cette PROCEDURE n'a pas de FIN PROCEDURE." },
		{ "Wrong number of arguments", "Line %d: not the right number of values for this word.", "Ligne %d : pas le bon nombre de valeurs pour ce mot." },
		{ "Type mismatch", "Line %d: a number was expected here.", "Ligne %d : il fallait un nombre ici." },
		{ "Division by zero", "Line %d: a division by zero.", "Ligne %d : une division par zéro." },
		{ 0, 0, 0 } };
	for (int i = 0; MAP[i].in; i++)
		if (!strncmp (msg, MAP[i].in, strlen (MAP[i].in))) { snprintf (out, (size_t) cap, lang == LANG_FR ? MAP[i].fr : MAP[i].en, line); return; }
	if (!strncmp (msg, "Sub or function not defined", 27) || !strncmp (msg, "Label not defined", 17))
	{
		snprintf (out, (size_t) cap, T (lang, "Line %d: I do not know this word (%s).", "Ligne %d : je ne connais pas ce mot (%s)."), line, msg);
		return;
	}
	snprintf (out, (size_t) cap, T (lang, "Line %d: %s", "Ligne %d : %s"), line, msg);
}

// ---- the run: a bas::Host that is the turtle -----------------------------------------------------------------------------------------
enum { MAX_EVENTS = 40000, MAX_POLLS = 3000 };	// (a run stopped past them: about 12 M instructions)
struct Recorder : bas::Host
{
	World w; Run *R; int lang; int line; int polls; bool endless;
	Recorder () : R (0), lang (0), line (0), polls (0), endless (false) {}
	void push (Ev e) { e.line = line; if (R->ev.n < MAX_EVENTS) R->ev.push (e); apply (w, e); }
	bool full () { if (R->ev.n < MAX_EVENTS) return false; endless = true; return true; }
	void out (const char *s, int n) override
	{
		Ev e; memset (&e, 0, sizeof e); e.kind = EV_PRINT; e.i = R->addOut (s, n);
		R->outLen++;				// (each text ends with its 0: addOut left room for it)
		push (e);
	}
	int  inputLine (char *, int) override { return -1; }
	bool poll () override { if (++polls > MAX_POLLS) { endless = true; return false; } return true; }
	bool onStatement (int ln) override
	{
		if (full ()) return false;
		line = ln; Ev e; memset (&e, 0, sizeof e); e.kind = EV_LINE; push (e);
		return true;
	}
	bool fail (char *why, int cap, const char *en, const char *fr) { snprintf (why, (size_t) cap, "%s", T (lang, en, fr)); return false; }
	bool move (double dist, char *why, int cap)
	{
		double dx, dy; World::dir (w.h, dx, dy);
		double sign = dist < 0 ? -1 : 1, left = fabs (dist);
		while (left > 1e-9)
		{
			if (full ()) return false;
			double st = left >= 1 ? 1 : left; left -= st;
			double nx = w.x + sign * dx * st, ny = w.y + sign * dy * st;
			int c = (int) floor (nx + 0.5), r = (int) floor (ny + 0.5);
			char k = w.at (c, r);
			bool grid = !(w.L && w.L->draw);
			if (grid && (c != w.cx () || r != w.cy ()))
			{
				if (k == 'D' && w.keys > 0) { Ev e; memset (&e, 0, sizeof e); e.kind = EV_DOOR; e.i = c + r * 256; push (e); }
				else if (w.blocks (k))
				{
					Ev e; memset (&e, 0, sizeof e); e.kind = EV_BUMP; e.a = (float) w.x; e.b = (float) w.y; e.c = (float) nx; e.d = (float) ny; push (e);
					if (k == 'D') return fail (why, cap, "The door is locked: the turtle needs a key.", "La porte est fermée : la tortue a besoin d'une clé.");
					if (k == ' ' || c < 0 || r < 0 || !w.L || c >= w.L->w || r >= w.L->h) return fail (why, cap, "The turtle almost fell off the board!", "La tortue a failli tomber du plateau !");
					return fail (why, cap, "Bump! The turtle hit a wall.", "Bong ! La tortue s'est cognée dans un mur.");
				}
			}
			else if (!grid && (nx < -0.5 || ny < -0.5 || nx > w.L->w - 0.5 || ny > w.L->h - 0.5))
			{
				Ev e; memset (&e, 0, sizeof e); e.kind = EV_BUMP; e.a = (float) w.x; e.b = (float) w.y; e.c = (float) nx; e.d = (float) ny; push (e);
				return fail (why, cap, "The turtle went off the page!", "La tortue est sortie de la page !");
			}
			Ev e; memset (&e, 0, sizeof e); e.kind = EV_MOVE; e.a = (float) w.x; e.b = (float) w.y; e.c = (float) nx; e.d = (float) ny; push (e);
		}
		return true;
	}
	bool turn (double deg)
	{
		if (full ()) return false;
		double h = fmod (w.h + deg, 360); if (h < 0) h += 360;
		if (fabs (h - floor (h + 0.5)) < 1e-6) h = floor (h + 0.5);
		if (h >= 360) h -= 360;
		Ev e; memset (&e, 0, sizeof e); e.kind = EV_TURN; e.a = (float) w.h; e.b = (float) h; push (e);
		return true;
	}
	bool ext (int id, const bas::ExtVal *a, int argc, bas::ExtVal *r, char *why, int cap) override
	{
		if (w.L && !w.L->knows (id))
		{
			snprintf (why, (size_t) cap, T (lang, "This level does not know %s yet.", "Ce niveau ne connaît pas encore %s."), word_name (id, lang));
			return false;
		}
		double n = argc > 0 ? a[0].n : 0;
		auto yes = [&] (bool b) { r->str = false; r->n = b ? -1 : 0; return true; };
		switch (id)
		{
		case W_FORWARD: return move (argc ? n : 1, why, cap);
		case W_BACK: return move (-(argc ? n : 1), why, cap);
		case W_LEFT: return turn (-(argc ? n : 90));
		case W_RIGHT: return turn (argc ? n : 90);
		case W_PENUP: case W_PENDOWN: { Ev e; memset (&e, 0, sizeof e); e.kind = EV_PEN; e.i = id == W_PENDOWN; push (e); return true; }
		case W_COLOR:
			if (n < 0 || n > 15) return fail (why, cap, "COLOR takes a number from 0 to 15.", "COULEUR prend un nombre de 0 à 15.");
			{ Ev e; memset (&e, 0, sizeof e); e.kind = EV_COLOR; e.i = (int) n; push (e); return true; }
		case W_PICK:
		{
			char k = w.at (w.cx (), w.cy ());
			if (k != 'k' && k != 'c') return fail (why, cap, "There is nothing to pick up here.", "Il n'y a rien à ramasser ici.");
			Ev e; memset (&e, 0, sizeof e); e.kind = EV_PICK; e.i = w.cx () + w.cy () * 256; push (e);
			return true;
		}
		case W_WALL: return yes (w.blocks (w.look (0)));
		case W_WALLLEFT: return yes (w.blocks (w.look (-90)));
		case W_WALLRIGHT: return yes (w.blocks (w.look (90)));
		case W_FRONT:
		{
			int c, rr; char k = w.look (0, &c, &rr);
			r->str = false;
			r->n = k == 'D' ? 4 : w.blocks (k) ? 1 : (k == 'k' || k == 'c') ? 2 : (w.hasGoal && c == w.gx && rr == w.gy) ? 3 : 0;
			return true;
		}
		case W_ONGOAL: return yes (w.hasGoal && w.cx () == w.gx && w.cy () == w.gy);
		case W_ITEM: { char k = w.at (w.cx (), w.cy ()); return yes (k == 'k' || k == 'c'); }
		case W_KEYS: r->str = false; r->n = w.keys; return true;
		case W_HEADING: r->str = false; r->n = w.h; return true;
		}
		return fail (why, cap, "Unknown word.", "Mot inconnu.");
	}
};

// The pen's lines of a world sampled into points (every 0.1 square): what a drawing level compares
static inline void sample (const Arr<Seg> &segs, Arr<float> &pts)
{
	pts.clear ();
	for (int i = 0; i < segs.n; i++)
	{
		const Seg &s = segs[i];
		double dx = s.x2 - s.x1, dy = s.y2 - s.y1, len = sqrt (dx * dx + dy * dy);
		int k = (int) (len / 0.1) + 1;
		for (int j = 0; j <= k; j++) { pts.push ((float) (s.x1 + dx * j / k)); pts.push ((float) (s.y1 + dy * j / k)); }
	}
}
static inline double seg_dist (const Seg &s, double px, double py)
{
	double dx = s.x2 - s.x1, dy = s.y2 - s.y1, l2 = dx * dx + dy * dy;
	double t = l2 > 0 ? ((px - s.x1) * dx + (py - s.y1) * dy) / l2 : 0;
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	double ex = s.x1 + t * dx - px, ey = s.y1 + t * dy - py;
	return sqrt (ex * ex + ey * ey);
}
// Every point of a near some line of b
static inline bool covered (const Arr<float> &a, const Arr<Seg> &b)
{
	for (int i = 0; i + 1 < a.n; i += 2)
	{
		bool near = false;
		for (int j = 0; j < b.n && !near; j++) if (seg_dist (b[j], a[i], a[i + 1]) < 0.15) near = true;
		if (!near) return false;
	}
	return true;
}
static inline bool same_drawing (const Arr<Seg> &mine, const Arr<Seg> &target)
{
	if (!target.n) return true;
	Arr<float> pm, pt;
	sample (mine, pm); sample (target, pt);
	return pm.n && covered (pm, target) && covered (pt, mine);
}

// Runs a program on a level: R gets the record, the result, the stars. target: the drawing to reproduce (a
// drawing level: the solution's -- target_of), 0 otherwise.
static inline void run_program (const Level &L, const char *src, int lang, Run &R, const Arr<Seg> *target)
{
	R.reset ();
	set_language (lang);
	R.count = count_instructions (src);
	bas::Error err;
	bas::Program *p = bas::compile (src, &err);
	if (!p) { R.result = R_COMPILE; R.errLine = err.line; friendly (lang, err.line, err.msg, R.msg, sizeof R.msg); return; }
	Recorder *h = new Recorder;
	h->R = &R; h->lang = lang; h->lineHook = true; h->managed = true;
	h->w.reset (L);
	int rc = bas::run (p, *h, &err);
	bas::destroy (p);
	if (h->endless)
	{
		R.result = R_ENDLESS; R.errLine = h->line;
		snprintf (R.msg, sizeof R.msg, "%s", T (lang, "The turtle is tired: the program does not seem to end (a loop that never stops?).",
			"La tortue est fatiguée : le programme ne semble pas finir (une boucle qui ne s'arrête jamais ?)."));
	}
	else if (rc != 0)
	{
		R.result = R_ERROR; R.errLine = err.line;
		char m[200];
		snprintf (m, sizeof m, T (lang, "Line %d: %s", "Ligne %d : %s"), err.line, err.msg);
		bool own = false;			// (the turtle's own sentences: as they are, with the line)
		static const char *const OWN[] = { "Bump", "Bong", "The door", "La porte", "There is", "Il n'y", "This level", "Ce niveau", "The turtle", "La tortue", "COLOR", "COULEUR", 0 };
		for (int k = 0; OWN[k]; k++) if (!strncmp (err.msg, OWN[k], strlen (OWN[k]))) own = true;
		if (own) snprintf (R.msg, sizeof R.msg, "%s", m);
		else friendly (lang, err.line, err.msg, R.msg, sizeof R.msg);
	}
	else
	{
		bool ok = h->w.won ();
		if (ok && L.draw && target) ok = same_drawing (h->w.segs, *target);
		R.result = ok ? R_WON : R_LOST;
		if (ok) R.stars = !L.par3 || R.count <= L.par3 ? 3 : R.count <= L.par2 ? 2 : 1;
		else if (L.draw) snprintf (R.msg, sizeof R.msg, "%s", T (lang, "Not quite the same figure: compare with the grey one.", "Pas tout à fait la même figure : compare avec la grise."));
		else if (h->w.coins < h->w.coinsTotal) snprintf (R.msg, sizeof R.msg, T (lang, "The program ended, but %d coin(s) are still on the floor.", "Le programme est fini, mais il reste %d pièce(s) par terre."), h->w.coinsTotal - h->w.coins);
		else if (h->w.hasGoal && (h->w.cx () != h->w.gx || h->w.cy () != h->w.gy)) snprintf (R.msg, sizeof R.msg, "%s", T (lang, "The program ended before the turtle reached the flag.", "Le programme est fini avant que la tortue arrive au drapeau."));
		else snprintf (R.msg, sizeof R.msg, "%s", T (lang, "Some tiles are not painted yet.", "Il reste des cases à peindre."));
	}
	delete h;
}
// A drawing level's figure: its solution's lines (false: no solution, or it fails)
static inline bool target_of (const Level &L, Arr<Seg> &segs)
{
	segs.clear ();
	if (!L.solution) return false;
	Run R;
	run_program (L, L.solution, LANG_EN, R, 0);
	if (R.result != R_WON) return false;
	World w; w.reset (L);
	for (int i = 0; i < R.ev.n; i++) apply (w, R.ev[i]);
	segs.copyFrom (w.segs);
	return true;
}

} // namespace turtle

#endif
