//
// formula.h -- a formula as Excel writes it: "=SUM(B2:B9)*(1+$E$1)", "='My Sheet'!A1&" km"", "{1,2;3,4}".
// Read into tokens (kept: they print the formula back, spaces and parentheses as typed) and a tree
// over them (evaluated by eval.h). A reference keeps the cell it points at (row, column, 0-based) and
// which parts are absolute ($): a formula copied elsewhere moves its relative parts; rows / columns
// inserted or deleted, cells moved, move every reference that points at them (deleted: #REF!).
//
#ifndef _sheet_formula_h
#define _sheet_formula_h

#include "book.h"

namespace ss {

enum { TK_NUM, TK_STR, TK_BOOL, TK_ERR, TK_REF, TK_AREA, TK_NAME, TK_FUNC, TK_OP, TK_LP, TK_RP, TK_SEP,
       TK_ALP, TK_ARP, TK_ACOL, TK_AROW, TK_SPACE };
enum { OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_CAT, OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
       OP_NEG, OP_POS, OP_PCT, OP_RANGE };
static const char *const OP_TEXT[] = { "+", "-", "*", "/", "^", "&", "=", "<>", "<", ">", "<=", ">=", "-", "+", "%", ":" };
enum { TF_AR0 = 1, TF_AC0 = 2, TF_AR1 = 4, TF_AC1 = 8, TF_COLS = 16, TF_ROWS = 32, TF_BAD = 64, TF_SHEET = 128 };

struct Tok
{
	unsigned char t, op, fl, pad;
	int sheet;					// TK_REF / TK_AREA: the sheet's id (0: the formula's own)
	int r0, c0, r1, c1;				// the cell(s) pointed at
	double num;					// TK_NUM, TK_BOOL (0 / 1), TK_ERR (its code)
	int s, sn;					// text in the pool: TK_STR, TK_NAME, TK_SPACE, an unknown TK_FUNC
	int fn;						// TK_FUNC: the function (-1: unknown)
	int at, len;					// where it was in the text (bytes)
};

enum { N_TOK, N_UN, N_BIN, N_FN, N_ARR, N_MISS, N_PAREN };
struct Node { unsigned char k, op; unsigned short n; int tok; int a, b; };
// N_TOK: tok (a literal, a reference, a name); N_UN: op, a; N_BIN: op, a, b; N_PAREN: a;
// N_FN: tok (the name), n arguments at args[a...]; N_ARR: n = rows x cols elements at args[a...]
// (their tokens), b = columns; N_MISS: an argument left out ("IF(A1,,2)").

struct Formula
{
	Tok *tok; int nt;
	Node *nd; int nn;
	int *args; int na;
	char *pool; int npool;
	int root;
	bool vol;					// volatile (NOW, RAND, OFFSET, INDIRECT...): computed every time
};

static void formula_free (Formula *f)
{
	if (!f) return;
	free (f->tok); free (f->nd); free (f->args); free (f->pool); free (f);
}

// (the functions' table, funcs.h)
static int fn_lookup (const char *name, int n);
static const char *fn_name (int i);
static bool fn_volatile (int i);

// ---- the lexer ------------------------------------------------------------------------------------------
struct Lexer
{
	Book *b;
	const char *s; int n, i;
	Tok *tok; int nt, ct;
	Buf pool;
	const char *err; int errPos;
	int brace;					// inside an array constant {...}

	Tok &add (int t)
	{
		if (nt == ct) { ct = ct ? ct * 2 : 32; tok = (Tok *) realloc (tok, ct * sizeof (Tok)); }
		Tok &k = tok[nt++];
		memset (&k, 0, sizeof k);
		k.t = (unsigned char) t; k.fn = -1;
		return k;
	}
	int text (const char *p, int len) { int o = pool.n; pool.putn (p, len); pool.put (0); return o; }
	bool fail (const char *m) { if (!err) { err = m; errPos = i; } return false; }

