//
// cmdscript.h -- cmd's script language, above the parser (cmdparse.h): variables and what
// replaces a $, arithmetic, `test`, and the blocks (if / while / for). No kapi: the shell gives
// its side through struct CsHost, and the host test (tools/tests/cmd/cmdscript_test.c) another.
// The includer defines CS_MALLOC (n) and CS_FREE (p) first.
//
// The language (docs/04 "Scripts"):
//
//   name=value          a variable (the value: the rest of the line, its $ replaced, its quotes
//                       removed). Variables are handed to the programs and scripts started.
//   $name ${name}       its value (nothing when it is not set)
//   $1 .. $9  $0  $#  $*  $?     the script's arguments, its name, their count, all of them, the
//                       exit code of the last command
//   $(command)          what the command prints (its last line feeds removed)
//   $((expression))     arithmetic on whole numbers: + - * / % ( ) and the comparisons
//                       == != < <= > >=, && || !, & | ^ ~ << >>; a name is a variable's value
//   Nothing is replaced inside '...'; inside "..." it is, and the result stays one word; \$ is a
//   plain $. Outside quotes a value's blanks separate words (and its * ? are file patterns).
//
//   if <command>        runs the lines up to else / elif / fi when the command's exit code is 0
//   elif <command>      (any command: `test`, `[ ... ]`, grep -q, a script...)
//   else
//   fi
//   while <command>     runs the lines up to done as long as the command's exit code is 0
//   until <command>     ... as long as it is NOT 0
//   for name in words   runs them once per word (after $ and file patterns: for f in *.txt)
//   done
//   break  continue     leave the loop / go to its next turn
//   ! command           the exit code inverted (0 <-> 1)
//   A keyword starts its own line; `; then` and `; do` may end an if / while / for line (a line
//   holding only `then` or `do` is fine too).
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
// hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
// do so, subject to the following conditions: The above copyright notice and this permission
// notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
// IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef _bin_cmdscript_h
#define _bin_cmdscript_h

#include "cmdparse.h"

#define CS_NAME		64		// a variable's name, its '\0' included

struct CmdVars
{
	int argc;			// the script's arguments: argv[0] its name, argv[1] = $1 ...
	const char *const *argv;
	int status;			// $?
	const char *(*get) (const char *name);			// a variable's value, or 0
	// $(command): what it prints, into out (cap bytes, '\0' included) -> its length, -1: too long
	int (*run) (const char *command, char *out, int cap);
};

static inline int cs_namestart (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static inline int cs_namechar (char c) { return cs_namestart (c) || (c >= '0' && c <= '9'); }
static inline int cs_streq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

// ---- arithmetic -----------------------------------------------------------------------

struct cs__ar { const char *p; const struct CmdVars *v; int err; };

static inline void cs__ws (struct cs__ar *a) { while (*a->p == ' ' || *a->p == '\t' || *a->p == '\n') a->p++; }

// a number as text: decimal (a sign first) or 0x...; the rest ignored
static inline long long cs_number (const char *s)
{
	long long r = 0; int neg = 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '-' || *s == '+') neg = *s++ == '-';
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		for (s += 2;; s++)
		{
			int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10
				: *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : -1;
			if (d < 0) break;
			r = r * 16 + d;
		}
	else while (*s >= '0' && *s <= '9') r = r * 10 + (*s++ - '0');
	return neg ? -r : r;
}

static inline long long cs__expr (struct cs__ar *a, int minprec, int depth);

static inline long long cs__primary (struct cs__ar *a, int depth)
{
	cs__ws (a);
	char c = *a->p;
	if (depth > 40) { a->err = 1; return 0; }
	if (c == '(')
	{
		a->p++;
		long long r = cs__expr (a, 1, depth + 1);
		cs__ws (a);
		if (*a->p == ')') a->p++; else a->err = 1;
		return r;
	}
	if (c == '-') { a->p++; return -cs__primary (a, depth + 1); }
	if (c == '+') { a->p++; return cs__primary (a, depth + 1); }
	if (c == '!') { a->p++; return !cs__primary (a, depth + 1); }
	if (c == '~') { a->p++; return ~cs__primary (a, depth + 1); }
	if (c >= '0' && c <= '9')
	{
		long long r = cs_number (a->p);
		if (a->p[0] == '0' && (a->p[1] == 'x' || a->p[1] == 'X')) a->p += 2;
		while (cs_namechar (*a->p)) a->p++;
		return r;
	}
	if (cs_namestart (c))
	{
		char name[CS_NAME]; int n = 0;
		while (cs_namechar (*a->p)) { if (n < CS_NAME - 1) name[n++] = *a->p; a->p++; }
		name[n] = '\0';
		const char *val = a->v && a->v->get ? a->v->get (name) : 0;
		return val ? cs_number (val) : 0;
	}
	a->err = 1;
	return 0;
}

