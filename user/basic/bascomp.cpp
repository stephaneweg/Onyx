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

namespace bas {

enum { T_EOF, T_NL, T_NUM, T_STR, T_ID, T_OP, T_DATA };
enum { O_LE = 256, O_GE, O_NE };

struct Tok { int t; int line; double num; char id[48]; char *s; int sl; int op; };

// Words that cannot be variables / labels.
static const char *const KEYWORDS[] = {
	"AND", "AS", "CALL", "CASE", "CLOSE", "CLS", "COLOR", "CONST", "DATA", "DECLARE", "DEF", "DIM", "DO",
	"ELSE", "ELSEIF", "END", "EQV", "ERASE", "EXIT", "FOR", "FUNCTION", "GOSUB", "GOTO", "IF", "IMP", "INPUT",
	"IS", "LET", "LINE", "LOCATE", "LOOP", "MOD", "NEXT", "NOT", "ON", "OPEN", "OPTION", "OR", "PRINT",
	"READ", "REDIM", "REM", "RESTORE", "RETURN", "SELECT", "SHARED", "STATIC", "STEP", "SUB", "SWAP", "THEN",
	"TO", "UNTIL", "WEND", "WHILE", "XOR", "SCREEN", "PSET", "PRESET", "CIRCLE", "SLEEP", "RANDOMIZE", "BEEP",
	"WINDOW", "LPRINT", "WRITE", "SYSTEM", "STOP", "KILL", "NAME", "MKDIR", "RMDIR", "APPEND", "OUTPUT",
	"BINARY", "RANDOM", "USING", "TAB", "SPC", 0 };

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
	{ 0, 0, 0, 0 } };

// Block terminators (what ends a statement block).
enum
{
	TM_ENDIF = 1, TM_ELSE = 2, TM_ELSEIF = 4, TM_LOOP = 8, TM_WEND = 16, TM_NEXT = 32, TM_CASE = 64,
	TM_ENDSELECT = 128, TM_ENDSUB = 256, TM_ENDFUNC = 512
};

enum { LOOP_FOR = 1, LOOP_DO, LOOP_WHILE };

class Compiler
{
public:
	Program *P; Error *err; bool failed;
	Vec<Tok> toks; int pos;

	struct Sym { char key[52]; int slot; int ty; bool shared; bool global; };
	Vec<Sym> gsyms, lsyms; int nlocals;
	struct Const { char name[48]; int ty; double n; int sidx; };
	Vec<Const> consts;
	struct PDecl { char name[48]; bool isFunc; int retTy; int np; char pname[16][48]; int pty[16]; bool parr[16]; };
	Vec<PDecl> pdecls;
	int curProc;					// -1 = the main module
	struct Label { char name[48]; int pc; int proc; int dataIdx; };
	Vec<Label> labels;
	struct Fix { int at; char name[48]; int proc; int line; bool data; };
	Vec<Fix> fixes;
	struct Loop { int kind; int exits[64]; int nexit; };
	Loop loops[32]; int nloops;
	bool base1; int tmpN; int lastLine;