	static bool idch (unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '\\' || c >= 0x80; }
	static bool letter (char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
	static bool digit (char c) { return c >= '0' && c <= '9'; }

	// A column's letters at p ($ first allowed): its number, the characters read (0: none).
	int col_at (int p, int *c, bool *abs)
	{
		int q = p; *abs = false;
		if (q < n && s[q] == '$') { *abs = true; q++; }
		int v = 0, k = 0;
		while (q < n && letter (s[q]) && k < 4) { v = v * 26 + ((s[q] & 0xDF) - 'A' + 1); q++; k++; }
		if (k == 0 || k > 3 || v > MAXC) return 0;
		*c = v - 1;
		return q - p;
	}
	int row_at (int p, int *r, bool *abs)
	{
		int q = p; *abs = false;
		if (q < n && s[q] == '$') { *abs = true; q++; }
		long v = 0; int k = 0;
		while (q < n && digit (s[q]) && k < 8) { v = v * 10 + (s[q] - '0'); q++; k++; }
		if (k == 0 || v < 1 || v > MAXR) return 0;
		*r = (int) v - 1;
		return q - p;
	}
	// A cell's name at p ("B7", "$B$7"), not followed by more of a name: its length (0: none).
	int cell_at (int p, int *r, int *c, bool *ar, bool *ac)
	{
		int l1 = col_at (p, c, ac);
		if (!l1) return 0;
		int l2 = row_at (p + l1, r, ar);
		if (!l2) return 0;
		int e = p + l1 + l2;
		if (e < n && (idch ((unsigned char) s[e]) || s[e] == '(' || s[e] == '$')) return 0;
		return l1 + l2;
	}
	// A reference at i, after a sheet's name or on its own: a cell, a range, whole columns / rows.
	bool ref (int sheet, bool named)
	{
		int r0, c0, r1, c1; bool ar0, ac0, ar1, ac1;
		int l = cell_at (i, &r0, &c0, &ar0, &ac0);
		if (l)
		{
			int j = i + l;
			if (j < n && s[j] == ':')
			{
				int l2 = cell_at (j + 1, &r1, &c1, &ar1, &ac1);
				if (l2)
				{
					Tok &k = add (TK_AREA);
					k.sheet = sheet; k.fl = (named ? TF_SHEET : 0) | (ar0 ? TF_AR0 : 0) | (ac0 ? TF_AC0 : 0) | (ar1 ? TF_AR1 : 0) | (ac1 ? TF_AC1 : 0);
					k.r0 = imin (r0, r1); k.r1 = imax (r0, r1); k.c0 = imin (c0, c1); k.c1 = imax (c0, c1);
					i = j + 1 + l2;
					return true;
				}
			}
			Tok &k = add (TK_REF);
			k.sheet = sheet; k.fl = (named ? TF_SHEET : 0) | (ar0 ? TF_AR0 : 0) | (ac0 ? TF_AC0 : 0);
			k.r0 = k.r1 = r0; k.c0 = k.c1 = c0;
			i += l;
			return true;
		}
		// whole columns "A:C", "$B:$B"
		int lc = col_at (i, &c0, &ac0);
		if (lc && i + lc < n && s[i + lc] == ':')
		{
			int lc2 = col_at (i + lc + 1, &c1, &ac1);
			int e = i + lc + 1 + lc2;
			if (lc2 && !(e < n && (idch ((unsigned char) s[e]) || s[e] == '(')))
			{
				Tok &k = add (TK_AREA);
				k.sheet = sheet; k.fl = TF_COLS | (named ? TF_SHEET : 0) | (ac0 ? TF_AC0 : 0) | (ac1 ? TF_AC1 : 0) | TF_AR0 | TF_AR1;
				k.r0 = 0; k.r1 = MAXR - 1; k.c0 = imin (c0, c1); k.c1 = imax (c0, c1);
				i = e;
				return true;
			}
		}
		// whole rows "1:3", "$2:$2"
		int lr = row_at (i, &r0, &ar0);
		if (lr && i + lr < n && s[i + lr] == ':')
		{
			int lr2 = row_at (i + lr + 1, &r1, &ar1);
			int e = i + lr + 1 + lr2;
			if (lr2 && !(e < n && (idch ((unsigned char) s[e]) || s[e] == '(')))
			{
				Tok &k = add (TK_AREA);
				k.sheet = sheet; k.fl = TF_ROWS | (named ? TF_SHEET : 0) | (ar0 ? TF_AR0 : 0) | (ar1 ? TF_AR1 : 0) | TF_AC0 | TF_AC1;
				k.c0 = 0; k.c1 = MAXC - 1; k.r0 = imin (r0, r1); k.r1 = imax (r0, r1);
				i = e;
				return true;
			}
		}
		// "#REF!" after a sheet's name
		if (named && i + 5 <= n && !memcmp (s + i, "#REF!", 5))
		{
			Tok &k = add (TK_REF); k.sheet = sheet; k.fl = TF_SHEET | TF_BAD; i += 5;
			return true;
		}
		return false;
	}
	int sheet_id (const char *name, int len)	// -1: no such sheet
	{
		int k = book_sheet_index (*b, name, len);
		return k < 0 ? -1 : b->sh[k]->id;
	}

	bool run ()
	{
		while (i < n)
		{
			int st = i, nt0 = nt;
			if (!one ()) return false;
			for (int k = nt0; k < nt; k++) { tok[k].at = st; tok[k].len = i - st; }
		}
		if (brace) return fail ("An array is not closed by }.");
		return true;
	}
	// One token (or a run of spaces) from i.
	bool one ()
	{
		{
			unsigned char c = (unsigned char) s[i];
			int st = i;
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			{
				while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
				if (!brace) { Tok &k = add (TK_SPACE); k.s = text (s + st, i - st); k.sn = i - st; }
				return true;
			}
			// a number (whole rows "1:3" are not)
			if (digit (c) || (c == '.' && i + 1 < n && digit (s[i + 1])))
			{
				if (!brace && digit (c)) { int save = nt; if (ref (0, false)) return true; nt = save; }
				char t[64]; int k = 0;
				while (i < n && k < 60 && (digit (s[i]) || s[i] == '.')) t[k++] = s[i++];
				if (i < n && (s[i] == 'e' || s[i] == 'E') && i + 1 < n && (digit (s[i + 1]) || ((s[i + 1] == '+' || s[i + 1] == '-') && i + 2 < n && digit (s[i + 2]))))
				{
					t[k++] = s[i++];
					if (s[i] == '+' || s[i] == '-') t[k++] = s[i++];
					while (i < n && k < 62 && digit (s[i])) t[k++] = s[i++];
				}
				t[k] = 0;
				char *e; double v = strtod (t, &e);
				if (*e) return fail ("A number is written wrongly.");
				Tok &tk = add (TK_NUM); tk.num = v;
				return true;
			}
			if (c == '-' && brace && i + 1 < n && (digit (s[i + 1]) || s[i + 1] == '.'))	// (a negative constant in {...})
			{
				i++;
				char t[64]; int k = 0; t[k++] = '-';
				while (i < n && k < 60 && (digit (s[i]) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || ((s[i] == '+' || s[i] == '-') && (s[i - 1] == 'e' || s[i - 1] == 'E')))) t[k++] = s[i++];
				t[k] = 0;
				Tok &tk = add (TK_NUM); tk.num = strtod (t, 0);
				return true;
			}
			if (c == '"')
			{
				Buf t; i++;
				for (;;)
				{
					if (i >= n) return fail ("A text is not closed by \".");
					if (s[i] == '"') { if (i + 1 < n && s[i + 1] == '"') { t.put ('"'); i += 2; continue; } i++; break; }
					t.put (s[i++]);
				}
				Tok &k = add (TK_STR); k.s = text (t.str (), t.n); k.sn = t.n;
				return true;
			}
			if (c == '#')
			{
				int e = 0;
				for (int k = 1; k <= 7; k++)
				{
					int l = (int) strlen (ERR_NAMES[k]);
					if (i + l <= n && ascii_ieq (s + i, ERR_NAMES[k], l)) { e = k; i += l; break; }
				}
				if (!e) return fail ("Unknown error value.");
				Tok &k = add (TK_ERR); k.num = e;
				return true;
			}
			if (c == '\'')					// 'A sheet'!A1
			{
				Buf t; i++;
				for (;;)
				{
					if (i >= n) return fail ("A sheet's name is not closed by '.");
					if (s[i] == '\'') { if (i + 1 < n && s[i + 1] == '\'') { t.put ('\''); i += 2; continue; } i++; break; }
					t.put (s[i++]);
				}
				if (i >= n || s[i] != '!') return fail ("A sheet's name must be followed by !.");
				i++;
				int id = sheet_id (t.str (), t.n);
				if (!ref (id < 0 ? -1 : id, true)) return fail ("A reference is expected after the sheet's name.");
				if (id < 0) tok[nt - 1].fl |= TF_BAD;
				return true;
			}
			if (c == '$' || letter ((char) c) || c == '_' || c == '\\' || c >= 0x80)
			{
				int j = i;
				while (j < n && (idch ((unsigned char) s[j]) || s[j] == '$')) j++;
				if (j < n && s[j] == '!')				// Sheet1!A1
				{
					int id = sheet_id (s + i, j - i);
					i = j + 1;
					if (!ref (id < 0 ? -1 : id, true)) return fail ("A reference is expected after the sheet's name.");
					if (id < 0) tok[nt - 1].fl |= TF_BAD;
					return true;
				}
				if (j < n && s[j] == '(')				// a function
				{
					int p = i, l = j - i;
					if (l > 6 && ascii_ieq (s + p, "_xlfn.", 6)) { p += 6; l -= 6; }
					if (l > 6 && ascii_ieq (s + p, "_xlws.", 6)) { p += 6; l -= 6; }
					int f = fn_lookup (s + p, l);
					Tok &k = add (TK_FUNC); k.fn = f;
					if (f < 0) { k.s = text (s + p, l); k.sn = l; }
					i = j;
					return true;
				}
				if (!brace) { int save = nt; if (ref (0, false)) return true; nt = save; }
				int l = j - i;
				if (l == 4 && ascii_ieq (s + i, "TRUE", 4)) { Tok &k = add (TK_BOOL); k.num = 1; i = j; return true; }
				if (l == 5 && ascii_ieq (s + i, "FALSE", 5)) { Tok &k = add (TK_BOOL); k.num = 0; i = j; return true; }
				if (brace) return fail ("An array holds numbers, texts, TRUE / FALSE or errors only.");
				Tok &k = add (TK_NAME); k.s = text (s + i, l); k.sn = l;
				i = j;
				return true;
			}
			i++;
			switch (c)
			{
			case '+': add (TK_OP).op = OP_ADD; break;
			case '-': add (TK_OP).op = OP_SUB; break;
			case '*': add (TK_OP).op = OP_MUL; break;
			case '/': add (TK_OP).op = OP_DIV; break;
			case '^': add (TK_OP).op = OP_POW; break;
			case '&': add (TK_OP).op = OP_CAT; break;
			case '%': add (TK_OP).op = OP_PCT; break;
			case ':': add (TK_OP).op = OP_RANGE; break;
			case '=': add (TK_OP).op = OP_EQ; break;
			case '<':
				if (i < n && s[i] == '=') { i++; add (TK_OP).op = OP_LE; }
				else if (i < n && s[i] == '>') { i++; add (TK_OP).op = OP_NE; }
				else add (TK_OP).op = OP_LT;
				break;
			case '>':
				if (i < n && s[i] == '=') { i++; add (TK_OP).op = OP_GE; }
				else add (TK_OP).op = OP_GT;
				break;
			case '(': add (TK_LP); break;
			case ')': add (TK_RP); break;
			case ',': add (brace ? TK_ACOL : TK_SEP); break;
			case ';': add (brace ? TK_AROW : TK_SEP); break;
			case '{': if (brace) { i--; return fail ("Arrays do not nest."); } brace++; add (TK_ALP); break;
			case '}': if (!brace) { i--; return fail ("A } without its {."); } brace--; add (TK_ARP); break;
			default: i--; return fail ("A character a formula cannot hold.");
			}
		}
		return true;
	}
};

// ---- the parser -----------------------------------------------------------------------------------------
struct Parser
{
	Formula *f;
	int p;						// the next token
	int cn, ca;
	const char *err; int errTok;