// the binary operator at p -> its precedence (0: none) and length
static inline int cs__binop (const char *p, int *len)
{
	*len = 2;
	if (p[0] == '|' && p[1] == '|') return 1;
	if (p[0] == '&' && p[1] == '&') return 2;
	if (p[0] == '=' && p[1] == '=') return 6;
	if (p[0] == '!' && p[1] == '=') return 6;
	if (p[0] == '<' && p[1] == '=') return 7;
	if (p[0] == '>' && p[1] == '=') return 7;
	if (p[0] == '<' && p[1] == '<') return 8;
	if (p[0] == '>' && p[1] == '>') return 8;
	*len = 1;
	switch (p[0])
	{
	case '|': return 3;
	case '^': return 4;
	case '&': return 5;
	case '<': case '>': return 7;
	case '+': case '-': return 9;
	case '*': case '/': case '%': return 10;
	}
	return 0;
}

static inline long long cs__expr (struct cs__ar *a, int minprec, int depth)
{
	long long l = cs__primary (a, depth);
	for (;;)
	{
		cs__ws (a);
		int len, prec = cs__binop (a->p, &len);
		if (prec == 0 || prec < minprec || a->err) return l;
		char o0 = a->p[0], o1 = len == 2 ? a->p[1] : 0;
		a->p += len;
		long long r = cs__expr (a, prec + 1, depth + 1);
		unsigned long long ul = (unsigned long long) l, ur = (unsigned long long) r;
		switch (o0)
		{
		case '|': l = o1 ? (l || r) : (l | r); break;
		case '&': l = o1 ? (l && r) : (l & r); break;
		case '^': l ^= r; break;
		case '=': l = l == r; break;
		case '!': l = l != r; break;
		case '<': l = o1 == '=' ? l <= r : o1 == '<' ? (long long) (ul << (ur & 63)) : l < r; break;
		case '>': l = o1 == '=' ? l >= r : o1 == '>' ? l >> (ur & 63) : l > r; break;
		case '+': l = (long long) (ul + ur); break;
		case '-': l = (long long) (ul - ur); break;
		case '*': l = (long long) (ul * ur); break;
		case '/': case '%':
			if (r == 0) { a->err = 2; return 0; }
			if (r == -1) l = o0 == '/' ? (long long) (0 - ul) : 0;	// (no overflow trap)
			else l = o0 == '/' ? l / r : l % r;
			break;
		}
	}
}

// The expression's value -> 0, or -1 a syntax error, -2 a division by zero.
static inline int cs_arith (const char *text, const struct CmdVars *v, long long *out)
{
	struct cs__ar a = { text, v, 0 };
	*out = cs__expr (&a, 1, 0);
	cs__ws (&a);
	if (a.err == 2) return -2;
	return a.err || *a.p ? -1 : 0;
}

static inline int cs_ltoa (long long x, char *b)
{
	char t[24]; int tn = 0, k = 0;
	unsigned long long u = x < 0 ? 0ULL - (unsigned long long) x : (unsigned long long) x;
	do { t[tn++] = (char) ('0' + u % 10); u /= 10; } while (u);
	if (x < 0) b[k++] = '-';
	while (tn) b[k++] = t[--tn];
	b[k] = '\0';
	return k;
}

// ---- what replaces a $ ------------------------------------------------------------------

