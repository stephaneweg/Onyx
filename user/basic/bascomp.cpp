//
// basic/bascomp.cpp -- the Onyx BASIC compiler: source text -> bytecode (basint.h).
//
// One pass over a token array (the lexer runs first), after a quick pre-scan that collects
// every SUB / FUNCTION header (so a SUB can be called before its definition, QBasic style).
// SUB / FUNCTION bodies are emitted in place with a jump over them. Variables are typed by
// their suffix ($ = string, else number) or by DIM ... AS STRING; module-level variables are
// globals, a procedure's own are locals (DIM SHARED / SHARED reach the globals). Scalar
// arguments that are plain variables are passed BY REFERENCE (QBasic's rule); anything
// else (an expression, a parenthesised variable) by value; arrays always by reference.
//
#include "basic/basint.h"
#include "basic/basnum.h"
#define BASFONT_TABLES_ONLY
#include "basic/basfont.h"

// Text inside BASIC is code page 437 (QBasic's: CHR$(201) = a frame corner); the source is
// Latin-1 like the rest of Onyx, so string literals and DATA are translated (\xE9 -> 130).
static inline char to437 (char c) { return (char) basLatin1To437[(unsigned char) c]; }

namespace bas {

enum { T_EOF, T_NL, T_NUM, T_STR, T_ID, T_OP, T_DATA };
enum { O_LE = 256, O_GE, O_NE };

struct Tok { int t; int line; double num; char id[48]; char *s; int sl; int op; bool dbl; };

// Words that cannot be variables / labels.
static const char *const KEYWORDS[] = {
	"AND", "AS", "CALL", "CASE", "CLOSE", "CLS", "COLOR", "CONST", "DATA", "DECLARE", "DEF", "DIM", "DO",
	"ELSE", "ELSEIF", "END", "EQV", "ERASE", "EXIT", "FOR", "FUNCTION", "GOSUB", "GOTO", "IF", "IMP", "INPUT",
	"IS", "LET", "LINE", "LOCATE", "LOOP", "MOD", "NEXT", "NOT", "ON", "OPEN", "OPTION", "OR", "PRINT",
	"READ", "REDIM", "REM", "RESTORE", "RETURN", "SELECT", "SHARED", "STATIC", "STEP", "SUB", "SWAP", "THEN",
	"TO", "UNTIL", "WEND", "WHILE", "XOR", "SCREEN", "PSET", "PRESET", "CIRCLE", "SLEEP", "RANDOMIZE", "BEEP",
	"WINDOW", "LPRINT", "WRITE", "SYSTEM", "STOP", "KILL", "NAME", "MKDIR", "RMDIR", "APPEND", "OUTPUT",
	"BINARY", "RANDOM", "USING", "TAB", "SPC",
	"TYPE", "RESUME", "FIELD", "LSET", "RSET", "COMMON", "CHAIN", "RUN", "CLEAR", "TRON", "TROFF", "KEY",
	"PAINT", "DRAW", "VIEW", "PALETTE", "PCOPY", "GET", "PUT", "RESET", "FILES", "CHDIR", "SHELL", "ENVIRON",
	"ERROR", "ACCESS", "LOCK", "UNLOCK", "DEFSTR", "FULLSCREEN", 0 };

struct BFn { const char *name; int id; int ret; const char *args; };
static const BFn BFNS[] = {
	{ "LEN", B_LEN, TY_NUM, "S" }, { "ASC", B_ASC, TY_NUM, "S" }, { "CHR$", B_CHR, TY_STR, "N" },
	{ "LEFT$", B_LEFT, TY_STR, "SN" }, { "RIGHT$", B_RIGHT, TY_STR, "SN" }, { "MID$", B_MID, TY_STR, "SN[N" },
	{ "INSTR", B_INSTR, TY_NUM, "*" }, { "UCASE$", B_UCASE, TY_STR, "S" }, { "LCASE$", B_LCASE, TY_STR, "S" },
	{ "LTRIM$", B_LTRIM, TY_STR, "S" }, { "RTRIM$", B_RTRIM, TY_STR, "S" }, { "TRIM$", B_TRIM, TY_STR, "S" },
	{ "STR$", B_STR, TY_STR, "N" }, { "VAL", B_VAL, TY_NUM, "S" }, { "SPACE$", B_SPACE, TY_STR, "N" },
	{ "STRING$", B_STRING, TY_STR, "N?" }, { "HEX$", B_HEX, TY_STR, "N" }, { "OCT$", B_OCT, TY_STR, "N" },
	{ "ABS", B_ABS, TY_NUM, "N" }, { "SGN", B_SGN, TY_NUM, "N" }, { "INT", B_INT, TY_NUM, "N" },
	{ "FIX", B_FIX, TY_NUM, "N" }, { "SQR", B_SQR, TY_NUM, "N" }, { "SIN", B_SIN, TY_NUM, "N" },
	{ "COS", B_COS, TY_NUM, "N" }, { "TAN", B_TAN, TY_NUM, "N" }, { "ATN", B_ATN, TY_NUM, "N" },
	{ "EXP", B_EXP, TY_NUM, "N" }, { "LOG", B_LOG, TY_NUM, "N" }, { "RND", B_RND, TY_NUM, "[N" },
	{ "CINT", B_CINT, TY_NUM, "N" }, { "CLNG", B_CLNG, TY_NUM, "N" }, { "CDBL", B_CDBL, TY_NUM, "N" },
	{ "CSNG", B_CDBL, TY_NUM, "N" }, { "MIN", B_MIN, TY_NUM, "NN" }, { "MAX", B_MAX, TY_NUM, "NN" },
	{ "TIMER", B_TIMER, TY_NUM, "" }, { "DATE$", B_DATE, TY_STR, "" }, { "TIME$", B_TIME, TY_STR, "" },
	{ "INKEY$", B_INKEY, TY_STR, "" }, { "COMMAND$", B_COMMAND, TY_STR, "" }, { "POS", B_POS, TY_NUM, "[N" },
	{ "CSRLIN", B_CSRLIN, TY_NUM, "" }, { "POINT", B_POINT, TY_NUM, "NN" }, { "RGB", B_RGB, TY_NUM, "NNN" },
	{ "EOF", B_EOF, TY_NUM, "N" }, { "LOF", B_LOF, TY_NUM, "N" }, { "FREEFILE", B_FREEFILE, TY_NUM, "" },
	{ "FILEEXISTS", B_FILEEXISTS, TY_NUM, "S" }, { "DIR$", B_DIR, TY_STR, "S[N" },
	{ "BUTTON", B_BUTTON, TY_NUM, "NNNNS" }, { "LABEL", B_LABEL, TY_NUM, "NNNNS" },
	{ "TEXTBOX", B_TEXTBOX, TY_NUM, "NNNN[S" }, { "CHECKBOX", B_CHECKBOX, TY_NUM, "NNNNS[N" },
	{ "LISTBOX", B_LISTBOX, TY_NUM, "NNNN[S" }, { "DROPDOWN", B_DROPDOWN, TY_NUM, "NNNNS" },
	{ "PROGRESS", B_PROGRESS, TY_NUM, "NNNN" }, { "SLIDER", B_SLIDER, TY_NUM, "NNNN[N" },
	{ "GETTEXT$", B_GETTEXT, TY_STR, "N" }, { "VALUE", B_VALUE, TY_NUM, "N" }, { "EVENT", B_EVENT, TY_NUM, "" },
	{ "WAITEVENT", B_WAITEVENT, TY_NUM, "" }, { "MSGBOX", B_MSGBOX, TY_NUM, "SS[N" },
	{ "CLIPBOARD$", B_CLIPBOARD, TY_STR, "" }, { "OPENFILE$", B_OPENFILE, TY_STR, "[S" },
	{ "SAVEFILE$", B_SAVEFILE, TY_STR, "[SS" }, { "MOUSEX", B_MOUSEX, TY_NUM, "" },
	{ "MOUSEY", B_MOUSEY, TY_NUM, "" }, { "MOUSEB", B_MOUSEB, TY_NUM, "" }, { "TICKS", B_TICKS, TY_NUM, "" },
	{ "ERR", B_ERR, TY_NUM, "" }, { "ERL", B_ERL, TY_NUM, "" },
	{ "MKI$", B_MKI, TY_STR, "N" }, { "MKL$", B_MKL, TY_STR, "N" }, { "MKS$", B_MKS, TY_STR, "N" }, { "MKD$", B_MKD, TY_STR, "N" },
	{ "CVI", B_CVI, TY_NUM, "S" }, { "CVL", B_CVL, TY_NUM, "S" }, { "CVS", B_CVS, TY_NUM, "S" }, { "CVD", B_CVD, TY_NUM, "S" },
	{ "INPUT$", B_INPUTS, TY_STR, "N[N" }, { "SEEK", B_SEEK, TY_NUM, "N" }, { "LOC", B_LOC, TY_NUM, "N" },
	{ "ENVIRON$", B_ENVIRON, TY_STR, "*" }, { "FRE", B_FRE, TY_NUM, "*" }, { "PMAP", B_PMAP, TY_NUM, "NN" },
	{ "SCREEN", B_SCREEN, TY_NUM, "NN[N" }, { "KEYDOWN", B_KEYDOWN, TY_NUM, "S" },
	{ "PLAY", B_PLAYN, TY_NUM, "N" },
	{ 0, 0, 0, 0 } };

// Block terminators (what ends a statement block).
enum
{
	TM_ENDIF = 1, TM_ELSE = 2, TM_ELSEIF = 4, TM_LOOP = 8, TM_WEND = 16, TM_NEXT = 32, TM_CASE = 64,
	TM_ENDSELECT = 128, TM_ENDSUB = 256, TM_ENDFUNC = 512, TM_ENDDEF = 1024
};

enum { LOOP_FOR = 1, LOOP_DO, LOOP_WHILE };

class Compiler
{
public:
	Program *P; Error *err; bool failed;
	Vec<Tok> toks; int pos;

	struct Sym { char key[52]; int slot; int ty; bool shared; bool global; int nt; int flen; };
	Vec<Sym> gsyms, lsyms; int nlocals;
	struct Const { char name[48]; int ty; double n; int sidx; bool dbl; };
	Vec<Const> consts;
	struct PDecl { char name[48]; bool isFunc; int retTy; int retNt; int np; char pname[16][48]; int pty[16]; int pnt[16]; bool parr[16]; bool defFn; };
	// A type spec (AS ...): the value type, numeric sub-type, fixed string length.
	struct TSpec { int ty; int nt; int flen; };
	struct FName { char name[48]; int ty; int nt; int flen; };
	Vec<FName> fnames;				// parallel to P->fields
	TSpec deftype[26];				// DEFINT / DEFLNG / DEFSNG / DEFDBL / DEFSTR
	bool dblSeen;					// the expression being compiled involves a DOUBLE
	Vec<PDecl> pdecls;
	int curProc;					// -1 = the main module
	struct Label { char name[48]; int pc; int proc; int dataIdx; };
	Vec<Label> labels;
	struct Fix { int at; char name[48]; int proc; int line; bool data; };
	Vec<Fix> fixes;
	struct Loop { int kind; int exits[64]; int nexit; };
	Loop loops[32]; int nloops;
	bool base1; int tmpN; int lastLine;

	Compiler () : P (0), err (0), failed (false), pos (0), nlocals (0), curProc (-1), nloops (0), base1 (false), tmpN (0), lastLine (-1)
	{ dblSeen = false; resetDeftypes (); }
	void resetDeftypes () { for (int i = 0; i < 26; i++) { deftype[i].ty = TY_NUM; deftype[i].nt = NT_SNG; deftype[i].flen = 0; } }
	~Compiler () { for (int i = 0; i < toks.n; i++) delete [] toks[i].s; }

	// ---- errors ---------------------------------------------------------------------------
	void fail (const char *msg, int line = -1)
	{
		if (failed) return;
		failed = true;
		err->line = line >= 0 ? line : (pos < toks.n ? toks[pos].line : (toks.n ? toks[toks.n - 1].line : 0));
		bscpy (err->msg, msg, sizeof err->msg);
	}
	void fail2 (const char *a, const char *b)
	{
		char m[120]; int n = 0;
		for (int i = 0; a[i] && n < 118; i++) m[n++] = a[i];
		for (int i = 0; b[i] && n < 118; i++) m[n++] = b[i];
		m[n] = 0; fail (m);
	}