	void skip () { while (p < f->nt && f->tok[p].t == TK_SPACE) p++; }
	int peek () { skip (); return p < f->nt ? f->tok[p].t : -1; }
	bool isop (int op) { return peek () == TK_OP && f->tok[p].op == op; }
	int node (int k, int op, int tok, int a, int b, int n = 0)
	{
		if (f->nn == cn) { cn = cn ? cn * 2 : 32; f->nd = (Node *) realloc (f->nd, cn * sizeof (Node)); }
		Node &x = f->nd[f->nn];
		x.k = (unsigned char) k; x.op = (unsigned char) op; x.n = (unsigned short) n; x.tok = tok; x.a = a; x.b = b;
		return f->nn++;
	}
	int arg (int v) { if (f->na == ca) { ca = ca ? ca * 2 : 16; f->args = (int *) realloc (f->args, ca * sizeof (int)); } f->args[f->na] = v; return f->na++; }
	int fail (const char *m) { if (!err) { err = m; errTok = p; } return -1; }

	int primary ()
	{
		int t = peek ();
		if (t < 0) return fail ("The formula ends too soon.");
		Tok &k = f->tok[p];
		switch (t)
		{
		case TK_NUM: case TK_STR: case TK_BOOL: case TK_ERR: case TK_REF: case TK_AREA: case TK_NAME:
			return node (N_TOK, 0, p++, 0, 0);
		case TK_LP:
		{
			p++;
			int a = expr ();
			if (a < 0) return -1;
			if (peek () != TK_RP) return fail ("A ) is missing.");
			p++;
			return node (N_PAREN, 0, 0, a, 0);
		}
		case TK_FUNC:
		{
			int ft = p++;
			if (fn_volatile (k.fn)) f->vol = true;
			if (peek () != TK_LP) return fail ("A ( is expected after a function's name.");
			p++;
			int tmp[256], nargs = 0;
			if (peek () == TK_RP) p++;
			else
				for (;;)
				{
					int a;
					int nx = peek ();
					if (nx == TK_SEP || nx == TK_RP) a = node (N_MISS, 0, 0, 0, 0);
					else { a = expr (); if (a < 0) return -1; }
					if (nargs >= 255) return fail ("Too many arguments.");
					tmp[nargs++] = a;
					nx = peek ();
					if (nx == TK_SEP) { p++; continue; }
					if (nx == TK_RP) { p++; break; }
					return fail ("A ) or a , is missing in a function's arguments.");
				}
			int first = f->na;
			for (int i = 0; i < nargs; i++) arg (tmp[i]);
			return node (N_FN, 0, ft, first, 0, nargs);
		}
		case TK_ALP:
		{
			p++;
			int first = f->na, cols = -1, c = 0, rows = 1;
			for (;;)
			{
				int x = peek ();
				if (x == TK_NUM || x == TK_STR || x == TK_BOOL || x == TK_ERR) { arg (p++); c++; }
				else return fail ("An array holds numbers, texts, TRUE / FALSE or errors only.");
				x = peek ();
				if (x == TK_ACOL) { p++; continue; }
				if (x == TK_AROW) { p++; if (cols >= 0 && c != cols) return fail ("An array's rows must be as long."); cols = c; c = 0; rows++; continue; }
				if (x == TK_ARP) { p++; if (cols >= 0 && c != cols) return fail ("An array's rows must be as long."); cols = c; break; }
				return fail ("An array is written wrongly.");
			}
			return node (N_ARR, 0, rows, first, cols, rows * cols);
		}
		case TK_OP:
			return fail ("An operator is misplaced.");
		default:
			return fail ("The formula is written wrongly.");
		}
	}
	int range ()
	{
		int a = primary ();
		while (a >= 0 && isop (OP_RANGE)) { p++; int b = primary (); if (b < 0) return -1; a = node (N_BIN, OP_RANGE, 0, a, b); }
		return a;
	}
	int neg ()
	{
		if (isop (OP_SUB) || isop (OP_ADD))
		{
			int op = f->tok[p].op == OP_SUB ? OP_NEG : OP_POS;
			p++;
			int a = neg ();
			return a < 0 ? -1 : node (N_UN, op, 0, a, 0);
		}
		return range ();
	}
	int pct ()
	{
		int a = neg ();
		while (a >= 0 && isop (OP_PCT)) { p++; a = node (N_UN, OP_PCT, 0, a, 0); }
		return a;
	}
	int pow_ ()
	{
		int a = pct ();
		while (a >= 0 && isop (OP_POW)) { p++; int b = pct (); if (b < 0) return -1; a = node (N_BIN, OP_POW, 0, a, b); }
		return a;
	}
	int mul ()
	{
		int a = pow_ ();
		while (a >= 0 && (isop (OP_MUL) || isop (OP_DIV))) { int op = f->tok[p++].op; int b = pow_ (); if (b < 0) return -1; a = node (N_BIN, op, 0, a, b); }
		return a;
	}
	int add ()
	{
		int a = mul ();
		while (a >= 0 && (isop (OP_ADD) || isop (OP_SUB))) { int op = f->tok[p++].op; int b = mul (); if (b < 0) return -1; a = node (N_BIN, op, 0, a, b); }
		return a;
	}
	int cat ()
	{
		int a = add ();
		while (a >= 0 && isop (OP_CAT)) { p++; int b = add (); if (b < 0) return -1; a = node (N_BIN, OP_CAT, 0, a, b); }
		return a;
	}
	int expr ()
	{
		int a = cat ();
		while (a >= 0 && peek () == TK_OP && f->tok[p].op >= OP_EQ && f->tok[p].op <= OP_GE)
		{
			int op = f->tok[p++].op;
			int b = cat ();
			if (b < 0) return -1;
			a = node (N_BIN, op, 0, a, b);
		}
		return a;
	}
};

// Text (without its "=") -> a formula; 0 and why (and where, in bytes) when it cannot be read.
static Formula *formula_parse (Book &b, const char *s, int n, const char **why = 0, int *where = 0)
{
	if (n < 0) n = (int) strlen (s);
	Lexer L; L.b = &b; L.s = s; L.n = n; L.i = 0; L.tok = 0; L.nt = L.ct = 0; L.err = 0; L.errPos = 0; L.brace = 0;
	Formula *f = (Formula *) calloc (1, sizeof (Formula));
	if (!L.run ())
	{
		if (why) *why = L.err;
		if (where) *where = L.errPos;
		free (L.tok); free (f);
		return 0;
	}
	f->tok = L.tok; f->nt = L.nt;
	f->npool = L.pool.n; f->pool = L.pool.take ();
	Parser P; P.f = f; P.p = 0; P.cn = P.ca = 0; P.err = 0; P.errTok = 0;
	int root = P.expr ();
	if (root >= 0 && P.peek () >= 0) root = P.fail (P.peek () == TK_RP ? "A ( is missing." : "Something follows the formula's end.");
	if (root < 0)
	{
		if (why) *why = P.err;
		if (where) *where = P.errTok < f->nt ? f->tok[P.errTok].at : n;
		formula_free (f);
		return 0;
	}
	f->root = root;
	return f;
}

// ---- printing -------------------------------------------------------------------------------------------
static bool sheet_name_plain (const char *nm)		// written without quotes?
{
	if (!nm[0]) return false;
	if (nm[0] >= '0' && nm[0] <= '9') return false;
	for (const char *p = nm; *p; p++)
	{
		unsigned char c = (unsigned char) *p;
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80)) return false;
	}
	int r, c;
	if (parse_cell_name (nm, &r, &c)) return false;		// ("AB12" would read as a cell)
	if (ci_eq (nm, "TRUE") || ci_eq (nm, "FALSE")) return false;
	return true;
}
static void put_sheet_name (Buf &o, const char *nm)
{
	if (sheet_name_plain (nm)) { o.puts (nm); return; }
	o.put ('\'');
	for (const char *p = nm; *p; p++) { if (*p == '\'') o.put ('\''); o.put (*p); }
	o.put ('\'');
}
static void put_ref_part (Buf &o, int r, int c, bool ar, bool ac, bool rowPart, bool colPart)
{
	char t[16];
	if (colPart) { if (ac) o.put ('$'); col_name (c, t); o.puts (t); }
	if (rowPart) { if (ar) o.put ('$'); snprintf (t, sizeof t, "%d", r + 1); o.puts (t); }
}
static void put_ref (Book &b, Buf &o, const Tok &k)
{
	if (k.fl & TF_SHEET)
	{
		Sheet *s = k.sheet > 0 ? book_sheet_by_id (b, k.sheet) : 0;
		if (!s) { o.puts ("#REF!"); return; }
		put_sheet_name (o, s->name);
		o.put ('!');
	}
	if (k.fl & TF_BAD) { o.puts ("#REF!"); return; }
	if (k.t == TK_REF) { put_ref_part (o, k.r0, k.c0, k.fl & TF_AR0, k.fl & TF_AC0, true, true); return; }
	bool rows = k.fl & TF_ROWS, cols = k.fl & TF_COLS;
	put_ref_part (o, k.r0, k.c0, k.fl & TF_AR0, k.fl & TF_AC0, !cols, !rows);
	o.put (':');
	put_ref_part (o, k.r1, k.c1, k.fl & TF_AR1, k.fl & TF_AC1, !cols, !rows);
}
// The formula as text, "=" first (xlsx: no "=", the newer functions named "_xlfn.X").
static void formula_print (Book &b, const Formula *f, Buf &o, bool xlsx = false)
{
	if (!xlsx) o.put ('=');
	for (int i = 0; i < f->nt; i++)
	{
		const Tok &k = f->tok[i];
		switch (k.t)
		{
		case TK_NUM: { char t[40]; num_full (k.num, t, sizeof t); o.puts (t); break; }
		case TK_STR:
			o.put ('"');
			for (int j = 0; j < k.sn; j++) { char c = f->pool[k.s + j]; if (c == '"') o.put ('"'); o.put (c); }
			o.put ('"');
			break;
		case TK_BOOL: o.puts (k.num ? "TRUE" : "FALSE"); break;
		case TK_ERR: o.puts (ERR_NAMES[(int) k.num]); break;
		case TK_REF: case TK_AREA: put_ref (b, o, k); break;
		case TK_NAME: o.putn (f->pool + k.s, k.sn); break;
		case TK_FUNC:
			if (k.fn >= 0)
			{
				const char *nm = fn_name (k.fn);
				if (xlsx)
				{
					static const char *const XLFN[] = { "IFS", "SWITCH", "CONCAT", "TEXTJOIN", "XLOOKUP", "MAXIFS", "MINIFS", "IFNA",
						"DAYS", "ISOWEEKNUM", "STDEV.S", "STDEV.P", "VAR.S", "VAR.P", "XOR", "SHEET", "SHEETS", "FORMULATEXT",
						"CEILING.MATH", "FLOOR.MATH", "RANK.EQ", "MODE.SNGL", "PERCENTILE.INC", "QUARTILE.INC", "NORM.DIST",
						"NORM.INV", "NORM.S.DIST", "NORM.S.INV", "AGGREGATE", 0 };
					for (int x = 0; XLFN[x]; x++) if (!strcmp (XLFN[x], nm)) { o.puts ("_xlfn."); break; }
				}
				o.puts (nm);
			}
			else o.putn (f->pool + k.s, k.sn);
			break;
		case TK_OP: o.puts (OP_TEXT[k.op]); break;
		case TK_LP: o.put ('('); break;
		case TK_RP: o.put (')'); break;
		case TK_SEP: o.put (','); break;
		case TK_ALP: o.put ('{'); break;
		case TK_ARP: o.put ('}'); break;
		case TK_ACOL: o.put (','); break;
		case TK_AROW: o.put (';'); break;
		case TK_SPACE: o.putn (f->pool + k.s, k.sn); break;
		}
	}
}