// A value put into the command being built, so that the parser reads it back as itself: inside
// "..." its " and \ escaped; outside quotes the characters the parser acts on escaped too (its
// blanks still separate words, its * ? are still file patterns), a line feed a blank.
static inline int cs__put (char *out, int cap, int o, const char *s, char q)
{
	for (; *s; s++)
	{
		char c = *s;
		int esc = q ? (c == '"' || c == '\\')
			    : (c == '"' || c == '\'' || c == '\\' || c == '<' || c == '>' || c == '|' || c == ';' || c == '&' || c == '#');
		if (!q && (c == '\n' || c == '\r')) c = ' ';
		if (o >= cap - 2) return -1;
		if (esc) out[o++] = '\\';
		out[o++] = c;
	}
	return o;
}

// the ) that closes the ( before s[i] (quotes and nested parentheses skipped), or -1
static inline int cs__close (const char *s, int i)
{
	int depth = 1; char q = 0;
	for (; s[i]; i++)
	{
		char c = s[i];
		if (q) { if (c == q) q = 0; else if (c == '\\' && q == '"' && s[i + 1]) i++; }
		else if (c == '"' || c == '\'') q = c;
		else if (c == '\\' && s[i + 1]) i++;
		else if (c == '(') depth++;
		else if (c == ')' && --depth == 0) return i;
	}
	return -1;
}

#define CS_ETOOLONG	(-1)
#define CS_ESYNTAX	(-2)		// $( without its ), a bad ${ }, a bad expression
#define CS_EDIVZERO	(-3)

static inline const char *cs_strerror (int e)
{
	return e == CS_ETOOLONG ? "line too long" : e == CS_EDIVZERO ? "division by zero" : "bad substitution";
}

// The command with its $ replaced -> its length, or CS_E*.
static inline int cmd_expand (const char *s, char *out, int cap, const struct CmdVars *v)
{
	int o = 0;
	char q = 0;
	for (int i = 0; s[i]; i++)
	{
		char c = s[i];
		if (q == '\'') { if (c == '\'') q = 0; }
		else if (c == '\\' && s[i + 1] == '$') { c = '$'; i++; }
		else if (c == '\\' && s[i + 1]) { if (o >= cap - 1) return CS_ETOOLONG; out[o++] = c; c = s[++i]; }
		else if (c == '"') q = q ? 0 : '"';
		else if (c == '\'' && !q) q = c;
		else if (c == '$')
		{
			char n = s[i + 1], num[24];
			const char *val = 0;
			if (n == '(')
			{
				int arith = s[i + 2] == '(';
				int end = cs__close (s, i + 2);
				if (end < 0 || (arith && (s[end - 1] != ')' || end - 1 < i + 3))) return CS_ESYNTAX;
				int from = i + (arith ? 3 : 2), to = arith ? end - 1 : end;
				char *in = (char *) CS_MALLOC (2 * (unsigned long) cap + (unsigned long) (to - from) + 1);
				if (in == 0) return CS_ETOOLONG;
				char *res = in + (to - from) + 1;
				for (int k = from; k < to; k++) in[k - from] = s[k];
				in[to - from] = '\0';
				int r;
				if (arith)
				{
					long long x;
					r = cmd_expand (in, res, cap, v);		// ($1, $(...) inside it)
					if (r >= 0)
					{
						// (the expansion's own escapes are not part of the expression)
						char *w = res;
						for (char *p = res; *p; p++) if (*p != '\\') *w++ = *p;
						*w = '\0';
						r = cs_arith (res, v, &x);
						r = r == -2 ? CS_EDIVZERO : r < 0 ? CS_ESYNTAX : 0;
						if (r == 0) cs_ltoa (x, res);
					}
				}
				else
				{
					r = v->run ? v->run (in, res, cap) : 0;
					if (r < 0) r = CS_ETOOLONG;
					else
					{
						if (v->run == 0) res[0] = '\0';
						int n2 = 0; while (res[n2]) n2++;
						while (n2 > 0 && (res[n2 - 1] == '\n' || res[n2 - 1] == '\r')) res[--n2] = '\0';
					}
				}
				if (r >= 0) { o = cs__put (out, cap, o, res, q); r = o < 0 ? CS_ETOOLONG : 0; }
				CS_FREE (in);
				if (r < 0) return r;
				i = end;
				continue;
			}
			if (n == '{' || cs_namestart (n))
			{
				char name[CS_NAME]; int k = 0, j = i + 1 + (n == '{');
				while (cs_namechar (s[j])) { if (k < CS_NAME - 1) name[k++] = s[j]; j++; }
				name[k] = '\0';
				if (n == '{') { if (s[j] != '}' || k == 0) return CS_ESYNTAX; j++; }
				val = v->get ? v->get (name) : 0;
				if (val && (o = cs__put (out, cap, o, val, q)) < 0) return CS_ETOOLONG;
				i = j - 1;
				continue;
			}
			if (n >= '0' && n <= '9')
			{
				if (n - '0' < v->argc && (o = cs__put (out, cap, o, v->argv[n - '0'], q)) < 0) return CS_ETOOLONG;
				i++; continue;
			}
			if (n == '#' || n == '?')
			{
				cs_ltoa (n == '#' ? (v->argc > 0 ? v->argc - 1 : 0) : v->status, num);
				if ((o = cs__put (out, cap, o, num, q)) < 0) return CS_ETOOLONG;
				i++; continue;
			}
			if (n == '*' || n == '@')
			{
				for (int a = 1; a < v->argc; a++)
				{
					if (a > 1) { if (o >= cap - 1) return CS_ETOOLONG; out[o++] = ' '; }
					if ((o = cs__put (out, cap, o, v->argv[a], q)) < 0) return CS_ETOOLONG;
				}
				i++; continue;
			}
		}
		if (o >= cap - 1) return CS_ETOOLONG;
		out[o++] = c;
	}
	out[o] = '\0';
	return o;
}