	Compiler () : P (0), err (0), failed (false), pos (0), nlocals (0), curProc (-1), nloops (0), base1 (false), tmpN (0), lastLine (-1) {}
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
	void addTok (int t, int line) { Tok k; k.t = t; k.line = line; k.num = 0; k.id[0] = 0; k.s = 0; k.sl = 0; k.op = 0; toks.push (k); }
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
				i += used;
				while (s[i] == '!' || s[i] == '#' || s[i] == '%' || s[i] == '&') i++;	// type suffix
				addTok (T_NUM, line); toks[toks.n - 1].num = v;
				continue;
			}
			if (c == '"')
			{
				int st = ++i; while (s[i] && s[i] != '"' && s[i] != '\n') i++;
				addTok (T_STR, line);
				Tok &k = toks[toks.n - 1];
				k.sl = i - st; k.s = new char[k.sl + 1];
				for (int j = 0; j < k.sl; j++) k.s[j] = s[st + j];
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
					for (int j = 0; j < k.sl; j++) k.s[j] = s[st + j];
					k.s[k.sl] = 0;
				}
				continue;
			}
			int op = c;
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
	static int suffixTy (const char *name) { int n = bslen (name); return n && name[n - 1] == '$' ? TY_STR : TY_NUM; }
	static void makeKey (char *key, const char *name, bool arr)
	{
		bscpy (key, name, 48);
		if (arr) { int n = bslen (key); key[n] = '('; key[n + 1] = ')'; key[n + 2] = 0; }
	}
	static unsigned char slotKind (const Sym &s)
	{
		int n = bslen (s.key);
		bool arr = n >= 2 && s.key[n - 1] == ')' && s.key[n - 2] == '(';
		return (unsigned char) ((arr ? 2 : 0) + (s.ty == TY_STR ? 1 : 0));
	}
	int findIn (Vec<Sym> &v, const char *key) { for (int i = 0; i < v.n; i++) if (bseq (v[i].key, key)) return i; return -1; }

	// A variable: global or local slot + type. declTy >= 0: declared type (DIM AS).
	struct Var { bool global; int slot; int ty; };
	Var var (const char *name, bool arr, int declTy = -1)
	{
		char key[52]; makeKey (key, name, arr);
		Var r;
		if (curProc >= 0)
		{
			int i = findIn (lsyms, key);
			if (i >= 0) { r.global = lsyms[i].global; r.slot = lsyms[i].slot; r.ty = lsyms[i].ty; return r; }
			int g = findIn (gsyms, key);
			if (g >= 0 && gsyms[g].shared) { r.global = true; r.slot = gsyms[g].slot; r.ty = gsyms[g].ty; return r; }
			Sym s; bscpy (s.key, key, 52); s.slot = nlocals++; s.ty = declTy >= 0 ? declTy : suffixTy (name);
			s.shared = false; s.global = false; lsyms.push (s);
			r.global = false; r.slot = s.slot; r.ty = s.ty; return r;
		}
		int g = findIn (gsyms, key);
		if (g >= 0) { r.global = true; r.slot = gsyms[g].slot; r.ty = gsyms[g].ty; return r; }
		Sym s; bscpy (s.key, key, 52); s.slot = P->nglobals++; s.ty = declTy >= 0 ? declTy : suffixTy (name);
		s.shared = false; s.global = true; gsyms.push (s);
		r.global = true; r.slot = s.slot; r.ty = s.ty; return r;
	}
	bool varExists (const char *name, bool arr)
	{
		char key[52]; makeKey (key, name, arr);
		if (curProc >= 0 && findIn (lsyms, key) >= 0) return true;
		int g = findIn (gsyms, key);
		return g >= 0 && (curProc < 0 || gsyms[g].shared);
	}
	Var tempVar (int ty)
	{
		char nm[24] = "~T"; int n = 2, v = ++tmpN; char t[12]; int k = 0;
		while (v) { t[k++] = (char) ('0' + v % 10); v /= 10; }
		while (k) nm[n++] = t[--k];
		if (ty == TY_STR) nm[n++] = '$';
		nm[n] = 0;
		return var (nm, false, ty);
	}
	void loadVar (const Var &v) { emit2 (v.global ? OP_LDG : OP_LDL, v.slot); }
	void storeVar (const Var &v) { emit2 (v.global ? OP_STG : OP_STL, v.slot); }

	int findConst (const char *n) { for (int i = 0; i < consts.n; i++) if (bseq (consts[i].name, n)) return i; return -1; }
	int findProc (const char *n) { for (int i = 0; i < pdecls.n; i++) if (bseq (pdecls[i].name, n)) return i; return -1; }
	const BFn *findBuiltin (const char *n) { for (int i = 0; BFNS[i].name; i++) if (bseq (BFNS[i].name, n)) return &BFNS[i]; return 0; }

	// ---- pre-scan: SUB / FUNCTION headers -------------------------------------------------------
	void prescan ()
	{
		for (int i = 0; i < toks.n && !failed; i++)
		{
			bool atStart = i == 0 || toks[i - 1].t == T_NL || (toks[i - 1].t == T_OP && toks[i - 1].op == ':');
			if (!atStart || toks[i].t != T_ID) continue;
			bool isSub = bseq (toks[i].id, "SUB"), isFn = bseq (toks[i].id, "FUNCTION");
			if (!isSub && !isFn) continue;
			if (i > 0 && toks[i - 1].t == T_ID) continue;		// "END SUB", "EXIT SUB", "DECLARE SUB"
			int p = i + 1;
			if (toks[p].t != T_ID) { pos = p; fail ("Expected a name after SUB / FUNCTION"); return; }
			if (findProc (toks[p].id) >= 0) { pos = p; fail2 ("Duplicate definition: ", toks[p].id); return; }
			PDecl d; bscpy (d.name, toks[p].id, 48); d.isFunc = isFn; d.retTy = suffixTy (d.name); d.np = 0;
			p++;
			if (toks[p].t == T_OP && toks[p].op == '(')
			{
				p++;
				while (!(toks[p].t == T_OP && toks[p].op == ')') && toks[p].t != T_NL && toks[p].t != T_EOF)
				{
					if (toks[p].t == T_ID && bseq (toks[p].id, "BYVAL")) p++;
					if (toks[p].t != T_ID || d.np >= 16) { pos = p; fail ("Bad parameter list"); return; }
					bscpy (d.pname[d.np], toks[p].id, 48); d.pty[d.np] = suffixTy (toks[p].id); d.parr[d.np] = false;
					p++;
					if (toks[p].t == T_OP && toks[p].op == '(' && toks[p + 1].t == T_OP && toks[p + 1].op == ')') { d.parr[d.np] = true; p += 2; }
					if (toks[p].t == T_ID && bseq (toks[p].id, "AS"))
					{
						p++;
						if (toks[p].t == T_ID) { d.pty[d.np] = bseq (toks[p].id, "STRING") ? TY_STR : TY_NUM; p++; }
					}
					d.np++;
					if (toks[p].t == T_OP && toks[p].op == ',') p++;
				}
			}
			if (isFn && toks[p].t == T_OP && toks[p].op == ')') p++;
			if (isFn && toks[p].t == T_ID && bseq (toks[p].id, "AS") && toks[p + 1].t == T_ID)
				d.retTy = bseq (toks[p + 1].id, "STRING") ? TY_STR : TY_NUM;
			pdecls.push (d);
			ProcInfo pi; bscpy (pi.name, d.name, 48); pi.isFunc = isFn; pi.retTy = d.retTy; pi.nparams = d.np; pi.entry = -1; pi.nlocals = 0;
			P->procs.push (pi);
		}
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
		if (k.t == T_NUM) { pushNum (k.num); next (); return TY_NUM; }
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
		const BFn *b = findBuiltin (name);
		if (b) { next (); return callBuiltin (b); }
		// A user FUNCTION (inside itself without "(": its own name is the result variable).
		int pi = findProc (name);
		if (pi >= 0 && pdecls[pi].isFunc)
		{
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
			if (consts[ci].ty == TY_STR) emit2 (OP_STR, consts[ci].sidx); else pushNum (consts[ci].n);
			return consts[ci].ty;
		}
		if (isKeyword (name)) { fail2 ("Syntax error near ", name); return TY_NUM; }
		next ();
		if (isOp ('('))				// array element
		{
			next ();
			int nd = 0;
			if (!isOp (')'))
				for (;;) { needNum (expr ()); nd++; if (!acceptOp (',')) break; }
			expectOp (')');
			Var v = var (name, true);
			emit3 (v.global ? OP_ALDG : OP_ALDL, v.slot, nd);
			return v.ty;
		}
		Var v = var (name, false);
		loadVar (v);
		return v.ty;
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
		if (isOp ('('))
		{
			next ();
			if (!isOp (')'))
				for (;;)
				{
					int t = expr ();
					if (!any)
					{
						// the argc-th letter of the spec (skipping '[')
						int k = 0, idx = 0; char want = 0;
						for (int i = 0; spec[i]; i++) { if (spec[i] == '[') continue; if (idx == argc) { want = spec[i]; break; } idx++; }
						(void) k;
						if (want == 'N') needNum (t); else if (want == 'S') needStr (t);
						else if (want == 0) { fail2 ("Too many arguments to ", b->name); return b->ret; }
					}
					argc++;
					if (!acceptOp (',')) break;
				}
			expectOp (')');
		}
		if (b->id == B_INSTR)
		{
			if (argc < 2 || argc > 3) fail ("INSTR needs 2 or 3 arguments");
		}
		else if (!any && (argc < minA || argc > maxA)) fail2 ("Wrong number of arguments to ", b->name);
		emit3 (OP_BI, b->id, argc);
		return b->ret;
	}

	// Call a SUB / FUNCTION (the name was consumed). parens: arguments are in (...).
	int callProc (int pi, bool parens)
	{
		const PDecl &d = pdecls[pi];
		int argc = 0;
		bool open = parens ? acceptOp ('(') : false;
		bool hasArgs = open ? !isOp (')') : !endOfStmt ();
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
		// A plain variable -> by reference.
		if (k.t == T_ID && (peekIsOp (',') || peekIsOp (')') || peek ().t == T_NL || peek ().t == T_EOF || (peek ().t == T_OP && peek ().op == ':'))
		    && !findBuiltin (k.id) && findProc (k.id) < 0 && findConst (k.id) < 0 && !isKeyword (k.id))
		{
			Var v = var (k.id, false);
			if (v.ty == d.pty[i])
			{
				next ();
				emit2 (v.global ? OP_REFG : OP_REFL, v.slot);
				return;
			}
		}
		int t = expr ();
		if (t != d.pty[i]) fail ("Type mismatch (argument)");
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
	struct LV { bool global; int slot; int ty; bool arr; int nd; };
	// Parses a variable / array element / function-result name; pushes the indices.
	bool lvalue (LV &lv)
	{
		Tok &k = cur ();
		if (k.t != T_ID || isKeyword (k.id) || findBuiltin (k.id) || findConst (k.id) >= 0) { fail ("A variable is expected"); return false; }
		char name[48]; bscpy (name, k.id, 48); next ();
		int pi = findProc (name);
		if (pi >= 0)
		{
			if (pi != curProc || !pdecls[pi].isFunc) { fail2 ("Not a variable: ", name); return false; }
			lv.global = false; lv.slot = 0; lv.ty = pdecls[pi].retTy; lv.arr = false; lv.nd = 0;
			return true;
		}
		if (isOp ('('))
		{
			next ();
			int nd = 0;
			for (;;) { needNum (expr ()); nd++; if (!acceptOp (',')) break; }
			expectOp (')');
			Var v = var (name, true);
			lv.global = v.global; lv.slot = v.slot; lv.ty = v.ty; lv.arr = true; lv.nd = nd;
			return true;
		}
		Var v = var (name, false);
		lv.global = v.global; lv.slot = v.slot; lv.ty = v.ty; lv.arr = false; lv.nd = 0;
		return true;
	}
	void storeLV (const LV &lv)
	{
		if (lv.arr) emit3 (lv.global ? OP_ASTG : OP_ASTL, lv.slot, lv.nd);
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
	}
	void jumpToLabel (int op)
	{
		char name[48];
		if (cur ().t == T_NUM) { formatNum (cur ().num, name); next (); }
		else if (cur ().t == T_ID) { bscpy (name, cur ().id, 48); next (); }
		else { fail ("A label is expected"); return; }
		int at = emitJump (op);
		Fix f; f.at = at; bscpy (f.name, name, 48); f.proc = curProc; f.line = cur ().line; f.data = false;
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
			if (isKw ("END") && (peekKw (1, "IF") || peekKw (1, "SELECT") || peekKw (1, "SUB") || peekKw (1, "FUNCTION")))
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
		if (bseq (w, "RETURN")) { next (); emit (OP_RETSUB); return; }
		if (bseq (w, "ON")) { next (); if (isKw ("ERROR")) { fail ("ON ERROR is not supported"); return; } stOn (); return; }
		if (bseq (w, "DIM") || bseq (w, "REDIM")) { next (); stDim (); return; }
		if (bseq (w, "SHARED")) { next (); stShared (); return; }
		if (bseq (w, "STATIC")) { next (); stStatic (); return; }
		if (bseq (w, "ERASE")) { next (); stErase (); return; }
		if (bseq (w, "CONST")) { next (); stConst (); return; }
		if (bseq (w, "INPUT")) { next (); stInput (); return; }
		if (bseq (w, "LINE") && peekKw (1, "INPUT")) { next (); next (); stLineInput (); return; }
		if (bseq (w, "LINE")) { next (); stLine (); return; }
		if (bseq (w, "READ")) { next (); stRead (); return; }
		if (bseq (w, "DATA")) { next (); stData (); return; }
		if (bseq (w, "RESTORE")) { next (); stRestore (); return; }
		if (bseq (w, "OPEN")) { next (); stOpen (); return; }
		if (bseq (w, "CLOSE")) { next (); stClose (); return; }
		if (bseq (w, "SWAP")) { next (); stSwap (); return; }
		if (bseq (w, "CALL")) { next (); stCall (); return; }
		if (bseq (w, "EXIT")) { next (); stExit (); return; }
		if (bseq (w, "END") || bseq (w, "SYSTEM")) { next (); emit (OP_END); return; }
		if (bseq (w, "STOP")) { next (); emit (OP_STOP); return; }
		if (bseq (w, "DECLARE") || bseq (w, "DEFINT") || bseq (w, "DEFLNG") || bseq (w, "DEFSNG") || bseq (w, "DEFDBL"))
		{ while (!(cur ().t == T_NL || cur ().t == T_EOF)) next (); return; }
		if (bseq (w, "OPTION")) { next (); if (acceptKw ("BASE")) { base1 = cur ().t == T_NUM && cur ().num == 1; next (); } else while (!endOfStmt ()) next (); return; }
		if (bseq (w, "SUB") || bseq (w, "FUNCTION")) { stProc (); return; }
		if (bseq (w, "PSET") || bseq (w, "PRESET")) { bool re = bseq (w, "PRESET"); next (); stPset (re); return; }
		if (bseq (w, "CIRCLE")) { next (); stCircle (); return; }
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

	// Statements "NAME arg, arg, ..." with fixed argument types; '[' = optional from here.
	bool simpleStatement (const char *w)
	{
		static const struct { const char *name; int id; const char *args; } S[] = {
			{ "CLS", S_CLS, "" }, { "LOCATE", S_LOCATE, "~[NN" }, { "COLOR", S_COLOR, "~[NN" }, { "SCREEN", S_SCREEN, "N" },
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
		if (isKw ("USING")) { fail ("PRINT USING is not supported"); return; }
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
			expr ();
			emit2 (OP_PRINT, write ? 1 : 0);
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
		if (lv.arr || lv.ty != TY_NUM) { fail ("FOR needs a numeric variable"); return; }
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

	int parseAsType ()
	{
		if (!acceptKw ("AS")) return -1;
		if (cur ().t != T_ID) { fail ("Type expected after AS"); return -1; }
		int ty = bseq (cur ().id, "STRING") ? TY_STR : TY_NUM;
		if (!(bseq (cur ().id, "STRING") || bseq (cur ().id, "INTEGER") || bseq (cur ().id, "LONG") || bseq (cur ().id, "SINGLE") || bseq (cur ().id, "DOUBLE") || bseq (cur ().id, "_INTEGER64")))
		{ fail2 ("Unknown type: ", cur ().id); return -1; }
		next ();
		if (ty == TY_STR && acceptOp ('*')) next ();		// STRING * n: fixed length ignored
		return ty;
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
				int ty = parseAsType ();
				if (nd > 4) { fail ("Arrays have at most 4 dimensions"); return; }
				Var v = declVar (name, true, ty, shared);
				emit (v.global ? OP_DIMG : OP_DIML); emit (v.slot); emit (nd); emit (v.ty == TY_STR ? 1 : 0);
			}
			else
			{
				int ty = parseAsType ();
				declVar (name, false, ty, shared);
			}
			if (failed || !acceptOp (',')) break;
		}
	}
	Var declVar (const char *name, bool arr, int ty, bool shared)
	{
		Var v = var (name, arr, ty);
		if (ty >= 0 && v.ty != ty) { fail2 ("Type conflict for ", name); return v; }
		if (shared)
		{
			if (curProc >= 0) { fail ("DIM SHARED is for the main module"); return v; }
			char key[52]; makeKey (key, name, arr);
			int g = findIn (gsyms, key); if (g >= 0) gsyms[g].shared = true;
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
			int ty = parseAsType ();
			char key[52]; makeKey (key, name, arr);
			char gname[52]; int n = 0; gname[n++] = '~';
			for (int i = 0; pdecls[curProc].name[i] && n < 22; i++) gname[n++] = pdecls[curProc].name[i];
			gname[n++] = '.';
			for (int i = 0; key[i] && n < 50; i++) gname[n++] = key[i];
			gname[n] = 0;
			Sym g; bscpy (g.key, gname, 52); g.slot = P->nglobals++; g.ty = ty >= 0 ? ty : suffixTy (name);
			g.shared = false; g.global = true;
			if (findIn (gsyms, gname) >= 0) { fail2 ("Duplicate STATIC: ", name); return; }
			gsyms.push (g);
			Sym l; bscpy (l.key, key, 52); l.slot = g.slot; l.ty = g.ty; l.shared = true; l.global = true;
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
			int ty = parseAsType ();
			char key[52]; makeKey (key, name, arr);
			int g = findIn (gsyms, key);
			Var gv;
			if (g >= 0) { gv.slot = gsyms[g].slot; gv.ty = gsyms[g].ty; }
			else
			{
				Sym s; bscpy (s.key, key, 52); s.slot = P->nglobals++; s.ty = ty >= 0 ? ty : suffixTy (name);
				s.shared = false; s.global = true; gsyms.push (s);
				gv.slot = s.slot; gv.ty = s.ty;
			}
			Sym l; bscpy (l.key, key, 52); l.slot = gv.slot; l.ty = gv.ty; l.shared = true; l.global = true;
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

	void stOpen ()
	{
		needStr (expr ());
		expectKw ("FOR");
		int mode = 0;
		if (acceptKw ("INPUT")) mode = 1; else if (acceptKw ("OUTPUT")) mode = 2; else if (acceptKw ("APPEND")) mode = 3;
		else if (acceptKw ("BINARY") || acceptKw ("RANDOM")) { fail ("Only INPUT, OUTPUT and APPEND files are supported"); return; }
		else { fail ("INPUT, OUTPUT or APPEND expected"); return; }
		expectKw ("AS");
		acceptOp ('#');
		needNum (expr ());
		emit2 (OP_OPEN, mode);
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
		int posA = pos;
		LV a; if (!lvalue (a)) return;
		expectOp (',');
		int posB = pos;
		LV b; if (!lvalue (b)) return;
		int posEnd = pos;
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

	// PSET (x, y)[, c]  /  PRESET
	void stPset (bool preset)
	{
		acceptKw ("STEP");
		expectOp ('('); needNum (expr ()); expectOp (','); needNum (expr ()); expectOp (')');
		if (acceptOp (',')) needNum (expr ()); else pushNum (preset ? 0 : -1);
		emit3 (OP_ST, S_PSET, 3);
	}
	// LINE [(x1, y1)]-(x2, y2)[, [c][, B | BF]]
	void stLine ()
	{
		if (isOp ('(')) { next (); needNum (expr ()); expectOp (','); needNum (expr ()); expectOp (')'); }
		else { pushNum (-32768); pushNum (-32768); }
		expectOp ('-');
		expectOp ('('); needNum (expr ()); expectOp (','); needNum (expr ()); expectOp (')');
		int box = 0;
		if (acceptOp (','))
		{
			if (isOp (',') || endOfStmt ()) pushNum (-1); else needNum (expr ());
			if (acceptOp (','))
			{
				if (isKw ("B")) box = 1; else if (isKw ("BF")) box = 2; else { fail ("B or BF expected"); return; }
				next ();
			}
		}
		else pushNum (-1);
		pushNum (box);
		emit3 (OP_ST, S_LINE, 6);
	}
	// CIRCLE (x, y), r[, c[, start, end, aspect]] -- or [, c, F] to fill (Onyx).
	void stCircle ()
	{
		acceptKw ("STEP");
		expectOp ('('); needNum (expr ()); expectOp (','); needNum (expr ()); expectOp (')');
		expectOp (','); needNum (expr ());
		int fill = 0;
		if (acceptOp (','))
		{
			if (isOp (',')) pushNum (-1); else needNum (expr ());
			while (acceptOp (','))
			{
				if (isKw ("F")) { next (); fill = 1; continue; }
				if (isOp (',') || endOfStmt ()) continue;
				needNum (expr ()); emit (OP_POP);		// start / end / aspect: ignored
			}
		}
		else pushNum (-1);
		pushNum (fill);
		emit3 (OP_ST, S_CIRCLE, 5);
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
		// skip the header (parsed by prescan)
		while (!(cur ().t == T_NL || cur ().t == T_EOF)) next ();
		int jover = emitJump (OP_JMP);
		curProc = pi;
		lsyms.n = 0; nlocals = 0;
		PDecl &d = pdecls[pi];
		if (isFn) { Sym r; bscpy (r.key, "~RESULT", 52); r.slot = nlocals++; r.ty = d.retTy; r.shared = false; r.global = false; lsyms.push (r); }
		for (int i = 0; i < d.np; i++)
		{
			Sym s; makeKey (s.key, d.pname[i], d.parr[i]); s.slot = nlocals++; s.ty = d.pty[i]; s.shared = false; s.global = false;
			lsyms.push (s);
		}
		P->procs[pi].entry = pc ();
		int savedLoops = nloops;
		block (isFn ? TM_ENDFUNC : TM_ENDSUB);
		if (failed) return;
		if (!(isKw ("END") && (peekKw (1, "SUB") || peekKw (1, "FUNCTION")))) { fail (isFn ? "FUNCTION without END FUNCTION" : "SUB without END SUB", l0); return; }
		markLine ();
		next (); next ();
		emit (isFn ? OP_RETF : OP_RET);
		nloops = savedLoops;
		resolveLabels (pi);
		P->procs[pi].nlocals = nlocals;
		P->procs[pi].kindOff = P->lkind.n;
		for (int i = 0; i < nlocals; i++) P->lkind.push (K_NUM);
		for (int i = 0; i < lsyms.n; i++)
			if (!lsyms[i].global) P->lkind[P->procs[pi].kindOff + lsyms[i].slot] = slotKind (lsyms[i]);
		curProc = -1;
		patch (jover, pc ());
	}

	// ---- driver ----------------------------------------------------------------------------------
	Program *compile (const char *src, Error *e)
	{
		err = e; e->line = 0; e->msg[0] = 0;
		P = new Program;
		lex (src);
		if (!failed) prescan ();
		pos = 0;
		if (!failed) block (0);
		if (!failed && cur ().t != T_EOF) fail ("Syntax error");
		if (!failed) { emit (OP_END); resolveLabels (-1); }
		if (!failed)
		{
			for (int i = 0; i < P->nglobals; i++) P->gkind.push (K_NUM);
			for (int i = 0; i < gsyms.n; i++) P->gkind[gsyms[i].slot] = slotKind (gsyms[i]);
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