// ---- moving references ------------------------------------------------------------------------------------
static Formula *formula_dup (const Formula *f)
{
	Formula *g = (Formula *) malloc (sizeof (Formula));
	*g = *f;
	g->tok = (Tok *) malloc (imax (1, f->nt) * sizeof (Tok)); memcpy (g->tok, f->tok, f->nt * sizeof (Tok));
	g->nd = (Node *) malloc (imax (1, f->nn) * sizeof (Node)); memcpy (g->nd, f->nd, f->nn * sizeof (Node));
	g->args = (int *) malloc (imax (1, f->na) * sizeof (int)); if (f->na) memcpy (g->args, f->args, f->na * sizeof (int));
	g->pool = (char *) malloc (imax (1, f->npool)); if (f->npool) memcpy (g->pool, f->pool, f->npool);
	return g;
}

// Copied dr rows, dc columns away: the relative parts move (off the sheet: #REF!).
static Formula *formula_copy (const Formula *f, int dr, int dc)
{
	Formula *g = formula_dup (f);
	for (int i = 0; i < g->nt; i++)
	{
		Tok &k = g->tok[i];
		if ((k.t != TK_REF && k.t != TK_AREA) || (k.fl & TF_BAD)) continue;
		bool cols = k.fl & TF_COLS, rows = k.fl & TF_ROWS;
		if (!cols) { if (!(k.fl & TF_AR0)) k.r0 += dr; if (!(k.fl & TF_AR1) && k.t == TK_AREA) k.r1 += dr; }
		if (!rows) { if (!(k.fl & TF_AC0)) k.c0 += dc; if (!(k.fl & TF_AC1) && k.t == TK_AREA) k.c1 += dc; }
		if (k.t == TK_REF) { k.r1 = k.r0; k.c1 = k.c0; }
		if (k.r0 < 0 || k.c0 < 0 || k.r1 >= MAXR || k.c1 >= MAXC || k.r1 < 0 || k.c1 < 0 || k.r0 >= MAXR || k.c0 >= MAXC) k.fl |= TF_BAD;
		if (k.t == TK_AREA && !(k.fl & TF_BAD))
		{
			if (k.r0 > k.r1) { int t = k.r0; k.r0 = k.r1; k.r1 = t; }
			if (k.c0 > k.c1) { int t = k.c0; k.c0 = k.c1; k.c1 = t; }
		}
	}
	return g;
}