// ---- test ----------------------------------------------------------------------------------

// `test` / `[ ]`: argv = its arguments (without "test", "[" and the final "]"); kind (path) ->
// 0 nothing there, 1 a file, 2 a folder. -> 0 true, 1 false, 2 a syntax error.
//   -e path   it exists        -f path   a file          -d path   a folder
//   -z text   it is empty      -n text   it is not       text      it is not empty
//   a = b   a != b             the same text / not the same
//   a -eq b  -ne  -lt  -le  -gt  -ge      numbers compared
//   ! expression     not       e1 -a e2   and       e1 -o e2   or
static inline int cs_test (int argc, const char *const *argv, int (*kind) (const char *path))
{
	for (int i = 0; i < argc; i++)				// -o binds the loosest, then -a
		if (cs_streq (argv[i], "-o") && i > 0 && i < argc - 1)
		{
			int l = cs_test (i, argv, kind), r = cs_test (argc - i - 1, argv + i + 1, kind);
			return l == 2 || r == 2 ? 2 : (l == 0 || r == 0) ? 0 : 1;
		}
	for (int i = 0; i < argc; i++)
		if (cs_streq (argv[i], "-a") && i > 0 && i < argc - 1)
		{
			int l = cs_test (i, argv, kind), r = cs_test (argc - i - 1, argv + i + 1, kind);
			return l == 2 || r == 2 ? 2 : (l == 0 && r == 0) ? 0 : 1;
		}
	if (argc == 0) return 1;
	if (cs_streq (argv[0], "!") && argc > 1)
	{
		int r = cs_test (argc - 1, argv + 1, kind);
		return r == 2 ? 2 : !r;
	}
	if (argc == 1) return argv[0][0] ? 0 : 1;
	if (argc == 2)
	{
		const char *o = argv[0], *a = argv[1];
		if (cs_streq (o, "-z")) return a[0] ? 1 : 0;
		if (cs_streq (o, "-n")) return a[0] ? 0 : 1;
		int k = kind ? kind (a) : 0;
		if (cs_streq (o, "-e")) return k ? 0 : 1;
		if (cs_streq (o, "-f")) return k == 1 ? 0 : 1;
		if (cs_streq (o, "-d")) return k == 2 ? 0 : 1;
		return 2;
	}
	if (argc == 3)
	{
		const char *a = argv[0], *o = argv[1], *b = argv[2];
		if (cs_streq (o, "=") || cs_streq (o, "==")) return cs_streq (a, b) ? 0 : 1;
		if (cs_streq (o, "!=")) return cs_streq (a, b) ? 1 : 0;
		long long x = cs_number (a), y = cs_number (b);
		if (cs_streq (o, "-eq")) return x == y ? 0 : 1;
		if (cs_streq (o, "-ne")) return x != y ? 0 : 1;
		if (cs_streq (o, "-lt")) return x <  y ? 0 : 1;
		if (cs_streq (o, "-le")) return x <= y ? 0 : 1;
		if (cs_streq (o, "-gt")) return x >  y ? 0 : 1;
		if (cs_streq (o, "-ge")) return x >= y ? 0 : 1;
	}
	return 2;
}