	// ---- lexer -----------------------------------------------------------------------------
	void addTok (int t, int line) { Tok k; k.t = t; k.line = line; k.num = 0; k.id[0] = 0; k.s = 0; k.sl = 0; k.op = 0; k.dbl = false; toks.push (k); }
	static bool idStart (char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
	static bool idChar (char c) { return idStart (c) || (c >= '0' && c <= '9') || c == '.'; }

	void lex (const char *s)
	{
		int line = 1, i = 0;
		while (s[i] && !failed)
		{
			char c = s[i];
			if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
			if (c == '\n') { addTok (T_NL, line); line++; i++; continue; }
			if (c == '\'') { while (s[i] && s[i] != '\n') i++; continue; }
			if ((c >= '0' && c <= '9') || (c == '.' && s[i + 1] >= '0' && s[i + 1] <= '9') || (c == '&' && (bup (s[i + 1]) == 'H' || bup (s[i + 1]) == 'O' || bup (s[i + 1]) == 'B')))
			{
				int used = 0; double v = parseNum (s + i, &used);
				if (used == 0) used = 1;
				int sig = 0; bool lead = true;		// > 7 significant digits = a DOUBLE literal
				for (int j = 0; j < used; j++)
				{
					char d = s[i + j];
					if (d == 'E' || d == 'e' || d == 'D' || d == 'd') break;
					if (d >= '0' && d <= '9') { if (d != '0') lead = false; if (!lead) sig++; }
				}
				bool dbl = sig > 7 || (c != '&' && (s[i + used - 1] == 'D' || s[i + used - 1] == 'd' || s[i + used] == '#'));
				for (int j = 0; j < used; j++) if (s[i + j] == 'D' || s[i + j] == 'd') dbl = c != '&';
				i += used;
				while (s[i] == '!' || s[i] == '#' || s[i] == '%' || s[i] == '&') i++;	// type suffix
				addTok (T_NUM, line); toks[toks.n - 1].num = v; toks[toks.n - 1].dbl = dbl;
				continue;
			}
			if (c == '"')
			{
				int st = ++i; while (s[i] && s[i] != '"' && s[i] != '\n') i++;
				addTok (T_STR, line);
				Tok &k = toks[toks.n - 1];
				k.sl = i - st; k.s = new char[k.sl + 1];
				for (int j = 0; j < k.sl; j++) k.s[j] = to437 (s[st + j]);	// the editor writes Latin-1
				k.s[k.sl] = 0;
				if (s[i] == '"') i++;
				continue;
			}
			if (idStart (c))
			{
				char id[48]; int n = 0;
				while (idChar (s[i])) { if (n < 46) id[n++] = bup (s[i]); i++; }
				if (s[i] == '$' || s[i] == '%' || s[i] == '&' || s[i] == '!' || s[i] == '#') { if (n < 47) id[n++] = s[i]; i++; }
				id[n] = 0;
				if (bseq (id, "REM")) { while (s[i] && s[i] != '\n') i++; continue; }
				addTok (T_ID, line); bscpy (toks[toks.n - 1].id, id, 48);
				if (bseq (id, "DATA"))				// the rest of the line, raw
				{
					while (s[i] == ' ' || s[i] == '\t') i++;
					int st = i; while (s[i] && s[i] != '\n') i++;
					int e = i; while (e > st && (s[e - 1] == ' ' || s[e - 1] == '\r' || s[e - 1] == '\t')) e--;
					addTok (T_DATA, line);
					Tok &k = toks[toks.n - 1];
					k.sl = e - st; k.s = new char[k.sl + 1];
					for (int j = 0; j < k.sl; j++) k.s[j] = to437 (s[st + j]);
					k.s[k.sl] = 0;
				}
				continue;
			}
			int op = c;
			if (c == '.' && idStart (s[i + 1]) && i > 0 && s[i - 1] == ')') { i++; addTok (T_OP, line); toks[toks.n - 1].op = '.'; continue; }	// a(i).field
			if (c == '<' && s[i + 1] == '=') { op = O_LE; i++; }
			else if (c == '<' && s[i + 1] == '>') { op = O_NE; i++; }
			else if (c == '>' && s[i + 1] == '=') { op = O_GE; i++; }
			else if (c == '=' && s[i + 1] == '<') { op = O_LE; i++; }
			else if (c == '=' && s[i + 1] == '>') { op = O_GE; i++; }
			if (!(c == '+' || c == '-' || c == '*' || c == '/' || c == '\\' || c == '^' || c == '=' || c == '<' || c == '>' ||
			      c == '(' || c == ')' || c == ',' || c == ';' || c == ':' || c == '#' || c == '?'))
			{
				char m[40] = "Unexpected character: ";
				int n = bslen (m); m[n++] = c; m[n] = 0;
				fail (m, line); return;
			}
			i++;
			addTok (T_OP, line); toks[toks.n - 1].op = op;
		}
		addTok (T_NL, line);
		addTok (T_EOF, line);
	}

	// ---- token helpers ---------------------------------------------------------------------
	Tok &cur () { return toks[pos]; }
	Tok &peek (int k = 1) { int p = pos + k; return toks[p < toks.n ? p : toks.n - 1]; }
	void next () { if (pos < toks.n - 1) pos++; }
	bool isOp (int op) { return cur ().t == T_OP && cur ().op == op; }
	bool isKw (const char *k) { return cur ().t == T_ID && bseq (cur ().id, k); }
	bool peekKw (int k, const char *w) { return peek (k).t == T_ID && bseq (peek (k).id, w); }
	bool acceptOp (int op) { if (isOp (op)) { next (); return true; } return false; }
	bool acceptKw (const char *k) { if (isKw (k)) { next (); return true; } return false; }
	void expectOp (int op)
	{
		if (acceptOp (op)) return;
		char m[24] = "Expected ' '"; m[10] = (char) (op == O_LE ? '<' : op); fail (m);
	}
	void expectKw (const char *k) { if (!acceptKw (k)) fail2 ("Expected ", k); }
	bool endOfStmt () { return cur ().t == T_NL || cur ().t == T_EOF || isOp (':') || isKw ("ELSE"); }
	static bool isKeyword (const char *id)
	{
		for (int i = 0; KEYWORDS[i]; i++) if (bseq (KEYWORDS[i], id)) return true;
		return false;
	}

	// ---- emission ----------------------------------------------------------------------------
	int pc () { return P->code.n; }
	void emit (int a) { P->code.push (a); }
	void emit2 (int a, int b) { emit (a); emit (b); }
	void emit3 (int a, int b, int c) { emit (a); emit (b); emit (c); }
	int  emitJump (int op) { emit (op); emit (0); return pc () - 1; }
	void patch (int at, int target) { P->code[at] = target; }
	int  numConst (double v)
	{
		for (int i = 0; i < P->nums.n; i++) if (P->nums[i] == v) return i;
		P->nums.push (v); return P->nums.n - 1;
	}
	int  strConst (const char *s, int len)
	{
		char *c = new char[len + 1]; bmcpy (c, s, len); c[len] = 0;
		P->strs.push (c); P->strl.push (len); return P->strs.n - 1;
	}
	void pushNum (double v) { emit2 (OP_NUM, numConst (v)); }
	void markLine ()
	{
		int l = cur ().line;
		if (l != lastLine) { LineMark m; m.pc = pc (); m.line = l; P->lines.push (m); lastLine = l; }
	}

	// ---- symbols --------------------------------------------------------------------------------
	// The type a name gets from its suffix ($ % & ! #) or, without one, from DEFtype.
	TSpec nameSpec (const char *name)
	{
		TSpec t; t.ty = TY_NUM; t.nt = NT_SNG; t.flen = 0;
		int n = bslen (name);
		char c = n ? name[n - 1] : 0;
		if (c == '$') t.ty = TY_STR;
		else if (c == '%') t.nt = NT_INT;
		else if (c == '&') t.nt = NT_LNG;
		else if (c == '!') t.nt = NT_SNG;
		else if (c == '#') t.nt = NT_DBL;
		else if (name[0] >= 'A' && name[0] <= 'Z') t = deftype[name[0] - 'A'];
		return t;
	}
	int suffixTy (const char *name) { return nameSpec (name).ty; }
	static void makeKey (char *key, const char *name, bool arr)
	{
		bscpy (key, name, 48);
		if (arr) { int n = bslen (key); key[n] = '('; key[n + 1] = ')'; key[n + 2] = 0; }
	}
	static bool keyIsArr (const char *key) { int n = bslen (key); return n >= 2 && key[n - 1] == ')' && key[n - 2] == '('; }
	static unsigned char slotKind (const Sym &s)
	{
		bool arr = keyIsArr (s.key);
		if (s.ty >= TY_REC) return (unsigned char) (arr ? K_RECARR : K_REC);
		return (unsigned char) ((arr ? 2 : 0) + (s.ty == TY_STR ? 1 : 0));
	}
	static int slotExt (const Sym &s) { return s.ty >= TY_REC ? s.ty - TY_REC : s.flen; }
	int findIn (Vec<Sym> &v, const char *key) { for (int i = 0; i < v.n; i++) if (bseq (v[i].key, key)) return i; return -1; }

	// A variable: global or local slot + type. decl: its declared type (DIM AS), or 0.
	struct Var { bool global; int slot; int ty; int nt; int flen; };
	static Var symVar (const Sym &s, bool global) { Var r; r.global = global; r.slot = s.slot; r.ty = s.ty; r.nt = s.nt; r.flen = s.flen; return r; }
	// Names without a suffix: a variable DIMmed AS a type keeps its bare name; the others
	// get the suffix of their DEFtype ("i" after DEFINT I = "i%", QBasic's rule).
	static bool hasSuffix (const char *name) { int n = bslen (name); char c = n ? name[n - 1] : 0; return c == '$' || c == '%' || c == '&' || c == '!' || c == '#'; }
	void implicitKey (char *key, const char *name, bool arr)
	{
		char nm[52]; bscpy (nm, name, 48);
		if (!hasSuffix (name) && name[0] != '~')
		{
			TSpec t = nameSpec (name);
			int n = bslen (nm);
			nm[n] = t.ty == TY_STR ? '$' : t.nt == NT_INT ? '%' : t.nt == NT_LNG ? '&' : t.nt == NT_DBL ? '#' : '!';
			nm[n + 1] = 0;
		}
		makeKey (key, nm, arr);
	}
	// A symbol of v by its exact (declared) key, else by its implicit one.
	int findSym (Vec<Sym> &v, const char *name, bool arr)
	{
		char key[52]; makeKey (key, name, arr);
		int i = findIn (v, key);
		if (i >= 0 || hasSuffix (name)) return i;
		implicitKey (key, name, arr);
		return findIn (v, key);
	}
	Var var (const char *name, bool arr, const TSpec *decl = 0)
	{
		char key[52];
		if (decl) makeKey (key, name, arr); else implicitKey (key, name, arr);
		TSpec ts = decl ? *decl : nameSpec (name);
		if (curProc >= 0)
		{
			int i = findSym (lsyms, name, arr);
			if (i >= 0) return symVar (lsyms[i], lsyms[i].global);
			int g = findSym (gsyms, name, arr);
			if (g >= 0 && (gsyms[g].shared || pdecls[curProc].defFn)) return symVar (gsyms[g], true);
			if (!pdecls[curProc].defFn)
			{
				Sym s; bscpy (s.key, key, 52); s.slot = nlocals++; s.ty = ts.ty; s.nt = ts.nt; s.flen = ts.flen;
				s.shared = false; s.global = false; lsyms.push (s);
				return symVar (s, false);
			}
		}
		int g = findSym (gsyms, name, arr);
		if (g >= 0) return symVar (gsyms[g], true);
		Sym s; bscpy (s.key, key, 52); s.slot = P->nglobals++; s.ty = ts.ty; s.nt = ts.nt; s.flen = ts.flen;
		s.shared = false; s.global = true; gsyms.push (s);
		return symVar (s, true);
	}
	// An existing variable (no creation).
	bool findVar (const char *name, bool arr, Var &out)
	{
		if (curProc >= 0)
		{
			int i = findSym (lsyms, name, arr);
			if (i >= 0) { out = symVar (lsyms[i], lsyms[i].global); return true; }
			int g = findSym (gsyms, name, arr);
			if (g >= 0 && (gsyms[g].shared || pdecls[curProc].defFn)) { out = symVar (gsyms[g], true); return true; }
			return false;
		}
		int g = findSym (gsyms, name, arr);
		if (g >= 0) { out = symVar (gsyms[g], true); return true; }
		return false;
	}
	bool varExists (const char *name, bool arr) { Var v; return findVar (name, arr, v); }
	Var tempVar (int ty)
	{
		char nm[24] = "~T"; int n = 2, v = ++tmpN; char t[12]; int k = 0;
		while (v) { t[k++] = (char) ('0' + v % 10); v /= 10; }
		while (k) nm[n++] = t[--k];
		if (ty == TY_STR) nm[n++] = '$';
		nm[n] = 0;
		TSpec ts; ts.ty = ty; ts.nt = NT_DBL; ts.flen = 0;
		if (curProc >= 0 && pdecls[curProc].defFn)		// (a DEF FN body has no locals of its own)
		{
			Sym s; bscpy (s.key, nm, 52); s.slot = nlocals++; s.ty = ty; s.nt = NT_DBL; s.flen = 0; s.shared = false; s.global = false;
			lsyms.push (s);
			return symVar (s, false);
		}
		return var (nm, false, &ts);
	}
	void loadVar (const Var &v) { emit2 (v.global ? OP_LDG : OP_LDL, v.slot); if (v.ty == TY_NUM && v.nt == NT_DBL) dblSeen = true; }
	void storeVar (const Var &v) { emit2 (v.global ? OP_STG : OP_STL, v.slot); }
	// Before a store: INTEGER / LONG round and check the range, fixed strings pad / cut.
	void convFor (int ty, int nt, int flen)
	{
		if (ty == TY_NUM && (nt == NT_INT || nt == NT_LNG)) emit2 (OP_CONV, nt);
		else if (ty == TY_STR && flen > 0) emit2 (OP_FIXSTR, flen);
	}

	// ---- user TYPEs ------------------------------------------------------------------------------
	int findType (const char *n) { for (int i = 0; i < P->types.n; i++) if (bseq (P->types[i].name, n)) return i; return -1; }
	int findField (int type, const char *n)
	{
		const TypeInfo &t = P->types[type];
		for (int i = 0; i < t.nf; i++) if (bseq (fnames[t.first + i].name, n)) return t.first + i;
		return -1;
	}
	static int ntSize (int nt) { return nt == NT_INT ? 2 : nt == NT_DBL ? 8 : 4; }
	int specSize (const TSpec &t) { return t.ty >= TY_REC ? P->types[t.ty - TY_REC].size : t.ty == TY_STR ? t.flen : ntSize (t.nt); }
	// AS <type> at pos p of the token array (prescan) or the current token: fills ts.
	bool typeName (const char *w, TSpec &ts)
	{
		ts.ty = TY_NUM; ts.nt = NT_SNG; ts.flen = 0;
		if (bseq (w, "STRING")) { ts.ty = TY_STR; return true; }
		if (bseq (w, "INTEGER")) { ts.nt = NT_INT; return true; }
		if (bseq (w, "LONG")) { ts.nt = NT_LNG; return true; }
		if (bseq (w, "SINGLE")) return true;
		if (bseq (w, "DOUBLE") || bseq (w, "_INTEGER64")) { ts.nt = NT_DBL; return true; }
		int t = findType (w);
		if (t >= 0) { ts.ty = TY_REC + t; return true; }
		return false;
	}
	// TYPE name / field AS type ... / END TYPE -- collected before the code (prescan).
	void prescanTypes ()
	{
		for (int i = 0; i < toks.n && !failed; i++)
		{
			bool atStart = i == 0 || toks[i - 1].t == T_NL || (toks[i - 1].t == T_OP && toks[i - 1].op == ':');
			if (!atStart || toks[i].t != T_ID || !bseq (toks[i].id, "TYPE") || toks[i + 1].t != T_ID) continue;
			if (i > 0 && toks[i - 1].t == T_ID) continue;			// END TYPE
			if (findType (toks[i + 1].id) >= 0) { pos = i + 1; fail2 ("Duplicate definition: ", toks[i + 1].id); return; }
			TypeInfo ti; bscpy (ti.name, toks[i + 1].id, 48); ti.first = P->fields.n; ti.nf = 0; ti.size = 0;
			int p = i + 2;
			for (;;)
			{
				while (toks[p].t == T_NL || (toks[p].t == T_OP && toks[p].op == ':')) p++;
				if (toks[p].t == T_EOF) { pos = i; fail ("TYPE without END TYPE"); return; }
				if (toks[p].t == T_ID && bseq (toks[p].id, "END") && toks[p + 1].t == T_ID && bseq (toks[p + 1].id, "TYPE")) { p += 2; break; }
				if (toks[p].t != T_ID || toks[p + 1].t != T_ID || !bseq (toks[p + 1].id, "AS") || toks[p + 2].t != T_ID)
				{ pos = p; fail ("TYPE field: name AS type expected"); return; }
				FName fnm; bscpy (fnm.name, toks[p].id, 48);
				TSpec ts;
				if (!typeName (toks[p + 2].id, ts)) { pos = p + 2; fail2 ("Unknown type: ", toks[p + 2].id); return; }
				p += 3;
				if (ts.ty == TY_STR && toks[p].t == T_OP && toks[p].op == '*' && toks[p + 1].t == T_NUM) { ts.flen = (int) toks[p + 1].num; p += 2; }
				fnm.ty = ts.ty; fnm.nt = ts.nt; fnm.flen = ts.flen;
				FieldInfo fi;
				fi.kind = ts.ty >= TY_REC ? FK_REC : ts.ty == TY_STR ? (ts.flen > 0 ? FK_FSTR : FK_VSTR) : ts.nt == NT_INT ? FK_INT : ts.nt == NT_LNG ? FK_LNG : ts.nt == NT_DBL ? FK_DBL : FK_SNG;
				fi.len = ts.flen; fi.sub = ts.ty >= TY_REC ? ts.ty - TY_REC : 0;
				ti.size += specSize (ts);
				P->fields.push (fi); fnames.push (fnm); ti.nf++;
			}
			P->types.push (ti);
			i = p - 1;
		}
	}

	int findConst (const char *n) { for (int i = 0; i < consts.n; i++) if (bseq (consts[i].name, n)) return i; return -1; }
	int findProc (const char *n) { for (int i = 0; i < pdecls.n; i++) if (bseq (pdecls[i].name, n)) return i; return -1; }
	const BFn *findBuiltin (const char *n) { for (int i = 0; BFNS[i].name; i++) if (bseq (BFNS[i].name, n)) return &BFNS[i]; return 0; }

	// ---- pre-scan: SUB / FUNCTION / DEF FN headers (+ DEFtype, which types their names) -----------
	// DEFINT A-C, X-Z (at pos p, just after the keyword): apply to deftype.
	int applyDeftype (int p, const TSpec &ts)
	{
		for (;;)
		{
			if (toks[p].t != T_ID) break;
			char a = toks[p].id[0], b = a; p++;
			if (toks[p].t == T_OP && toks[p].op == '-' && toks[p + 1].t == T_ID) { b = toks[p + 1].id[0]; p += 2; }
			for (char c = a; c <= b; c++) if (c >= 'A' && c <= 'Z') deftype[c - 'A'] = ts;
			if (toks[p].t == T_OP && toks[p].op == ',') p++; else break;
		}
		return p;
	}
	bool deftypeWord (const char *w, TSpec &ts)
	{
		ts.ty = TY_NUM; ts.nt = NT_SNG; ts.flen = 0;
		if (bseq (w, "DEFINT")) { ts.nt = NT_INT; return true; }
		if (bseq (w, "DEFLNG")) { ts.nt = NT_LNG; return true; }
		if (bseq (w, "DEFSNG")) return true;
		if (bseq (w, "DEFDBL")) { ts.nt = NT_DBL; return true; }
		if (bseq (w, "DEFSTR")) { ts.ty = TY_STR; return true; }
		return false;
	}
	void prescan ()
	{
		for (int i = 0; i < toks.n && !failed; i++)
		{
			bool atStart = i == 0 || toks[i - 1].t == T_NL || (toks[i - 1].t == T_OP && toks[i - 1].op == ':');
			if (!atStart || toks[i].t != T_ID) continue;
			TSpec dts;
			if (deftypeWord (toks[i].id, dts)) { applyDeftype (i + 1, dts); continue; }
			bool isSub = bseq (toks[i].id, "SUB"), isFn = bseq (toks[i].id, "FUNCTION");
			bool isDef = bseq (toks[i].id, "DEF") && toks[i + 1].t == T_ID && toks[i + 1].id[0] == 'F' && toks[i + 1].id[1] == 'N';
			if (!isSub && !isFn && !isDef) continue;
			if (i > 0 && toks[i - 1].t == T_ID) continue;		// "END SUB", "EXIT SUB", "DECLARE SUB"
			int p = i + 1;
			if (toks[p].t != T_ID) { pos = p; fail ("Expected a name after SUB / FUNCTION"); return; }
			char name[48]; bscpy (name, toks[p].id, 48);
			if (isDef && bseq (name, "FN") && toks[p + 1].t == T_ID)		// DEF FN name
			{
				int n = 2; for (int k = 0; toks[p + 1].id[k] && n < 46; k++) name[n++] = toks[p + 1].id[k];
				name[n] = 0; p++;
			}
			if (findProc (name) >= 0) { pos = p; fail2 ("Duplicate definition: ", name); return; }
			PDecl d; bscpy (d.name, name, 48); d.isFunc = isFn || isDef; d.defFn = isDef;
			TSpec rs = nameSpec (d.name); d.retTy = rs.ty; d.retNt = rs.nt; d.np = 0;
			p++;
			if (toks[p].t == T_OP && toks[p].op == '(')
			{
				p++;
				while (!(toks[p].t == T_OP && toks[p].op == ')') && toks[p].t != T_NL && toks[p].t != T_EOF)
				{
					if (toks[p].t == T_ID && bseq (toks[p].id, "BYVAL")) p++;
					if (toks[p].t != T_ID || d.np >= 16) { pos = p; fail ("Bad parameter list"); return; }
					bscpy (d.pname[d.np], toks[p].id, 48);
					TSpec ps = nameSpec (toks[p].id);
					d.parr[d.np] = false;
					p++;
					if (toks[p].t == T_OP && toks[p].op == '(' && toks[p + 1].t == T_OP && toks[p + 1].op == ')') { d.parr[d.np] = true; p += 2; }
					if (toks[p].t == T_ID && bseq (toks[p].id, "AS"))
					{
						p++;
						if (toks[p].t == T_ID)
						{
							if (!typeName (toks[p].id, ps)) { pos = p; fail2 ("Unknown type: ", toks[p].id); return; }
							p++;
						}
					}
					d.pty[d.np] = ps.ty; d.pnt[d.np] = ps.nt;
					d.np++;
					if (toks[p].t == T_OP && toks[p].op == ',') p++;
				}
			}
			if (d.isFunc && toks[p].t == T_OP && toks[p].op == ')') p++;
			if (isFn && toks[p].t == T_ID && bseq (toks[p].id, "AS") && toks[p + 1].t == T_ID)
			{
				TSpec ts; if (typeName (toks[p + 1].id, ts)) { d.retTy = ts.ty; d.retNt = ts.nt; }
			}
			pdecls.push (d);
			ProcInfo pi; bscpy (pi.name, d.name, 48); pi.isFunc = d.isFunc; pi.retTy = d.retTy; pi.nparams = d.np; pi.entry = -1; pi.nlocals = 0;
			P->procs.push (pi);
		}
		resetDeftypes ();			// the main pass re-applies them in order
	}

	// ---- expressions ----------------------------------------------------------------------------
	void needNum (int t) { if (t != TY_NUM) fail ("Type mismatch (a number is expected)"); }
	void needStr (int t) { if (t != TY_STR) fail ("Type mismatch (a string is expected)"); }

	int expr () { return eImp (); }
	int eImp () { int t = eEqv (); while (!failed && isKw ("IMP")) { next (); needNum (t); needNum (eEqv ()); emit (OP_IMP); } return t; }
	int eEqv () { int t = eXor (); while (!failed && isKw ("EQV")) { next (); needNum (t); needNum (eXor ()); emit (OP_EQV); } return t; }
	int eXor () { int t = eOr (); while (!failed && isKw ("XOR")) { next (); needNum (t); needNum (eOr ()); emit (OP_XOR); } return t; }
	int eOr ()  { int t = eAnd (); while (!failed && isKw ("OR")) { next (); needNum (t); needNum (eAnd ()); emit (OP_OR); } return t; }
	int eAnd () { int t = eNot (); while (!failed && isKw ("AND")) { next (); needNum (t); needNum (eNot ()); emit (OP_AND); } return t; }
	int eNot ()
	{
		if (isKw ("NOT")) { next (); needNum (eNot ()); emit (OP_NOT); return TY_NUM; }
		return eRel ();
	}
	int relop ()
	{
		if (cur ().t != T_OP) return 0;
		switch (cur ().op) { case '=': case '<': case '>': case O_LE: case O_GE: case O_NE: return cur ().op; }
		return 0;
	}
	int eRel ()
	{
		int t = eAdd ();
		for (;;)
		{
			int op = relop ();
			if (!op || failed) break;
			next ();
			int t2 = eAdd ();
			if (t != t2) { fail ("Type mismatch in comparison"); return TY_NUM; }
			int o = 0;
			switch (op)
			{
			case '=': o = t ? OP_SEQ : OP_EQ; break;
			case O_NE: o = t ? OP_SNE : OP_NE; break;
			case '<': o = t ? OP_SLT : OP_LT; break;
			case '>': o = t ? OP_SGT : OP_GT; break;
			case O_LE: o = t ? OP_SLE : OP_LE; break;
			case O_GE: o = t ? OP_SGE : OP_GE; break;
			}
			emit (o);
			t = TY_NUM;
		}
		return t;
	}
	int eAdd ()
	{
		int t = eMod ();
		while (!failed && (isOp ('+') || isOp ('-')))
		{
			bool plus = isOp ('+'); next ();
			int t2 = eMod ();
			if (plus && t == TY_STR && t2 == TY_STR) emit (OP_CAT);
			else { needNum (t); needNum (t2); emit (plus ? OP_ADD : OP_SUB); }
		}
		return t;
	}
	int eMod () { int t = eIdiv (); while (!failed && isKw ("MOD")) { next (); needNum (t); needNum (eIdiv ()); emit (OP_MOD); } return t; }
	int eIdiv () { int t = eMul (); while (!failed && isOp ('\\')) { next (); needNum (t); needNum (eMul ()); emit (OP_IDIV); } return t; }
	int eMul ()
	{
		int t = eUnary ();
		while (!failed && (isOp ('*') || isOp ('/')))
		{
			bool mul = isOp ('*'); next ();
			needNum (t); needNum (eUnary ());
			emit (mul ? OP_MUL : OP_DIV);
		}
		return t;
	}
	int eUnary ()
	{
		if (isOp ('-')) { next (); needNum (eUnary ()); emit (OP_NEG); return TY_NUM; }
		if (isOp ('+')) { next (); return eUnary (); }
		return ePow ();
	}
	int ePow ()
	{
		int t = primary ();
		while (!failed && isOp ('^'))
		{
			next (); needNum (t);
			if (isOp ('-')) { next (); needNum (primary ()); emit (OP_NEG); }
			else needNum (primary ());
			emit (OP_POW);
		}
		return t;
	}

	int primary ()
	{
		if (failed) return TY_NUM;
		Tok &k = cur ();
		if (k.t == T_NUM) { pushNum (k.num); if (k.dbl) dblSeen = true; next (); return TY_NUM; }
		if (k.t == T_STR) { emit2 (OP_STR, strConst (k.s, k.sl)); next (); return TY_STR; }
		if (isOp ('(')) { next (); int t = expr (); expectOp (')'); return t; }
		if (k.t != T_ID) { fail ("Syntax error in expression"); return TY_NUM; }
		char name[48]; bscpy (name, k.id, 48);
		// A builtin function.
		if (bseq (name, "LBOUND") || bseq (name, "UBOUND"))	// (array [, dimension])
		{
			bool lo = bseq (name, "LBOUND"); next ();
			expectOp ('(');
			if (cur ().t != T_ID) { fail ("An array name is expected"); return TY_NUM; }
			Var v = var (cur ().id, true); next ();
			if (isOp ('(') && peekIsOp (')')) { next (); next (); }
			loadVar (v);
			if (acceptOp (',')) needNum (expr ()); else pushNum (1);
			expectOp (')');
			emit3 (OP_BI, lo ? B_LBOUND : B_UBOUND, 2);
			return TY_NUM;
		}
		if (bseq (name, "LEN")) { next (); return lenOf (); }
		if (bseq (name, "POINT") && peekIsOp ('('))		// POINT (x, y) / POINT (n)
		{
			next (); next ();
			needNum (expr ());
			if (acceptOp (',')) { needNum (expr ()); expectOp (')'); emit3 (OP_BI, B_POINT, 2); }
			else { expectOp (')'); emit3 (OP_BI, B_POINT1, 1); }
			return TY_NUM;
		}
		const BFn *b = findBuiltin (name);
		if (b) { next (); return callBuiltin (b); }
		// A user FUNCTION (inside itself without "(": its own name is the result variable).
		int pi = findProc (name);
		if (pi >= 0 && pdecls[pi].isFunc)
		{
			if (pdecls[pi].retTy == TY_NUM && pdecls[pi].retNt == NT_DBL) dblSeen = true;
			if (curProc == pi && !peekIsOp ('('))
			{ next (); emit2 (OP_LDL, 0); return pdecls[pi].retTy; }
			next ();
			return callProc (pi, true);
		}
		if (pi >= 0) { fail2 ("A SUB has no value: ", name); return TY_NUM; }
		int ci = findConst (name);
		if (ci >= 0)
		{
			next ();
			if (consts[ci].ty == TY_STR) emit2 (OP_STR, consts[ci].sidx); else { pushNum (consts[ci].n); if (consts[ci].dbl) dblSeen = true; }
			return consts[ci].ty;
		}
		if (isKeyword (name)) { fail2 ("Syntax error near ", name); return TY_NUM; }
		Ref r;
		if (!parseRef (r)) return TY_NUM;
		emitLoad (r);
		return r.ty;
	}

	// ---- variable references: name [(indices)] [.field ...] --------------------------------------
	struct Ref { bool global; int slot; bool arr; int nd; int nfld; int fld[8]; int ty, nt, flen; bool fnResult; };
	// Follow ".a.b" (dotted in the name, from `dot`, or '.' tokens) through the record type r.ty.
	bool fieldPath (Ref &r, const char *dot)
	{
		for (;;)
		{
			char part[48]; int n = 0;
			if (dot && *dot == '.')
			{
				dot++;
				while (*dot && *dot != '.' && n < 47) part[n++] = *dot++;
				part[n] = 0;
				if (!*dot) dot = 0;
			}
			else if (isOp ('.') && peek ().t == T_ID)
			{
				next ();
				const char *id = cur ().id;
				while (*id && *id != '.' && n < 47) part[n++] = *id++;
				part[n] = 0;
				static char rest[48]; bscpy (rest, id, 48);
				next ();
				dot = rest[0] ? rest : 0;
			}
			else return true;
			if (r.ty < TY_REC) { fail2 ("Not a record: .", part); return false; }
			int f = findField (r.ty - TY_REC, part);
			if (f < 0) { fail2 ("No such field: ", part); return false; }
			if (r.nfld >= 8) { fail ("Fields nested too deep"); return false; }
			r.fld[r.nfld++] = f - P->types[r.ty - TY_REC].first;
			r.ty = fnames[f].ty; r.nt = fnames[f].nt; r.flen = fnames[f].flen;
		}
	}
	// Parses a reference at the current ID; emits the array indices (if any).
	bool parseRef (Ref &r)
	{
		Tok &k = cur ();
		if (k.t != T_ID || isKeyword (k.id) || findBuiltin (k.id) || findConst (k.id) >= 0) { fail ("A variable is expected"); return false; }
		char name[48]; bscpy (name, k.id, 48); next ();
		r.arr = false; r.nd = 0; r.nfld = 0; r.fnResult = false;
		int pi = findProc (name);
		if (pi >= 0)
		{
			if (pi != curProc || !pdecls[pi].isFunc) { fail2 ("Not a variable: ", name); return false; }
			r.global = false; r.slot = 0; r.ty = pdecls[pi].retTy; r.nt = pdecls[pi].retNt; r.flen = 0; r.fnResult = true;
			return true;
		}
		// "p.x": a record variable p and its fields (else a plain dotted name, QBasic-style)
		const char *dot = 0; char dotted[48];
		for (int i = 1; name[i]; i++)
			if (name[i] == '.')
			{
				char base[48]; bscpy (base, name, i + 1);
				Var bv;
				if (!isOp ('(') && findVar (base, false, bv) && bv.ty >= TY_REC) { bscpy (dotted, name + i, 48); dot = dotted; name[i] = 0; }
				break;
			}
		if (isOp ('(') && !dot)
		{
			next ();
			int nd = 0;
			if (!isOp (')'))
				for (;;) { needNum (expr ()); nd++; if (!acceptOp (',')) break; }
			expectOp (')');
			Var v = var (name, true);
			r.global = v.global; r.slot = v.slot; r.ty = v.ty; r.nt = v.nt; r.flen = v.flen; r.arr = true; r.nd = nd;
		}
		else
		{
			Var v = var (name, false);
			r.global = v.global; r.slot = v.slot; r.ty = v.ty; r.nt = v.nt; r.flen = v.flen;
		}
		return fieldPath (r, dot);
	}
	void emitLoad (const Ref &r)
	{
		if (r.arr) emit3 (r.global ? OP_ALDG : OP_ALDL, r.slot, r.nd);
		else emit2 (r.global ? OP_LDG : OP_LDL, r.slot);
		for (int i = 0; i < r.nfld; i++) emit2 (OP_FLD, r.fld[i]);
		if (r.ty == TY_NUM && r.nt == NT_DBL) dblSeen = true;
	}
	void emitAddr (const Ref &r)
	{
		if (r.arr) emit3 (r.global ? OP_AADDRG : OP_AADDRL, r.slot, r.nd);
		else emit2 (r.global ? OP_REFG : OP_REFL, r.slot);
		for (int i = 0; i < r.nfld; i++) emit2 (OP_FADDR, r.fld[i]);
	}
	// A reference (address) of an lvalue, for MID$ / LSET / RSET / GET / PUT / FIELD.
	bool refAddr (Ref &r) { if (!parseRef (r)) return false; emitAddr (r); return true; }

	// LEN (x): a string's length, or the size in bytes of a numeric variable / record.
	int lenOf ()
	{
		expectOp ('(');
		int at = pc (), savePos = pos;
		bool dSave = dblSeen;
		if (cur ().t == T_ID && !findBuiltin (cur ().id) && findProc (cur ().id) < 0 && findConst (cur ().id) < 0 && !isKeyword (cur ().id))
		{
			Ref r;
			if (parseRef (r) && isOp (')') && r.ty != TY_STR)
			{
				P->code.n = at;					// a sized variable: a constant
				TSpec ts; ts.ty = r.ty; ts.nt = r.nt; ts.flen = r.flen;
				next ();
				pushNum (specSize (ts));
				dblSeen = dSave;
				return TY_NUM;
			}
			if (failed) return TY_NUM;
			P->code.n = at; pos = savePos;
		}
		needStr (expr ());
		expectOp (')');
		emit3 (OP_BI, B_LEN, 1);
		dblSeen = dSave;
		return TY_NUM;
	}
	bool peekIsOp (int op) { return peek ().t == T_OP && peek ().op == op; }

	// Builtin call: the arguments (with or without parentheses when there are none).
	int callBuiltin (const BFn *b)
	{
		int argc = 0;
		const char *spec = b->args;
		int minA = 0, maxA = 0; bool opt = false;
		for (int i = 0; spec[i]; i++) { if (spec[i] == '[') { opt = true; continue; } maxA++; if (!opt) minA++; }
		bool any = spec[0] == '*';
		bool dOuter = dblSeen, dArgs = false;
		if (isOp ('('))
		{
			next ();
			if (!isOp (')'))
				for (;;)
				{
					acceptOp ('#');				// INPUT$ (n, #f), EOF (#1)
					dblSeen = false;
					int t = expr ();
					dArgs |= dblSeen;
					if (!any)
					{
						// the argc-th letter of the spec (skipping '[')
						int idx = 0; char want = 0;
						for (int i = 0; spec[i]; i++) { if (spec[i] == '[') continue; if (idx == argc) { want = spec[i]; break; } idx++; }
						if (want == 'N') needNum (t); else if (want == 'S') needStr (t);
						else if (want == 0) { fail2 ("Too many arguments to ", b->name); return b->ret; }
					}
					else if (t >= TY_REC) needNum (t);
					argc++;
					if (!acceptOp (',')) break;
				}
			expectOp (')');
		}
		if (b->id == B_INSTR)
		{
			if (argc < 2 || argc > 3) fail ("INSTR needs 2 or 3 arguments");
		}
		else if (any) { if (argc != 1) fail2 ("Wrong number of arguments to ", b->name); }
		else if (argc < minA || argc > maxA) fail2 ("Wrong number of arguments to ", b->name);
		int id = b->id;
		if (id == B_STR && dArgs) id = B_STRD;
		// the result is DOUBLE for CDBL, and for these when an argument is
		static const int keep[] = { B_ABS, B_SGN, B_INT, B_FIX, B_SQR, B_SIN, B_COS, B_TAN, B_ATN, B_EXP, B_LOG, B_MIN, B_MAX, B_CVD, 0 };
		bool d = (id == B_CDBL && bseq (b->name, "CDBL")) || id == B_CVD;
		for (int i = 0; keep[i]; i++) if (keep[i] == id && dArgs) d = true;
		dblSeen = dOuter || d;
		emit3 (OP_BI, id, argc);
		return b->ret;
	}

	// Call a SUB / FUNCTION (the name was consumed). parens: arguments are in (...).
	int callProc (int pi, bool parens)
	{
		const PDecl &d = pdecls[pi];
		int argc = 0;
		bool open = parens ? acceptOp ('(') : false;
		bool hasArgs = open ? !isOp (')') : !parens && !endOfStmt ();	// (in an expression: no '(' = no arguments)
		if (hasArgs)
			for (;;)
			{
				if (argc >= d.np) { fail2 ("Too many arguments to ", d.name); return d.retTy; }
				compileArg (d, argc);
				argc++;
				if (!acceptOp (',')) break;
			}
		if (open) expectOp (')');
		if (argc != d.np) { fail2 ("Wrong number of arguments to ", d.name); return d.retTy; }
		emit3 (OP_CALL, pi, argc);
		return d.retTy;
	}
	void compileArg (const PDecl &d, int i)
	{
		Tok &k = cur ();
		if (d.parr[i])					// an array: name or name()
		{
			if (k.t != T_ID) { fail ("An array is expected"); return; }
			char name[48]; bscpy (name, k.id, 48); next ();
			if (isOp ('(') && peekIsOp (')')) { next (); next (); }
			Var v = var (name, true);
			if (v.ty != d.pty[i]) { fail ("Type mismatch (array argument)"); return; }
			loadVar (v);
			return;
		}
		// A variable, array element or record field of the same type -> by reference
		// (QBasic's rule; a DEF FN takes its arguments by value).
		if (!d.defFn && k.t == T_ID && !findBuiltin (k.id) && findProc (k.id) < 0 && findConst (k.id) < 0 && !isKeyword (k.id))
		{
			int at = pc (), savePos = pos; bool dSave = dblSeen;
			Ref r;
			if (parseRef (r) && !r.fnResult && (isOp (',') || isOp (')') || endOfStmt ()) && r.ty == d.pty[i] && (r.ty != TY_NUM || r.nt == d.pnt[i]))
			{
				emitAddr (r);
				return;
			}
			if (failed) return;
			P->code.n = at; pos = savePos; dblSeen = dSave;
		}
		int t = expr ();
		if (t != d.pty[i]) { fail ("Type mismatch (argument)"); return; }
		convFor (t, d.pnt[i], 0);
	}

	// ---- constant expressions (CONST) ------------------------------------------------------------
	bool constPrimary (int *ty, double *n, const char **s, int *sl)
	{
		if (isOp ('-')) { next (); if (!constPrimary (ty, n, s, sl) || *ty != TY_NUM) return false; *n = -*n; return true; }
		if (isOp ('(')) { next (); bool ok = constAdd (ty, n, s, sl); expectOp (')'); return ok; }
		Tok &k = cur ();
		if (k.t == T_NUM) { *ty = TY_NUM; *n = k.num; next (); return true; }
		if (k.t == T_STR) { *ty = TY_STR; *s = k.s; *sl = k.sl; next (); return true; }
		if (k.t == T_ID)
		{
			int ci = findConst (k.id);
			if (ci < 0) return false;
			*ty = consts[ci].ty; *n = consts[ci].n;
			if (*ty == TY_STR) { *s = P->strs[consts[ci].sidx]; *sl = P->strl[consts[ci].sidx]; }
			next (); return true;
		}
		return false;
	}
	bool constMul (int *ty, double *n, const char **s, int *sl)
	{
		if (!constPrimary (ty, n, s, sl)) return false;
		while (isOp ('*') || isOp ('/'))
		{
			bool mul = isOp ('*'); next ();
			int t2; double n2; const char *s2; int l2;
			if (!constPrimary (&t2, &n2, &s2, &l2) || *ty != TY_NUM || t2 != TY_NUM) return false;
			if (!mul && n2 == 0) return false;
			*n = mul ? *n * n2 : *n / n2;
		}
		return true;
	}
	bool constAdd (int *ty, double *n, const char **s, int *sl)
	{
		if (!constMul (ty, n, s, sl)) return false;
		while (isOp ('+') || isOp ('-'))
		{
			bool plus = isOp ('+'); next ();
			int t2; double n2; const char *s2; int l2;
			if (!constMul (&t2, &n2, &s2, &l2) || *ty != TY_NUM || t2 != TY_NUM) return false;
			*n = plus ? *n + n2 : *n - n2;
		}
		return true;
	}

	// ---- lvalues ----------------------------------------------------------------------------------
	// A variable / array element (its indices pushed now) / record field (its reference pushed
	// now) / the FUNCTION's result; storeLV (after the value) stores it.
	struct LV { bool global; int slot; int ty; bool arr; int nd; bool ref; int nt; int flen; };
	bool lvalue (LV &lv)
	{
		Ref r;
		if (!parseRef (r)) return false;
		lv.global = r.global; lv.slot = r.slot; lv.ty = r.ty; lv.arr = r.arr; lv.nd = r.nd; lv.nt = r.nt; lv.flen = r.flen;
		lv.ref = r.nfld > 0;
		if (lv.ref) emitAddr (r);
		return true;
	}
	void storeLV (const LV &lv)
	{
		convFor (lv.ty, lv.nt, lv.flen);
		if (lv.ref) emit (OP_STREF);
		else if (lv.arr) emit3 (lv.global ? OP_ASTG : OP_ASTL, lv.slot, lv.nd);
		else emit2 (lv.global ? OP_STG : OP_STL, lv.slot);
	}
	void loadLV (const LV &lv)		// (only for scalars)
	{ emit2 (lv.global ? OP_LDG : OP_LDL, lv.slot); }

	// ---- labels / jumps -----------------------------------------------------------------------------
	void defineLabel (const char *name)
	{
		for (int i = 0; i < labels.n; i++)
			if (labels[i].proc == curProc && bseq (labels[i].name, name)) { fail2 ("Duplicate label: ", name); return; }
		Label l; bscpy (l.name, name, 48); l.pc = pc (); l.proc = curProc; l.dataIdx = P->data.n;
		labels.push (l);
		if (name[0] >= '0' && name[0] <= '9') { NumLabel nl; nl.pc = pc (); nl.value = (int) parseNum (name, 0); P->numLabels.push (nl); }
	}
	void jumpToLabel (int op) { labelRef (op, false); }
	// Emits op + a label's pc (patched later). module: the label is in the main module
	// (ON ERROR / ON TIMER / ON KEY / RESUME / RUN handlers).
	void labelRef (int op, bool module, int extra = -999)
	{
		char name[48];
		if (cur ().t == T_NUM) { formatNum (cur ().num, name); next (); }
		else if (cur ().t == T_ID) { bscpy (name, cur ().id, 48); next (); }
		else { fail ("A label is expected"); return; }
		emit (op);
		if (extra != -999) emit (extra);
		emit (0);
		Fix f; f.at = pc () - 1; bscpy (f.name, name, 48); f.proc = module ? -1 : curProc; f.line = cur ().line; f.data = false;
		fixes.push (f);
	}
	void resolveLabels (int proc)
	{
		for (int i = 0; i < fixes.n && !failed; i++)
		{
			if (fixes[i].proc != proc) continue;
			int found = -1;
			for (int j = 0; j < labels.n; j++)
				if (labels[j].proc == proc && bseq (labels[j].name, fixes[i].name)) { found = j; break; }
			if (found < 0 && fixes[i].data)			// RESTORE may name a module-level label
				for (int j = 0; j < labels.n; j++) if (bseq (labels[j].name, fixes[i].name)) { found = j; break; }
			if (found < 0) { fail2 ("Label not defined: ", fixes[i].name); err->line = fixes[i].line; return; }
			P->code[fixes[i].at] = fixes[i].data ? labels[found].dataIdx : labels[found].pc;
		}
	}

	// ---- statements -----------------------------------------------------------------------------------
	bool atTerm (int mask)
	{
		if (cur ().t != T_ID) return false;
		const char *w = cur ().id;
		if ((mask & TM_ELSE) && bseq (w, "ELSE")) return true;
		if ((mask & TM_ELSEIF) && bseq (w, "ELSEIF")) return true;
		if ((mask & TM_LOOP) && bseq (w, "LOOP")) return true;
		if ((mask & TM_WEND) && bseq (w, "WEND")) return true;
		if ((mask & TM_NEXT) && bseq (w, "NEXT")) return true;
		if ((mask & TM_CASE) && bseq (w, "CASE")) return true;
		if (bseq (w, "END"))
		{
			if ((mask & TM_ENDIF) && peekKw (1, "IF")) return true;
			if ((mask & TM_ENDSELECT) && peekKw (1, "SELECT")) return true;
			if ((mask & TM_ENDSUB) && peekKw (1, "SUB")) return true;
			if ((mask & TM_ENDFUNC) && peekKw (1, "FUNCTION")) return true;
			if ((mask & TM_ENDDEF) && peekKw (1, "DEF")) return true;
		}
		return false;
	}

	// Statements until a terminator in mask (left current) or EOF.
	void block (int mask)
	{
		while (!failed)
		{
			bool lineStart = pos == 0 || toks[pos - 1].t == T_NL;
			if (cur ().t == T_NL) { next (); continue; }
			if (isOp (':')) { next (); continue; }
			if (cur ().t == T_EOF) return;
			if (lineStart)				// labels: "10 PRINT", "loop1:"
			{
				if (cur ().t == T_NUM) { char nm[32]; formatNum (cur ().num, nm); defineLabel (nm); next (); continue; }
				if (cur ().t == T_ID && peekIsOp (':') && !isKeyword (cur ().id) && findProc (cur ().id) < 0 && !findBuiltin (cur ().id))
				{ defineLabel (cur ().id); next (); next (); continue; }
			}
			if (atTerm (mask)) return;
			if (isKw ("END") && (peekKw (1, "IF") || peekKw (1, "SELECT") || peekKw (1, "SUB") || peekKw (1, "FUNCTION") || peekKw (1, "DEF") || peekKw (1, "TYPE")))
			{ fail2 ("Unexpected END ", peek ().id); return; }
			if (isKw ("ELSE") || isKw ("ELSEIF") || isKw ("LOOP") || isKw ("WEND") || isKw ("NEXT") || isKw ("CASE"))
			{ fail2 (cur ().id, " without its block"); return; }
			statement ();
			if (failed) return;
			if (atTerm (mask)) continue;			// "NEXT j, i" re-queues a NEXT
			if (!(cur ().t == T_NL || cur ().t == T_EOF || isOp (':')))
			{
				if (isKw ("ELSE")) { fail ("ELSE without IF"); return; }
				fail ("Syntax error (end of statement expected)"); return;
			}
		}
	}

	void statement ()
	{
		markLine ();
		int start = pc ();
		statement1 ();
		StmtRange r; r.start = start; r.end = pc ();
		if (r.end > r.start) P->stmts.push (r);
	}
	void skipStatement () { while (!endOfStmt () || isKw ("ELSE")) { if (isKw ("ELSE") && endOfStmt ()) break; next (); } }
	void statement1 ()
	{
		if (isOp ('?')) { next (); stPrint (false); return; }
		Tok &k = cur ();
		if (k.t != T_ID) { fail ("Syntax error"); return; }
		const char *w = k.id;
		if (bseq (w, "PRINT") || bseq (w, "LPRINT")) { next (); stPrint (false); return; }
		if (bseq (w, "WRITE")) { next (); stPrint (true); return; }
		if (bseq (w, "LET")) { next (); stAssign (); return; }
		if (bseq (w, "IF")) { next (); stIf (); return; }
		if (bseq (w, "FOR")) { next (); stFor (); return; }
		if (bseq (w, "WHILE")) { next (); stWhile (); return; }
		if (bseq (w, "DO")) { next (); stDo (); return; }
		if (bseq (w, "SELECT")) { next (); stSelect (); return; }
		if (bseq (w, "GOTO")) { next (); jumpToLabel (OP_JMP); return; }
		if (bseq (w, "GOSUB")) { next (); jumpToLabel (OP_GOSUB); return; }
		if (bseq (w, "RETURN")) { next (); stReturn (); return; }
		if (bseq (w, "ON")) { next (); stOn (); return; }
		if (bseq (w, "RESUME")) { next (); stResume (); return; }
		if (bseq (w, "ERROR")) { next (); needNum (expr ()); emit3 (OP_ST, S_ERROR, 1); return; }
		if (bseq (w, "DIM") || bseq (w, "REDIM")) { next (); stDim (); return; }
		if (bseq (w, "SHARED")) { next (); stShared (); return; }
		if (bseq (w, "STATIC")) { next (); stStatic (); return; }
		if (bseq (w, "COMMON")) { next (); stCommon (); return; }
		if (bseq (w, "ERASE")) { next (); stErase (); return; }
		if (bseq (w, "CONST")) { next (); stConst (); return; }
		if (bseq (w, "TYPE")) { stType (); return; }
		if (bseq (w, "INPUT")) { next (); stInput (); return; }
		if (bseq (w, "LINE") && peekKw (1, "INPUT")) { next (); next (); stLineInput (); return; }
		if (bseq (w, "LINE")) { next (); stLine (); return; }
		if (bseq (w, "READ")) { next (); stRead (); return; }
		if (bseq (w, "DATA")) { next (); stData (); return; }
		if (bseq (w, "RESTORE")) { next (); stRestore (); return; }
		if (bseq (w, "OPEN")) { next (); stOpen (); return; }
		if (bseq (w, "CLOSE")) { next (); stClose (); return; }
		if (bseq (w, "GET") || bseq (w, "PUT")) { bool get = bseq (w, "GET"); next (); stGetPut (get); return; }
		if (bseq (w, "FIELD")) { next (); stField (); return; }
		if (bseq (w, "SEEK")) { next (); acceptOp ('#'); needNum (expr ()); expectOp (','); needNum (expr ()); emit (OP_SEEK); return; }
		if (bseq (w, "LSET") || bseq (w, "RSET"))
		{
			bool l = bseq (w, "LSET"); next ();
			Ref r; if (!refAddr (r)) return;
			if (r.ty != TY_STR) { fail ("LSET / RSET need a string variable"); return; }
			expectOp ('='); needStr (expr ());
			emit (l ? OP_LSET : OP_RSET);
			return;
		}
		if (bseq (w, "MID$"))
		{
			next (); expectOp ('(');
			Ref r; if (!refAddr (r)) return;
			if (r.ty != TY_STR) { fail ("MID$ needs a string variable"); return; }
			expectOp (','); needNum (expr ());
			if (acceptOp (',')) needNum (expr ()); else pushNum (-1);
			expectOp (')'); expectOp ('=');
			needStr (expr ());
			emit (OP_MIDSET);
			return;
		}
		if (bseq (w, "SWAP")) { next (); stSwap (); return; }
		if (bseq (w, "CALL")) { next (); stCall (); return; }
		if (bseq (w, "EXIT")) { next (); stExit (); return; }
		if (bseq (w, "END") || bseq (w, "SYSTEM")) { next (); emit (OP_END); return; }
		if (bseq (w, "STOP")) { next (); emit (OP_STOP); return; }
		if (bseq (w, "CHAIN")) { next (); needStr (expr ()); emit (OP_CHAIN); return; }
		if (bseq (w, "RUN"))
		{
			next ();
			if (endOfStmt ()) { emit3 (OP_RUN, 0, 0); return; }
			if (cur ().t == T_NUM || (cur ().t == T_ID && !findBuiltin (cur ().id) && !varExists (cur ().id, false) && !isKeyword (cur ().id)))
			{ labelRef (OP_RUN, true, 1); return; }
			needStr (expr ()); emit3 (OP_RUN, 2, 0);
			return;
		}
		if (bseq (w, "CLEAR")) { next (); while (!endOfStmt ()) next (); emit (OP_CLEAR); return; }
		if (bseq (w, "TRON") || bseq (w, "TROFF")) { bool on = bseq (w, "TRON"); next (); emit2 (OP_TRON, on ? 1 : 0); return; }
		if (bseq (w, "TIMER") && (peekKw (1, "ON") || peekKw (1, "OFF") || peekKw (1, "STOP")))
		{ next (); pushNum (0); emit3 (OP_EVSTATE, 0, evState ()); return; }
		if (bseq (w, "KEY")) { next (); stKey (); return; }
		if (bseq (w, "DECLARE") || bseq (w, "LOCK") || bseq (w, "UNLOCK")) { while (!(cur ().t == T_NL || cur ().t == T_EOF)) next (); return; }
		{
			TSpec dts;
			if (deftypeWord (w, dts)) { next (); pos = applyDeftype (pos, dts); return; }
		}
		if (bseq (w, "DEF"))
		{
			if (peek ().t == T_ID && peek ().id[0] == 'F' && peek ().id[1] == 'N') { stDefFn (); return; }
			while (!endOfStmt ()) next ();				// DEF SEG: nothing to do
			return;
		}
		if (bseq (w, "OPTION")) { next (); if (acceptKw ("BASE")) { base1 = cur ().t == T_NUM && cur ().num == 1; next (); } else while (!endOfStmt ()) next (); return; }
		if (bseq (w, "SUB") || bseq (w, "FUNCTION")) { stProc (); return; }
		if (bseq (w, "PSET") || bseq (w, "PRESET")) { bool re = bseq (w, "PRESET"); next (); stPset (re); return; }
		if (bseq (w, "CIRCLE")) { next (); stCircle (); return; }
		if (bseq (w, "PAINT")) { next (); stPaint (); return; }
		if (bseq (w, "VIEW")) { next (); stView (); return; }
		if (bseq (w, "WINDOW") && (peekIsOp ('(') || peekKw (1, "SCREEN") || peek ().t == T_NL || peek ().t == T_EOF || (peek ().t == T_OP && peek ().op == ':')))
		{ next (); stGWindow (); return; }
		if (bseq (w, "PALETTE")) { next (); stPalette (); return; }
		if (bseq (w, "FULLSCREEN"))				// FULLSCREEN [ON | OFF]
		{
			next ();
			int on = 1;
			if (acceptKw ("OFF")) on = 0; else acceptKw ("ON");
			pushNum (on); emit3 (OP_ST, S_FULLSCREEN, 1);
			return;
		}
		if (bseq (w, "NAME")) { next (); needStr (expr ()); expectKw ("AS"); needStr (expr ()); emit3 (OP_ST, S_NAME, 2); return; }
		if (simpleStatement (w)) return;
		// A SUB call without CALL.
		int pi = findProc (w);
		if (pi >= 0 && !pdecls[pi].isFunc)
		{
			next ();
			// "Name (a, b)" (without CALL) is accepted like "CALL Name (a, b)".
			bool parens = false;
			if (isOp ('('))
			{
				int depth = 0, p = pos;
				for (; p < toks.n && toks[p].t != T_NL && toks[p].t != T_EOF; p++)
				{
					if (toks[p].t == T_OP && toks[p].op == '(') depth++;
					else if (toks[p].t == T_OP && toks[p].op == ')' && --depth == 0) break;
				}
				Tok &a = toks[p + 1 < toks.n ? p + 1 : p];
				parens = depth == 0 && (a.t == T_NL || a.t == T_EOF || (a.t == T_OP && a.op == ':') || (a.t == T_ID && bseq (a.id, "ELSE")));
			}
			callProc (pi, parens);
			return;
		}
		if (pi >= 0 && pdecls[pi].isFunc && curProc != pi) { fail2 ("A FUNCTION's value must be used: ", w); return; }
		stAssign ();
	}
	int evState ()
	{
		int st = 1;
		if (acceptKw ("ON")) st = 1; else if (acceptKw ("OFF")) st = 0; else if (acceptKw ("STOP")) st = 2; else fail ("ON, OFF or STOP expected");
		return st;
	}
	// KEY(n) ON/OFF/STOP, KEY n, s$ (a user key: CHR$(shift) + CHR$(scan code)), KEY ON/OFF/LIST.
	void stKey ()
	{
		if (isOp ('('))
		{
			next (); needNum (expr ()); expectOp (')');
			emit3 (OP_EVSTATE, 1, evState ());
			return;
		}
		if (isKw ("ON") || isKw ("OFF") || isKw ("LIST")) { next (); return; }	// the soft-key line: not shown
		needNum (expr ()); expectOp (','); needStr (expr ());
		emit3 (OP_ST, S_KEYDEF, 2);
	}
	// RETURN (from GOSUB), or RETURN value in a FUNCTION.
	void stReturn ()
	{
		if (endOfStmt ()) { emit (OP_RETSUB); return; }
		if (curProc < 0 || !pdecls[curProc].isFunc) { fail ("RETURN with a value outside a FUNCTION"); return; }
		const PDecl &d = pdecls[curProc];
		int t = expr ();
		if (t != d.retTy) { fail ("Type mismatch (RETURN)"); return; }
		convFor (d.retTy, d.retNt, 0);
		emit2 (OP_STL, 0);
		emit (OP_RETF);
	}
	void stResume ()
	{
		if (acceptKw ("NEXT")) { emit3 (OP_RESUME, 1, 0); return; }
		if (endOfStmt () || (cur ().t == T_NUM && cur ().num == 0)) { if (!endOfStmt ()) next (); emit3 (OP_RESUME, 0, 0); return; }
		labelRef (OP_RESUME, true, 2);
	}
	// TYPE ... END TYPE: collected by prescanTypes; skipped here.
	void stType ()
	{
		while (!failed && cur ().t != T_EOF)
		{
			if (isKw ("END") && peekKw (1, "TYPE")) { next (); next (); return; }
			next ();
		}
	}
	// COMMON [SHARED] [/block/] var, arr(), ... -- the variables CHAIN hands on.
	void stCommon ()
	{
		if (curProc >= 0) { fail ("COMMON is for the main module"); return; }
		bool shared = acceptKw ("SHARED");
		if (acceptOp ('/')) { while (!isOp ('/') && !endOfStmt ()) next (); expectOp ('/'); }
		for (;;)
		{
			if (cur ().t != T_ID || isKeyword (cur ().id)) { fail ("A name is expected after COMMON"); return; }
			char name[48]; bscpy (name, cur ().id, 48); next ();
			bool arr = false;
			if (isOp ('(') && peekIsOp (')')) { next (); next (); arr = true; }
			TSpec ts; bool has = parseAsType (ts);
			Var v = declVar (name, arr, has ? &ts : 0, shared);
			bool dup = false;
			for (int i = 0; i < P->common.n; i++) if (P->common[i] == v.slot) dup = true;
			if (!dup) P->common.push (v.slot);
			if (!acceptOp (',')) break;
		}
	}
	// Statements "NAME arg, arg, ..." with fixed argument types; '[' = optional from here.
	bool simpleStatement (const char *w)
	{
		static const struct { const char *name; int id; const char *args; } S[] = {
			{ "CLS", S_CLS, "[N" }, { "LOCATE", S_LOCATE, "~[NNNNN" }, { "COLOR", S_COLOR, "~[NNN" }, { "SCREEN", S_SCREEN, "~N[NNN" },
			{ "DRAW", S_DRAW, "S" }, { "PCOPY", S_PCOPY, "NN" }, { "RESET", S_RESET, "" }, { "FILES", S_FILES, "[S" },
			{ "CHDIR", S_CHDIR, "S" }, { "ENVIRON", S_ENVIRON, "S" }, { "SHELL", S_SHELL, "[S" },
			{ "DRAWTEXT", S_DRAWTEXT, "NNS[N" }, { "SLEEP", S_SLEEP, "[N" }, { "PAUSE", S_PAUSE, "N" },
			{ "BEEP", S_BEEP, "" }, { "SOUND", S_SOUND, "NN" }, { "RANDOMIZE", S_RANDOMIZE, "[N" },
			{ "WINDOW", S_WINDOW, "S[NN" }, { "SETTEXT", S_SETTEXT, "NS" }, { "SETVALUE", S_SETVALUE, "NN" },
			{ "NOTIFY", S_NOTIFY, "SS" }, { "SETCLIPBOARD", S_SETCLIPBOARD, "S" }, { "EXEC", S_EXEC, "S[S" },
			{ "LAUNCH", S_LAUNCH, "S" }, { "KILL", S_KILL, "S" }, { "MKDIR", S_MKDIR, "S" }, { "RMDIR", S_RMDIR, "S" },
			{ "WIDTH", S_WIDTH, "[NN" }, { "PLAY", S_PLAY, "S" }, { "NOTEON", S_NOTEON, "NN[NN" },
			{ "NOTEOFF", S_NOTEOFF, "[N" }, { 0, 0, 0 } };
		for (int i = 0; S[i].name; i++)
		{
			if (!bseq (S[i].name, w)) continue;
			next ();
			const char *spec = S[i].args;
			bool blanks = spec[0] == '~';			// LOCATE , 5  (empty args = -1)
			if (blanks) spec++;
			int argc = 0, minA = 0, maxA = 0; bool opt = false;
			for (int j = 0; spec[j]; j++) { if (spec[j] == '[') { opt = true; continue; } maxA++; if (!opt) minA++; }
			if (!endOfStmt ())
				for (;;)
				{
					char want = 0; int idx = 0;
					for (int j = 0; spec[j]; j++) { if (spec[j] == '[') continue; if (idx == argc) { want = spec[j]; break; } idx++; }
					if (!want) { fail2 ("Too many arguments to ", S[i].name); return true; }
					if (blanks && (isOp (',') || endOfStmt ())) pushNum (-1);
					else { int t = expr (); if (want == 'N') needNum (t); else needStr (t); }
					argc++;
					if (!acceptOp (',')) break;
				}
			if (argc < minA || argc > maxA) { fail2 ("Wrong number of arguments to ", S[i].name); return true; }
			emit3 (OP_ST, S[i].id, argc);
			return true;
		}
		return false;
	}

	void stAssign ()
	{
		LV lv;
		if (!lvalue (lv)) return;
		if (!acceptOp ('=')) { fail ("Syntax error (= expected)"); return; }
		int t = expr ();
		if (t != lv.ty) { fail ("Type mismatch in assignment"); return; }
		storeLV (lv);
	}

	void stPrint (bool write)
	{
		bool chan = false;
		if (acceptOp ('#')) { needNum (expr ()); expectOp (','); emit (OP_CHAN); chan = true; }
		if (acceptKw ("USING"))
		{
			needStr (expr ());
			if (!endOfStmt () && !acceptOp (';')) expectOp (',');
			int n = 0; bool nl2 = true;
			while (!endOfStmt () && !failed)
			{
				if (acceptOp (';') || acceptOp (',')) { nl2 = false; continue; }
				int t = expr ();
				if (t >= TY_REC) { fail ("Type mismatch (PRINT USING)"); return; }
				n++; nl2 = true;
			}
			emit2 (OP_USING, n);
			if (nl2) emit2 (OP_PRSEP, 2);
			if (chan) { pushNum (0); emit (OP_CHAN); }
			return;
		}
		bool nl = true;
		while (!endOfStmt () && !failed)
		{
			if (acceptOp (';')) { nl = false; continue; }
			if (acceptOp (',')) { emit2 (OP_PRSEP, write ? 3 : 1); nl = false; continue; }
			if ((isKw ("TAB") || isKw ("SPC")) && peekIsOp ('('))
			{
				bool tab = isKw ("TAB"); next (); next ();
				needNum (expr ()); expectOp (')');
				emit (tab ? OP_PRTAB : OP_PRSPC); nl = true; continue;
			}
			dblSeen = false;
			int t = expr ();
			if (t >= TY_REC) { fail ("Type mismatch (a record cannot be printed)"); return; }
			emit2 (OP_PRINT, (write ? 1 : 0) | (dblSeen ? 2 : 0));
			nl = true;
		}
		if (nl || write) emit2 (OP_PRSEP, 2);
		if (chan) { pushNum (0); emit (OP_CHAN); }
	}

	void stInput ()
	{
		bool chan = false;
		if (acceptOp ('#')) { needNum (expr ()); expectOp (','); emit (OP_CHAN); chan = true; }
		else
		{
			acceptOp (';');
			int prompt = -1, flags = 1;		// 1 = show "? "
			if (cur ().t == T_STR)
			{
				prompt = strConst (cur ().s, cur ().sl); next ();
				if (acceptOp (',')) flags = 0; else expectOp (';');
			}
			emit3 (OP_INPUT, prompt, flags);
		}
		for (;;)
		{
			LV lv;
			if (!lvalue (lv)) return;
			emit2 (OP_INFIELD, lv.ty);
			storeLV (lv);
			if (!acceptOp (',')) break;
		}
		if (chan) { pushNum (0); emit (OP_CHAN); }
	}

	void stLineInput ()
	{
		bool chan = false; int prompt = -1;
		if (acceptOp ('#')) { needNum (expr ()); expectOp (','); emit (OP_CHAN); chan = true; }
		else
		{
			acceptOp (';');
			if (cur ().t == T_STR) { prompt = strConst (cur ().s, cur ().sl); next (); if (!acceptOp (';')) expectOp (','); }
		}
		LV lv;
		if (!lvalue (lv)) return;
		if (lv.ty != TY_STR) { fail ("LINE INPUT needs a string variable"); return; }
		emit2 (OP_LINPUT, prompt);
		storeLV (lv);
		if (chan) { pushNum (0); emit (OP_CHAN); }
	}

	void stIf ()
	{
		int l0 = toks[pos - 1].line;
		needNum (expr ());
		if (isKw ("GOTO")) { next (); int j = emitJump (OP_JZ); jumpToLabel (OP_JMP); patch (j, pc ()); return; }
		expectKw ("THEN");
		if (failed) return;
		if (cur ().t != T_NL && cur ().t != T_EOF)		// single-line IF
		{
			int jf = emitJump (OP_JZ);
			singleLine ();
			if (isKw ("ELSE"))
			{
				next ();
				int je = emitJump (OP_JMP);
				patch (jf, pc ());
				singleLine ();
				patch (je, pc ());
			}
			else patch (jf, pc ());
			return;
		}
		// Block IF.
		int ends[64]; int ne = 0;
		int jf = emitJump (OP_JZ);
		block (TM_ELSE | TM_ELSEIF | TM_ENDIF);
		while (!failed && isKw ("ELSEIF"))
		{
			if (ne < 64) ends[ne++] = emitJump (OP_JMP);
			patch (jf, pc ());
			next (); markLine ();
			needNum (expr ()); expectKw ("THEN");
			jf = emitJump (OP_JZ);
			block (TM_ELSE | TM_ELSEIF | TM_ENDIF);
		}
		if (!failed && isKw ("ELSE"))
		{
			if (ne < 64) ends[ne++] = emitJump (OP_JMP);
			patch (jf, pc ()); jf = -1;
			next ();
			block (TM_ENDIF);
		}
		if (failed) return;
		if (!(isKw ("END") && peekKw (1, "IF"))) { fail ("IF without END IF", l0); return; }
		next (); next ();
		if (jf >= 0) patch (jf, pc ());
		for (int i = 0; i < ne; i++) patch (ends[i], pc ());
	}
	// "THEN a: b: c" -- up to ELSE or the end of the line. A bare number = GOTO.
	void singleLine ()
	{
		if (cur ().t == T_NUM) { jumpToLabel (OP_JMP); return; }
		for (;;)
		{
			statement ();
			if (failed) return;
			if (acceptOp (':')) { if (cur ().t == T_NL || cur ().t == T_EOF || isKw ("ELSE")) return; continue; }
			return;
		}
	}

	Loop *pushLoop (int kind)
	{
		if (nloops >= 32) { fail ("Loops nested too deep"); return 0; }
		Loop *l = &loops[nloops++]; l->kind = kind; l->nexit = 0; return l;
	}
	void addExit (Loop *l, int at) { if (l->nexit < 64) l->exits[l->nexit++] = at; else fail ("Too many EXITs"); }
	void popLoop (int target) { Loop *l = &loops[--nloops]; for (int i = 0; i < l->nexit; i++) patch (l->exits[i], target); }

	void stFor ()
	{
		int l0 = toks[pos - 1].line;
		LV lv;
		if (!lvalue (lv)) return;
		if (lv.arr || lv.ref || lv.ty != TY_NUM) { fail ("FOR needs a numeric variable"); return; }
		expectOp ('=');
		needNum (expr ()); storeLV (lv);
		expectKw ("TO");
		Var lim = tempVar (TY_NUM), step = tempVar (TY_NUM);
		needNum (expr ()); storeVar (lim);
		if (acceptKw ("STEP")) needNum (expr ()); else pushNum (1);
		storeVar (step);
		int top = pc ();
		loadVar (step); pushNum (0); emit (OP_LT);
		int jneg = emitJump (OP_JNZ);
		loadLV (lv); loadVar (lim); emit (OP_GT);
		int x1 = emitJump (OP_JNZ);
		int jbody = emitJump (OP_JMP);
		patch (jneg, pc ());
		loadLV (lv); loadVar (lim); emit (OP_LT);
		int x2 = emitJump (OP_JNZ);
		patch (jbody, pc ());
		Loop *l = pushLoop (LOOP_FOR); if (!l) return;
		addExit (l, x1); addExit (l, x2);
		int depth = nloops;
		block (TM_NEXT);
		if (failed) return;
		if (!isKw ("NEXT")) { fail ("FOR without NEXT", l0); return; }
		markLine ();
		next ();
		// NEXT [var [, var ...]]: this loop, then maybe the enclosing ones.
		bool more = cur ().t == T_ID;
		if (more && cur ().t == T_ID) next ();				// (the variable name: not checked)
		loadLV (lv); loadVar (step); emit (OP_ADD); storeLV (lv);
		emit2 (OP_JMP, top);
		(void) depth;
		popLoop (pc ());
		if (more && acceptOp (','))					// "NEXT j, i": close the outer FOR too
		{
			// Let the enclosing stFor handle its NEXT: put a synthetic NEXT back.
			pos--; toks[pos].t = T_ID; bscpy (toks[pos].id, "NEXT", 48);
		}
	}

	void stWhile ()
	{
		int l0 = toks[pos - 1].line;
		int top = pc ();
		needNum (expr ());
		int jx = emitJump (OP_JZ);
		Loop *l = pushLoop (LOOP_WHILE); if (!l) return;
		addExit (l, jx);
		block (TM_WEND);
		if (failed) return;
		if (!acceptKw ("WEND")) { fail ("WHILE without WEND", l0); return; }
		emit2 (OP_JMP, top);
		popLoop (pc ());
	}

	void stDo ()
	{
		int l0 = toks[pos - 1].line;
		int top = pc ();
		Loop *l = pushLoop (LOOP_DO); if (!l) return;
		if (isKw ("WHILE") || isKw ("UNTIL"))
		{
			bool wh = isKw ("WHILE"); next ();
			needNum (expr ());
			addExit (l, emitJump (wh ? OP_JZ : OP_JNZ));
		}
		block (TM_LOOP);
		if (failed) return;
		if (!acceptKw ("LOOP")) { fail ("DO without LOOP", l0); return; }
		if (isKw ("WHILE") || isKw ("UNTIL"))
		{
			bool wh = isKw ("WHILE"); next ();
			needNum (expr ());
			emit2 (wh ? OP_JNZ : OP_JZ, top);
		}
		else emit2 (OP_JMP, top);
		popLoop (pc ());
	}

	void stExit ()
	{
		if (acceptKw ("SUB") || acceptKw ("FUNCTION") || acceptKw ("DEF"))
		{
			if (curProc < 0) { fail ("EXIT SUB outside a SUB"); return; }
			emit (pdecls[curProc].isFunc ? OP_RETF : OP_RET);
			return;
		}
		int kind = 0;
		if (acceptKw ("FOR")) kind = LOOP_FOR;
		else if (acceptKw ("DO")) kind = LOOP_DO;
		else if (acceptKw ("WHILE")) kind = LOOP_WHILE;
		else { fail ("EXIT what? (FOR, DO, SUB, FUNCTION)"); return; }
		for (int i = nloops - 1; i >= 0; i--)
			if (loops[i].kind == kind) { addExit (&loops[i], emitJump (OP_JMP)); return; }
		fail ("EXIT outside its loop");
	}

	void stSelect ()
	{
		int l0 = toks[pos - 1].line;
		expectKw ("CASE");
		int t = expr ();
		Var sel = tempVar (t);
		storeVar (sel);
		int ends[128]; int ne = 0;
		// skip to the first CASE
		while (cur ().t == T_NL || isOp (':')) next ();
		while (!failed && isKw ("CASE"))
		{
			markLine ();
			next ();
			if (acceptKw ("ELSE"))
			{
				block (TM_CASE | TM_ENDSELECT);
				if (ne < 128) ends[ne++] = emitJump (OP_JMP);
				continue;
			}
			int toBody[32]; int nb = 0;
			for (;;)
			{
				if (acceptKw ("IS") || relop ())
				{
					int op = relop ();
					if (!op) { fail ("Comparison expected after IS"); return; }
					next ();
					loadVar (sel);
					if (expr () != t) { fail ("Type mismatch in CASE"); return; }
					int o = 0;
					switch (op)
					{
					case '=': o = t ? OP_SEQ : OP_EQ; break;
					case O_NE: o = t ? OP_SNE : OP_NE; break;
					case '<': o = t ? OP_SLT : OP_LT; break;
					case '>': o = t ? OP_SGT : OP_GT; break;
					case O_LE: o = t ? OP_SLE : OP_LE; break;
					case O_GE: o = t ? OP_SGE : OP_GE; break;
					}
					emit (o);
				}
				else
				{
					loadVar (sel);
					if (expr () != t) { fail ("Type mismatch in CASE"); return; }
					if (acceptKw ("TO"))		// a <= sel AND sel <= b
					{
						emit (t ? OP_SGE : OP_GE);
						loadVar (sel);
						if (expr () != t) { fail ("Type mismatch in CASE"); return; }
						emit (t ? OP_SLE : OP_LE);
						emit (OP_AND);
					}
					else emit (t ? OP_SEQ : OP_EQ);
				}
				if (nb < 32) toBody[nb++] = emitJump (OP_JNZ);
				if (!acceptOp (',')) break;
			}
			int skip = emitJump (OP_JMP);
			for (int i = 0; i < nb; i++) patch (toBody[i], pc ());
			block (TM_CASE | TM_ENDSELECT);
			if (ne < 128) ends[ne++] = emitJump (OP_JMP);
			patch (skip, pc ());
		}
		if (failed) return;
		if (!(isKw ("END") && peekKw (1, "SELECT"))) { fail ("SELECT CASE without END SELECT", l0); return; }
		next (); next ();
		for (int i = 0; i < ne; i++) patch (ends[i], pc ());
	}

	void stOn ()
	{
		if (acceptKw ("ERROR"))
		{
			expectKw ("GOTO");
			if (cur ().t == T_NUM && cur ().num == 0) { next (); emit2 (OP_ONERR, -1); return; }
			labelRef (OP_ONERR, true);
			return;
		}
		if (isKw ("TIMER") || isKw ("KEY"))			// ON TIMER (n) GOSUB / ON KEY (n) GOSUB
		{
			bool timer = isKw ("TIMER"); next ();
			expectOp ('('); needNum (expr ()); expectOp (')');
			expectKw ("GOSUB");
			labelRef (OP_ONEVENT, true, timer ? 0 : 1);
			return;
		}
		needNum (expr ());
		Var t = tempVar (TY_NUM); storeVar (t);
		bool gosub = false;
		if (acceptKw ("GOSUB")) gosub = true; else expectKw ("GOTO");
		int n = 1;
		int ends[64]; int ne = 0;
		for (;;)
		{
			loadVar (t); pushNum (n); emit (OP_EQ);
			if (gosub)
			{
				int skip = emitJump (OP_JZ);
				jumpToLabel (OP_GOSUB);
				if (ne < 64) ends[ne++] = emitJump (OP_JMP);
				patch (skip, pc ());
			}
			else jumpToLabel (OP_JNZ);
			n++;
			if (!acceptOp (',')) break;
		}
		for (int i = 0; i < ne; i++) patch (ends[i], pc ());
	}

	// AS type -> ts (false when there is no AS).
	bool parseAsType (TSpec &ts)
	{
		ts.ty = TY_NUM; ts.nt = NT_SNG; ts.flen = 0;
		if (!acceptKw ("AS")) return false;
		if (cur ().t != T_ID) { fail ("Type expected after AS"); return false; }
		if (!typeName (cur ().id, ts)) { fail2 ("Unknown type: ", cur ().id); return false; }
		next ();
		if (ts.ty == TY_STR && acceptOp ('*'))			// STRING * n: a fixed-length string
		{
			if (cur ().t != T_NUM && !(cur ().t == T_ID && findConst (cur ().id) >= 0)) { fail ("A length is expected after STRING *"); return false; }
			ts.flen = cur ().t == T_NUM ? (int) cur ().num : (int) consts[findConst (cur ().id)].n;
			next ();
		}
		return true;
	}

	void stDim ()
	{
		bool shared = acceptKw ("SHARED");
		for (;;)
		{
			if (cur ().t != T_ID || isKeyword (cur ().id)) { fail ("A name is expected after DIM"); return; }
			char name[48]; bscpy (name, cur ().id, 48); next ();
			if (isOp ('('))
			{
				next ();
				int nd = 0;
				for (;;)
				{
					needNum (expr ());
					if (acceptKw ("TO")) needNum (expr ());
					else { Var tmp = tempVar (TY_NUM); storeVar (tmp); pushNum (base1 ? 1 : 0); loadVar (tmp); }
					nd++;
					if (!acceptOp (',')) break;
				}
				expectOp (')');
				TSpec ts; bool has = parseAsType (ts);
				if (nd > 4) { fail ("Arrays have at most 4 dimensions"); return; }
				Var v = declVar (name, true, has ? &ts : 0, shared);
				int ek = v.ty >= TY_REC ? 2 : v.ty == TY_STR ? 1 : 0, ext = v.ty >= TY_REC ? v.ty - TY_REC : v.flen;
				emit (v.global ? OP_DIMG : OP_DIML); emit (v.slot); emit (nd); emit (ek); emit (ext);
			}
			else
			{
				TSpec ts; bool has = parseAsType (ts);
				declVar (name, false, has ? &ts : 0, shared);
			}
			if (failed || !acceptOp (',')) break;
		}
	}
	char skey[52];
	Var declVar (const char *name, bool arr, const TSpec *ts, bool shared)
	{
		Var v = var (name, arr, ts);
		if (ts && (v.ty != ts->ty || (v.ty == TY_NUM && v.nt != ts->nt) || v.flen != ts->flen)) { fail2 ("Duplicate definition: ", name); return v; }
		if (shared)
		{
			if (curProc >= 0) { fail ("DIM SHARED is for the main module"); return v; }
			int g = ts ? findIn (gsyms, (makeKey (skey, name, arr), skey)) : findSym (gsyms, name, arr);
			if (g >= 0) gsyms[g].shared = true;
		}
		return v;
	}
	// STATIC x, y() -- inside a SUB: variables that keep their value between calls (a
	// hidden global per procedure + name). Arrays still need their DIM.
	void stStatic ()
	{
		if (curProc < 0) { stDim (); return; }
		for (;;)
		{
			if (cur ().t != T_ID) { fail ("A name is expected after STATIC"); return; }
			char name[48]; bscpy (name, cur ().id, 48); next ();
			bool arr = false;
			if (isOp ('(') && peekIsOp (')')) { next (); next (); arr = true; }
			TSpec ts; bool has = parseAsType (ts); if (!has) ts = nameSpec (name);
			char key[52]; if (has) makeKey (key, name, arr); else implicitKey (key, name, arr);
			char gname[52]; int n = 0; gname[n++] = '~';
			for (int i = 0; pdecls[curProc].name[i] && n < 22; i++) gname[n++] = pdecls[curProc].name[i];
			gname[n++] = '.';
			for (int i = 0; key[i] && n < 50; i++) gname[n++] = key[i];
			gname[n] = 0;
			Sym g; bscpy (g.key, gname, 52); g.slot = P->nglobals++; g.ty = ts.ty; g.nt = ts.nt; g.flen = ts.flen;
			g.shared = false; g.global = true;
			if (findIn (gsyms, gname) >= 0) { fail2 ("Duplicate STATIC: ", name); return; }
			gsyms.push (g);
			Sym l = g; bscpy (l.key, key, 52); l.shared = true; l.global = true;
			lsyms.push (l);
			if (!acceptOp (',')) break;
		}
	}

	void stShared ()
	{
		if (curProc < 0) { fail ("SHARED is for SUBs and FUNCTIONs"); return; }
		for (;;)
		{
			if (cur ().t != T_ID) { fail ("A name is expected after SHARED"); return; }
			char name[48]; bscpy (name, cur ().id, 48); next ();
			bool arr = false;
			if (isOp ('(') && peekIsOp (')')) { next (); next (); arr = true; }
			TSpec ts; bool has = parseAsType (ts); if (!has) ts = nameSpec (name);
			char key[52]; if (has) makeKey (key, name, arr); else implicitKey (key, name, arr);
			int g = has ? findIn (gsyms, key) : findSym (gsyms, name, arr);
			Sym l;
			if (g >= 0) l = gsyms[g];
			else
			{
				Sym s; bscpy (s.key, key, 52); s.slot = P->nglobals++; s.ty = ts.ty; s.nt = ts.nt; s.flen = ts.flen;
				s.shared = false; s.global = true; gsyms.push (s);
				l = s;
			}
			l.shared = true; l.global = true;
			lsyms.push (l);
			if (!acceptOp (',')) break;
		}
	}
	void stErase ()
	{
		for (;;)
		{
			if (cur ().t != T_ID) { fail ("An array name is expected"); return; }
			Var v = var (cur ().id, true); next ();
			emit2 (v.global ? OP_ERASEG : OP_ERASEL, v.slot);
			if (!acceptOp (',')) break;
		}
	}
	void stConst ()
	{
		for (;;)
		{
			if (cur ().t != T_ID) { fail ("A name is expected after CONST"); return; }
			Const c; bscpy (c.name, cur ().id, 48); next ();
			expectOp ('=');
			const char *s = 0; int sl = 0;
			if (!constAdd (&c.ty, &c.n, &s, &sl)) { fail ("CONST needs a constant value"); return; }
			c.sidx = c.ty == TY_STR ? strConst (s, sl) : -1;
			c.dbl = nameSpec (c.name).nt == NT_DBL;
			if (findConst (c.name) >= 0) { fail2 ("Duplicate CONST: ", c.name); return; }
			consts.push (c);
			if (!acceptOp (',')) break;
		}
	}

	void stRead ()
	{
		for (;;)
		{
			LV lv;
			if (!lvalue (lv)) return;
			emit2 (OP_READ, lv.ty);
			storeLV (lv);
			if (!acceptOp (',')) break;
		}
	}
	void stData ()
	{
		if (cur ().t != T_DATA) return;
		const char *s = cur ().s; int n = cur ().sl;
		int i = 0;
		while (i <= n)
		{
			while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
			DataItem d; int st, e;
			if (i < n && s[i] == '"')
			{
				st = ++i; while (i < n && s[i] != '"') i++;
				e = i; if (i < n) i++;
				d.isStr = true;
				while (i < n && s[i] != ',') i++;
			}
			else
			{
				st = i; while (i < n && s[i] != ',') i++;
				e = i; while (e > st && (s[e - 1] == ' ' || s[e - 1] == '\t')) e--;
				d.isStr = false;
			}
			d.text = new char[e - st + 1]; bmcpy (d.text, s + st, e - st); d.text[e - st] = 0;
			P->data.push (d);
			i++;					// the comma
		}
		next ();
	}
	void stRestore ()
	{
		if (endOfStmt ()) { emit2 (OP_RESTORE, 0); return; }
		char name[48];
		if (cur ().t == T_NUM) formatNum (cur ().num, name); else bscpy (name, cur ().id, 48);
		next ();
		int at = emitJump (OP_RESTORE);
		Fix f; f.at = at; bscpy (f.name, name, 48); f.proc = curProc; f.line = cur ().line; f.data = true;
		fixes.push (f);
	}

	// OPEN f$ [FOR INPUT|OUTPUT|APPEND|RANDOM|BINARY] [ACCESS ...] [SHARED|LOCK ...] AS [#]n [LEN = r]
	// or the old OPEN mode$, [#]n, f$[, r]. Stack: path n reclen (mode 9: mode$ n path reclen).
	void stOpen ()
	{
		needStr (expr ());
		if (acceptOp (','))
		{
			acceptOp ('#'); needNum (expr ()); expectOp (',');
			needStr (expr ());
			if (acceptOp (',')) needNum (expr ()); else pushNum (-1);
			emit2 (OP_OPEN, 9);
			return;
		}
		int mode = 4;					// RANDOM by default
		if (acceptKw ("FOR"))
		{
			if (acceptKw ("INPUT")) mode = 1; else if (acceptKw ("OUTPUT")) mode = 2; else if (acceptKw ("APPEND")) mode = 3;
			else if (acceptKw ("RANDOM")) mode = 4; else if (acceptKw ("BINARY")) mode = 5;
			else { fail ("INPUT, OUTPUT, APPEND, RANDOM or BINARY expected"); return; }
		}
		if (acceptKw ("ACCESS")) { while (cur ().t == T_ID && (isKw ("READ") || isKw ("WRITE"))) next (); }
		acceptKw ("SHARED");
		if (acceptKw ("LOCK")) { while (cur ().t == T_ID && (isKw ("READ") || isKw ("WRITE"))) next (); }
		expectKw ("AS");
		acceptOp ('#');
		needNum (expr ());
		if (cur ().t == T_ID && bseq (cur ().id, "LEN")) { next (); expectOp ('='); needNum (expr ()); }
		else pushNum (-1);
		emit2 (OP_OPEN, mode);
	}
	// GET / PUT: a file record (GET #n [, [rec] [, var]]) or graphics (GET (x1,y1)-(x2,y2), a()).
	void stGetPut (bool get)
	{
		if (isOp ('(') || isKw ("STEP")) { if (get) stGGet (); else stGPut (); return; }
		acceptOp ('#');
		needNum (expr ());
		bool var = false;
		if (acceptOp (','))
		{
			if (isOp (',') || endOfStmt ()) pushNum (-1); else needNum (expr ());
			if (acceptOp (','))
			{
				Ref r; if (!refAddr (r)) return;
				emit (get ? OP_FGET : OP_FPUT); emit (1);
				int kind = r.ty >= TY_REC ? LK_REC : r.ty == TY_STR ? (r.flen > 0 ? LK_FSTR : LK_VSTR)
					 : r.nt == NT_INT ? LK_INT : r.nt == NT_LNG ? LK_LNG : r.nt == NT_DBL ? LK_DBL : LK_SNG;
				emit (kind); emit (r.ty >= TY_REC ? r.ty - TY_REC : r.flen);
				var = true;
			}
		}
		else pushNum (-1);
		if (!var) { emit (get ? OP_FGET : OP_FPUT); emit (0); emit (0); emit (0); }
	}
	// FIELD #n, w AS a$, w2 AS b$ ...
	void stField ()
	{
		acceptOp ('#'); needNum (expr ());
		int n = 0;
		while (acceptOp (','))
		{
			needNum (expr ()); expectKw ("AS");
			Ref r; if (!refAddr (r)) return;
			if (r.ty != TY_STR) { fail ("FIELD needs string variables"); return; }
			n++;
		}
		emit2 (OP_FIELD, n);
	}
	// ---- graphics ------------------------------------------------------------------------
	// [STEP] (x, y): pushes x, y; returns 1 if STEP.
	int point2 ()
	{
		int st = acceptKw ("STEP") ? 1 : 0;
		expectOp ('('); needNum (expr ()); expectOp (','); needNum (expr ()); expectOp (')');
		return st;
	}
	void optNum (int def) { if (isOp (',') || endOfStmt ()) pushNum (def); else needNum (expr ()); }
	// an array (for GET / PUT / PALETTE USING): pushes the array and a start index (-1 = first)
	bool arrayArg ()
	{
		if (cur ().t != T_ID) { fail ("An array is expected"); return false; }
		Var v = var (cur ().id, true); next ();
		loadVar (v);
		if (isOp ('(') && peekIsOp (')')) { next (); next (); pushNum (-1); }
		else if (acceptOp ('(')) { needNum (expr ()); if (isOp (',')) { fail ("Only one subscript here"); return false; } expectOp (')'); }
		else pushNum (-1);
		return true;
	}
	void stGGet ()
	{
		int f = point2 ();
		expectOp ('-');
		f |= point2 () << 1;
		pushNum (f);
		expectOp (',');
		if (!arrayArg ()) return;
		emit3 (OP_ST, S_GGET, 7);
	}
	void stGPut ()
	{
		int f = point2 ();
		pushNum (f);
		expectOp (',');
		if (!arrayArg ()) return;
		int act = 4;					// XOR by default
		if (acceptOp (','))
		{
			if (acceptKw ("PSET")) act = 0; else if (acceptKw ("PRESET")) act = 1; else if (acceptKw ("AND")) act = 2;
			else if (acceptKw ("OR")) act = 3; else if (acceptKw ("XOR")) act = 4; else { fail ("PSET, PRESET, AND, OR or XOR expected"); return; }
		}
		pushNum (act);
		emit3 (OP_ST, S_GPUT, 6);
	}
	// PSET [STEP] (x, y)[, c]  /  PRESET
	void stPset (bool preset)
	{
		int st = point2 ();
		if (acceptOp (',')) needNum (expr ()); else pushNum (preset ? -2 : -1);
		pushNum (st);
		emit3 (OP_ST, S_PSET, 4);
	}
	// LINE [[STEP] (x1, y1)]-[STEP] (x2, y2)[, [c][, [B | BF][, style]]]
	void stLine ()
	{
		int f = 0;
		if (isOp ('(') || isKw ("STEP")) f |= point2 ();
		else { pushNum (0); pushNum (0); f |= 4; }
		expectOp ('-');
		f |= point2 () << 1;
		int box = 0;
		if (acceptOp (','))
		{
			optNum (-1);
			if (acceptOp (','))
			{
				if (isKw ("B")) { box = 1; next (); } else if (isKw ("BF")) { box = 2; next (); }
				else if (!isOp (',')) { fail ("B or BF expected"); return; }
				if (acceptOp (',')) needNum (expr ()); else pushNum (-1);
			}
			else pushNum (-1);
		}
		else { pushNum (-1); pushNum (-1); }
		pushNum (box); pushNum (f);
		emit3 (OP_ST, S_LINE, 8);
	}
	// CIRCLE [STEP] (x, y), r[, [c][, [start][, [end][, aspect]]]] -- or [, c, F] to fill (Onyx).
	void stCircle ()
	{
		int f = point2 ();
		expectOp (','); needNum (expr ());
		if (acceptOp (','))
		{
			optNum (-1);
			static const int bits[3] = { 4, 8, 16 };
			for (int i = 0; i < 3; i++)
			{
				if (!acceptOp (',')) { for (; i < 3; i++) pushNum (0); break; }
				if (isKw ("F")) { next (); f |= 2; pushNum (0); for (i++; i < 3; i++) pushNum (0); break; }
				if (isOp (',') || endOfStmt ()) pushNum (0);
				else { needNum (expr ()); f |= bits[i]; }
			}
		}
		else { pushNum (-1); pushNum (0); pushNum (0); pushNum (0); }
		pushNum (f);
		emit3 (OP_ST, S_CIRCLE, 8);
	}
	// PAINT [STEP] (x, y)[, [paint][, [border]]]  (a tile string paints with the default colour)
	void stPaint ()
	{
		int f = point2 ();
		if (acceptOp (','))
		{
			if (isOp (',') || endOfStmt ()) pushNum (-1);
			else { int t = expr (); if (t == TY_STR) { emit (OP_POP); pushNum (-1); } else needNum (t); }
			if (acceptOp (',')) optNum (-1); else pushNum (-1);
			while (acceptOp (',')) { if (!endOfStmt ()) { expr (); emit (OP_POP); } }	// background tile: ignored
		}
		else { pushNum (-1); pushNum (-1); }
		pushNum (f);
		emit3 (OP_ST, S_PAINT, 5);
	}
	// VIEW [[SCREEN] (x1, y1)-(x2, y2)[, [fill][, border]]]  /  VIEW PRINT [top TO bottom]
	void stView ()
	{
		if (acceptKw ("PRINT"))
		{
			if (endOfStmt ()) { emit3 (OP_ST, S_VIEWPRINT, 0); return; }
			needNum (expr ()); expectKw ("TO"); needNum (expr ());
			emit3 (OP_ST, S_VIEWPRINT, 2);
			return;
		}
		if (endOfStmt ()) { emit3 (OP_ST, S_VIEW, 0); return; }
		int scr = acceptKw ("SCREEN") ? 1 : 0;
		expectOp ('('); needNum (expr ()); expectOp (','); needNum (expr ()); expectOp (')');
		expectOp ('-');
		expectOp ('('); needNum (expr ()); expectOp (','); needNum (expr ()); expectOp (')');
		if (acceptOp (',')) { optNum (-1); if (acceptOp (',')) optNum (-1); else pushNum (-1); }
		else { pushNum (-1); pushNum (-1); }
		pushNum (scr);
		emit3 (OP_ST, S_VIEW, 7);
	}
	// WINDOW [[SCREEN] (x1, y1)-(x2, y2)] -- logical coordinates.
	void stGWindow ()
	{
		if (endOfStmt ()) { emit3 (OP_ST, S_GWINDOW, 0); return; }
		int scr = acceptKw ("SCREEN") ? 1 : 0;
		expectOp ('('); needNum (expr ()); expectOp (','); needNum (expr ()); expectOp (')');
		expectOp ('-');
		expectOp ('('); needNum (expr ()); expectOp (','); needNum (expr ()); expectOp (')');
		pushNum (scr);
		emit3 (OP_ST, S_GWINDOW, 5);
	}
	// PALETTE [attr, colour] / PALETTE USING array[(start)]
	void stPalette ()
	{
		if (acceptKw ("USING")) { if (!arrayArg ()) return; emit3 (OP_ST, S_PALUSING, 2); return; }
		if (endOfStmt ()) { emit3 (OP_ST, S_PALETTE, 0); return; }
		needNum (expr ()); expectOp (','); needNum (expr ());
		emit3 (OP_ST, S_PALETTE, 2);
	}
	void stClose ()
	{
		if (endOfStmt ()) { pushNum (0); emit (OP_CLOSE); return; }
		for (;;)
		{
			acceptOp ('#');
			needNum (expr ()); emit (OP_CLOSE);
			if (!acceptOp (',')) break;
		}
	}

	void stSwap ()
	{
		int posA = pos, at = pc ();
		LV a; if (!lvalue (a)) return;
		expectOp (',');
		int posB = pos;
		LV b; if (!lvalue (b)) return;
		int posEnd = pos;
		P->code.n = at;				// (the parse above only found the types)
		if (a.ty != b.ty) { fail ("SWAP needs two variables of the same type"); return; }
		Var t = tempVar (a.ty);
		// t = a
		pos = posA; expr (); storeVar (t);
		// a = b
		pos = posA; lvalue (a); pos = posB; expr (); storeLV (a);
		// b = t
		pos = posB; lvalue (b); loadVar (t); storeLV (b);
		pos = posEnd;
	}

	void stCall ()
	{
		if (cur ().t != T_ID) { fail ("A SUB name is expected after CALL"); return; }
		int pi = findProc (cur ().id);
		if (pi < 0) { fail2 ("SUB not defined: ", cur ().id); return; }
		next ();
		callProc (pi, true);
		if (pdecls[pi].isFunc) emit (OP_POP);
	}

	// SUB / FUNCTION / DEF FN bodies: the procedure's scope.
	int savedLoopsProc;
	void beginProc (int pi, int &jover)
	{
		jover = emitJump (OP_JMP);
		curProc = pi;
		lsyms.n = 0; nlocals = 0;
		PDecl &d = pdecls[pi];
		if (d.isFunc) { Sym r; bscpy (r.key, "~RESULT", 52); r.slot = nlocals++; r.ty = d.retTy; r.nt = d.retNt; r.flen = 0; r.shared = false; r.global = false; lsyms.push (r); }
		for (int i = 0; i < d.np; i++)
		{
			Sym s; makeKey (s.key, d.pname[i], d.parr[i]); s.slot = nlocals++; s.ty = d.pty[i]; s.nt = d.pnt[i]; s.flen = 0;
			s.shared = false; s.global = false;
			lsyms.push (s);
		}
		P->procs[pi].entry = pc ();
		savedLoopsProc = nloops;
	}
	void endProc (int pi, int jover)
	{
		emit (pdecls[pi].isFunc ? OP_RETF : OP_RET);
		nloops = savedLoopsProc;
		resolveLabels (pi);
		P->procs[pi].nlocals = nlocals;
		P->procs[pi].kindOff = P->lkind.n;
		for (int i = 0; i < nlocals; i++) { P->lkind.push (K_NUM); P->lext.push (0); }
		for (int i = 0; i < lsyms.n; i++)
			if (!lsyms[i].global)
			{
				P->lkind[P->procs[pi].kindOff + lsyms[i].slot] = slotKind (lsyms[i]);
				P->lext[P->procs[pi].kindOff + lsyms[i].slot] = slotExt (lsyms[i]);
			}
		curProc = -1;
		patch (jover, pc ());
	}
	// SUB / FUNCTION definition (at the module level).
	void stProc ()
	{
		bool isFn = isKw ("FUNCTION");
		int l0 = cur ().line;
		next ();
		if (curProc >= 0 || nloops > 0) { fail ("SUB / FUNCTION cannot be nested"); return; }
		int pi = findProc (cur ().id);
		if (pi < 0) { fail ("Bad SUB / FUNCTION"); return; }
		while (!(cur ().t == T_NL || cur ().t == T_EOF)) next ();	// the header (parsed by prescan)
		int jover;
		beginProc (pi, jover);
		block (isFn ? TM_ENDFUNC : TM_ENDSUB);
		if (failed) return;
		if (!(isKw ("END") && (peekKw (1, "SUB") || peekKw (1, "FUNCTION")))) { fail (isFn ? "FUNCTION without END FUNCTION" : "SUB without END SUB", l0); return; }
		markLine ();
		next (); next ();
		endProc (pi, jover);
	}
	// DEF FNname[(params)] = expr   or   DEF FNname[(params)] ... END DEF (module level).
	void stDefFn ()
	{
		int l0 = cur ().line;
		next ();
		if (curProc >= 0 || nloops > 0) { fail ("DEF FN belongs to the main module"); return; }
		char name[48]; bscpy (name, cur ().id, 48); next ();
		if (bseq (name, "FN") && cur ().t == T_ID)
		{ int n = 2; for (int k = 0; cur ().id[k] && n < 46; k++) name[n++] = cur ().id[k]; name[n] = 0; next (); }
		int pi = findProc (name);
		if (pi < 0) { fail ("Bad DEF FN"); return; }
		if (isOp ('('))						// the parameters (parsed by prescan)
		{
			int depth = 0;
			while (!endOfStmt ()) { if (isOp ('(')) depth++; if (isOp (')') && --depth == 0) { next (); break; } next (); }
		}
		int jover;
		beginProc (pi, jover);
		const PDecl &d = pdecls[pi];
		if (acceptOp ('='))					// single line
		{
			int t = expr ();
			if (t != d.retTy) { fail ("Type mismatch (DEF FN)"); return; }
			convFor (d.retTy, d.retNt, 0);
			emit2 (OP_STL, 0);
		}
		else
		{
			block (TM_ENDDEF);
			if (failed) return;
			if (!(isKw ("END") && peekKw (1, "DEF"))) { fail ("DEF FN without END DEF", l0); return; }
			markLine ();
			next (); next ();
		}
		endProc (pi, jover);
	}

	// ---- driver ----------------------------------------------------------------------------------
	Program *compile (const char *src, Error *e)
	{
		err = e; e->line = 0; e->msg[0] = 0;
		P = new Program;
		lex (src);
		if (!failed) prescanTypes ();
		if (!failed) prescan ();
		pos = 0;
		if (!failed) block (0);
		if (!failed && cur ().t != T_EOF) fail ("Syntax error");
		if (!failed) { emit (OP_END); resolveLabels (-1); }
		if (!failed)
		{
			for (int i = 0; i < P->nglobals; i++) { P->gkind.push (K_NUM); P->gext.push (0); }
			for (int i = 0; i < gsyms.n; i++) { P->gkind[gsyms[i].slot] = slotKind (gsyms[i]); P->gext[gsyms[i].slot] = slotExt (gsyms[i]); }
		}
		for (int i = 0; i < P->procs.n && !failed; i++)
			if (P->procs[i].entry < 0) { fail2 ("SUB / FUNCTION without a body: ", P->procs[i].name); }
		if (failed) { delete P; return 0; }
		return P;
	}
};

Program *compile (const char *src, Error *err)
{
	Compiler *c = new Compiler;
	Program *p = c->compile (src, err);
	delete c;
	return p;
}

void destroy (Program *p) { delete p; }

} // namespace bas

int bas::wordList (char *buf, int cap)
{
	int n = 0;
	auto add = [&] (const char *w) { if (n && n < cap - 1) buf[n++] = ' '; for (; *w && n < cap - 1; w++) buf[n++] = *w; };
	for (int i = 0; KEYWORDS[i]; i++) add (KEYWORDS[i]);
	for (int i = 0; BFNS[i].name; i++) add (BFNS[i].name);
	if (cap > 0) buf[n] = 0;
	return n;
}