// Rows (rows = true) or columns inserted (n > 0) at `at`, or deleted (n < 0: [at, at - n)), in the sheet
// whose id is `target`; the formula lives in the sheet `own`. Returns whether it changed.
static bool formula_shift (Formula *f, int own, int target, bool rows, int at, int n)
{
	bool changed = false;
	for (int i = 0; i < f->nt; i++)
	{
		Tok &k = f->tok[i];
		if ((k.t != TK_REF && k.t != TK_AREA) || (k.fl & TF_BAD)) continue;
		int sid = k.sheet ? k.sheet : own;
		if (sid != target) continue;
		if (rows ? (k.fl & TF_COLS) : (k.fl & TF_ROWS)) continue;	// (whole columns: rows do not move them)
		int &a = rows ? k.r0 : k.c0, &z = rows ? k.r1 : k.c1;
		int lim = rows ? MAXR : MAXC;
		int oa = a, oz = z;
		if (n > 0)
		{
			if (a >= at) a += n;
			if (z >= at) z += n;
			if (a >= lim) { k.fl |= TF_BAD; changed = true; continue; }
			if (z >= lim) z = lim - 1;
		}
		else
		{
			int d0 = at, d1 = at - n - 1;			// deleted [d0, d1]
			if (a >= d0 && z <= d1) { k.fl |= TF_BAD; changed = true; continue; }
			if (a > d1) a += n;
			else if (a >= d0) a = d0;
			if (z > d1) z += n;
			else if (z >= d0) z = d0 - 1;
		}
		if (k.t == TK_REF) { k.r1 = k.r0; k.c1 = k.c0; }
		if (a != oa || z != oz) changed = true;
	}
	return changed;
}