// ---- lines and blocks ----------------------------------------------------------------------

struct CsHost
{
	struct CmdVars *vars;				// the arguments, $?, the variables, $(...)
	int  (*pipeline) (const char *command);		// one command (a pipeline), its $ replaced -> its exit code
	void (*set) (const char *name, const char *value);
	int  (*stop) (void);				// exit asked, or interrupted: nothing more is run
	void (*error) (const char *message);
};

// `name=value` at the start of the command -> the value's text (name filled), or 0
static inline const char *cs_assign (const char *s, char *name)
{
	int n = 0;
	while (cmd_blank (*s)) s++;
	if (!cs_namestart (*s)) return 0;
	while (cs_namechar (s[n])) n++;
	if (s[n] != '=' || n >= CS_NAME) return 0;
	for (int k = 0; k < n; k++) name[k] = s[k];
	name[n] = '\0';
	return s + n + 1;
}

// The words of a text whose $ are replaced already (quotes removed; file patterns when glob)
// -> a stage to CS_FREE, or 0 with the error reported.
static inline struct CmdStage *cs__words (struct CsHost *h, const char *text, int glob)
{
	struct CmdStage *st = (struct CmdStage *) CS_MALLOC (sizeof *st);
	const char *err;
	if (st == 0) { h->error ("out of memory"); return 0; }
	int (*hook) (const char *, char *, int, int *) = cmd_glob_hook;
	if (!glob) cmd_glob_hook = 0;
	int n = cmd_parse (text, st, 1, &err);
	cmd_glob_hook = hook;
	if (n < 0) { h->error (err); CS_FREE (st); return 0; }
	if (n == 0) { st->argc = st->len = 0; st->argv[0] = st->argv[1] = '\0'; }
	return st;
}

// One line: its commands (; && ||), each with its $ replaced; `name=value`; `! command`.
// -> the last exit code (also in h->vars->status).
static inline int cs_run_line (struct CsHost *h, const char *input)
{
	char *seg = (char *) CS_MALLOC (2 * CMD_LINE);
	if (seg == 0) { h->error ("out of memory"); return h->vars->status = 2; }
	char *exp = seg + CMD_LINE;
	int pos = 0, op = 0, prev = ';';
	while (!h->stop () && cmd_next (input, &pos, seg, CMD_LINE, &op))
	{
		int st = h->vars->status;
		int run = prev == ';' || (prev == '&' && st == 0) || (prev == '|' && st != 0);
		prev = op ? op : ';';
		if (!run) continue;
		const char *c = seg;
		int neg = 0;
		while (cmd_blank (*c)) c++;
		if (c[0] == '!' && (cmd_blank (c[1]) || c[1] == '\0')) { neg = 1; c++; while (cmd_blank (*c)) c++; }
		if (*c == '\0') { if (neg) h->vars->status = st == 0; continue; }
		char name[CS_NAME];
		const char *val = cs_assign (c, name);
		int r = cmd_expand (val ? val : c, exp, CMD_LINE, h->vars);
		if (r < 0) { h->error (cs_strerror (r)); h->vars->status = 2; continue; }
		if (h->stop ()) break;				// (a $(...) interrupted)
		if (val)
		{
			struct CmdStage *w = cs__words (h, exp, 0);
			if (w == 0) { h->vars->status = 2; continue; }
			for (int k = 0; k + 1 < w->len; k++) if (w->argv[k] == '\0') w->argv[k] = ' ';	// (the words joined)
			h->set (name, w->argv);
			CS_FREE (w);
			st = 0;
		}
		else st = h->pipeline (exp);
		h->vars->status = neg ? st == 0 : st;
	}
	CS_FREE (seg);
	return h->vars->status;
}

enum { CS_NONE, CS_IF, CS_ELIF, CS_ELSE, CS_FI, CS_WHILE, CS_UNTIL, CS_FOR, CS_DONE, CS_THEN, CS_DO, CS_BREAK, CS_CONTINUE };

// The keyword a line starts with (CS_NONE: an ordinary line); *rest: what follows it.
static inline int cs_keyword (const char *line, const char **rest)
{
	static const char *const kw[] = { "", "if", "elif", "else", "fi", "while", "until", "for", "done", "then", "do", "break", "continue" };
	while (cmd_blank (*line)) line++;
	for (int k = 1; k <= CS_CONTINUE; k++)
	{
		int n = 0;
		while (kw[k][n] && kw[k][n] == line[n]) n++;
		if (kw[k][n] == '\0' && (line[n] == '\0' || cmd_blank (line[n]) || line[n] == ';' || line[n] == '#' || line[n] == '\r'))
		{
			line += n;
			while (cmd_blank (*line)) line++;
			if (rest) *rest = line;
			return k;
		}
	}
	if (rest) *rest = line;
	return CS_NONE;
}

// what a line does to the depth of blocks: +1 it opens one, -1 it closes one
static inline int cs_depth (const char *line)
{
	int k = cs_keyword (line, 0);
	return k == CS_IF || k == CS_WHILE || k == CS_UNTIL || k == CS_FOR ? 1 : k == CS_FI || k == CS_DONE ? -1 : 0;
}

// an if / while / for line's text without its final `; then` / `; do` (a copy to CS_FREE)
static inline char *cs__head (const char *rest, const char *word)
{
	int n = 0, wl = 0;
	while (rest[n]) n++;
	while (word[wl]) wl++;
	char *c = (char *) CS_MALLOC ((unsigned long) n + 1);
	if (c == 0) return 0;
	for (int k = 0; k <= n; k++) c[k] = rest[k];
	while (n > 0 && (cmd_blank (c[n - 1]) || c[n - 1] == '\r')) n--;
	if (n > wl && (cmd_blank (c[n - wl - 1]) || c[n - wl - 1] == ';'))
	{
		int k = 0;
		while (k < wl && c[n - wl + k] == word[k]) k++;
		if (k == wl) n -= wl;
	}
	while (n > 0 && (cmd_blank (c[n - 1]) || c[n - 1] == ';')) n--;
	c[n] = '\0';
	return c;
}

// the line that closes the block opened at line i (fi / done), or -1
static inline int cs__end (char *const *lines, int i, int to)
{
	int depth = 0;
	for (int j = i + 1; j < to; j++)
	{
		int d = cs_depth (lines[j]);
		if (d > 0) depth++;
		else if (d < 0 && depth-- == 0) return j;
	}
	return -1;
}

#define CS_FLOW_BREAK		1
#define CS_FLOW_CONTINUE	2
#define CS_FLOW_ERROR		(-1)