// Cells moved: the references to a range inside `src` (sheet id srcId) now point dr, dc away on the sheet
// dstId (a cut and a paste). Returns whether it changed.
static bool formula_move (Formula *f, int own, int srcId, Rect src, int dr, int dc, int dstId)
{
	bool changed = false;
	for (int i = 0; i < f->nt; i++)
	{
		Tok &k = f->tok[i];
		if ((k.t != TK_REF && k.t != TK_AREA) || (k.fl & TF_BAD)) continue;
		int sid = k.sheet ? k.sheet : own;
		if (sid != srcId) continue;
		if (k.r0 < src.r0 || k.r1 > src.r1 || k.c0 < src.c0 || k.c1 > src.c1) continue;
		k.r0 += dr; k.r1 += dr; k.c0 += dc; k.c1 += dc;
		if (dstId != sid) { k.sheet = dstId; k.fl |= TF_SHEET; }
		if (k.r0 < 0 || k.c0 < 0 || k.r1 >= MAXR || k.c1 >= MAXC) k.fl |= TF_BAD;
		changed = true;
	}
	return changed;
}

// A sheet deleted: the references naming it become #REF!.
static bool formula_drop_sheet (Formula *f, int own, int id)
{
	bool changed = false;
	for (int i = 0; i < f->nt; i++)
	{
		Tok &k = f->tok[i];
		if ((k.t == TK_REF || k.t == TK_AREA) && (k.sheet ? k.sheet : own) == id && !(k.fl & TF_BAD)) { k.fl |= TF_BAD; changed = true; }
	}
	return changed;
}

} // namespace ss

#endif