// The lines [from, to) run -> 0, CS_FLOW_BREAK / CONTINUE (asked by a line, for the loop
// around), or CS_FLOW_ERROR (a block badly formed: reported, the script stops).
static inline int cs_exec (struct CsHost *h, char *const *lines, int from, int to)
{
	for (int i = from; i < to && !h->stop (); i++)
	{
		const char *rest;
		int k = cs_keyword (lines[i], &rest);
		switch (k)
		{
		case CS_NONE: cs_run_line (h, lines[i]); break;
		case CS_THEN: case CS_DO: break;
		case CS_BREAK: return CS_FLOW_BREAK;
		case CS_CONTINUE: return CS_FLOW_CONTINUE;
		case CS_IF:
		{
			int end = cs__end (lines, i, to), cur = i, taken = 0;
			if (end < 0 || cs_keyword (lines[end], 0) != CS_FI) { h->error ("if without its fi"); return CS_FLOW_ERROR; }
			while (cur < end)
			{
				int next = end, depth = 0;		// the next elif / else of THIS if
				for (int j = cur + 1; j < end; j++)
				{
					int kj = cs_keyword (lines[j], 0), d = cs_depth (lines[j]);
					if (depth == 0 && (kj == CS_ELIF || kj == CS_ELSE)) { next = j; break; }
					depth += d;
				}
				int kc = cs_keyword (lines[cur], &rest), yes = 0;
				if (!taken)
				{
					if (kc == CS_ELSE) yes = 1;
					else
					{
						char *cond = cs__head (rest, "then");
						if (cond == 0) { h->error ("out of memory"); return CS_FLOW_ERROR; }
						yes = cs_run_line (h, cond) == 0;
						CS_FREE (cond);
					}
				}
				if (yes && !h->stop ())
				{
					taken = 1;
					int f = cs_exec (h, lines, cur + 1, next);
					if (f) return f;
				}
				cur = next;
			}
			i = end;
			break;
		}
		case CS_WHILE: case CS_UNTIL:
		{
			int end = cs__end (lines, i, to);
			if (end < 0 || cs_keyword (lines[end], 0) != CS_DONE) { h->error ("while without its done"); return CS_FLOW_ERROR; }
			char *cond = cs__head (rest, "do");
			if (cond == 0) { h->error ("out of memory"); return CS_FLOW_ERROR; }
			while (!h->stop ())
			{
				int ok = cs_run_line (h, cond) == 0;
				if (h->stop () || ok == (k == CS_UNTIL)) break;
				int f = cs_exec (h, lines, i + 1, end);
				if (f == CS_FLOW_BREAK) break;
				if (f == CS_FLOW_ERROR) { CS_FREE (cond); return f; }
			}
			CS_FREE (cond);
			i = end;
			break;
		}
		case CS_FOR:
		{
			int end = cs__end (lines, i, to);
			if (end < 0 || cs_keyword (lines[end], 0) != CS_DONE) { h->error ("for without its done"); return CS_FLOW_ERROR; }
			char name[CS_NAME]; int n = 0;
			while (cs_namechar (rest[n]) && n < CS_NAME - 1) { name[n] = rest[n]; n++; }
			name[n] = '\0';
			const char *p = rest + n;
			while (cmd_blank (*p)) p++;
			if (n == 0 || p[0] != 'i' || p[1] != 'n' || (p[2] && !cmd_blank (p[2]) && p[2] != ';'))
			{ h->error ("for: `for name in words` expected"); return CS_FLOW_ERROR; }
			char *list = cs__head (p + 2, "do");
			char *exp = (char *) CS_MALLOC (CMD_LINE);
			if (list == 0 || exp == 0) { h->error ("out of memory"); return CS_FLOW_ERROR; }
			int r = cmd_expand (list, exp, CMD_LINE, h->vars);
			CS_FREE (list);
			struct CmdStage *w = r < 0 ? 0 : cs__words (h, exp, 1);
			if (r < 0) h->error (cs_strerror (r));
			CS_FREE (exp);
			if (w == 0) return CS_FLOW_ERROR;
			const char *word = w->argv;
			for (int a = 0; a < w->argc && !h->stop (); a++)
			{
				h->set (name, word);
				while (*word) word++;
				word++;
				int f = cs_exec (h, lines, i + 1, end);
				if (f == CS_FLOW_BREAK) break;
				if (f == CS_FLOW_ERROR) { CS_FREE (w); return f; }
			}
			CS_FREE (w);
			i = end;
			break;
		}
		default:
			h->error (k == CS_FI ? "fi without its if" : k == CS_DONE ? "done without its loop" : "else without its if");
			return CS_FLOW_ERROR;
		}
	}
	return 0;
}

// A text cut into its lines in place ('\n' -> '\0', a '\r' before it dropped) -> the lines (to
// CS_FREE) and their count, or 0.
static inline char **cs_lines (char *text, int *count)
{
	int n = 1;
	for (char *p = text; *p; p++) if (*p == '\n') n++;
	char **v = (char **) CS_MALLOC ((unsigned long) n * sizeof *v);
	if (v == 0) return 0;
	n = 0;
	char *p = text;
	while (*p)
	{
		v[n++] = p;
		while (*p && *p != '\n') p++;
		if (p > text && p[-1] == '\r') p[-1] = '\0';
		if (*p) *p++ = '\0';
	}
	*count = n;
	return v;
}

#endif
