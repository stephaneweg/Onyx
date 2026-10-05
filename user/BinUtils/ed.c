//
// ed -- the line editor: edits a text file from the console, one command a line.
//   usage: ed [-p prompt] [file]
//
// The text is a list of numbered lines; one of them is the current line ("."). A command is
//   [address[,address]]command[parameters]
// Addresses: N (line N), . (the current line), $ (the last), +N / -N (relative to .),
//   /re/ (the next line matching, wrapping around) and ?re? (the previous one), 'x (the line
//   marked x); "," alone is 1,$ (the whole text) and ";" is .,$ .
// Commands (the default lines in brackets):
//   a  i  c      add lines after [.], insert before [.], change [.,.]: type the lines, then a
//                line holding only "." ends the input
//   d            delete [.,.]                 j   join [.,.+1] into one line
//   p  n  l      print [.,.]: as is, with line numbers, showing tabs and the line's end
//   s/re/new/    substitute on [.,.] (flags g, a number N, p; & and \1..\9 in new); s alone
//                repeats the last one
//   m N   t N    move, copy [.,.] after line N       k x  mark [.] as x (a..z)
//   g/re/cmd     run cmd on every line matching [1,$];  v/re/cmd  on those that do not
//   r file       read a file after [$]         w file   write [1,$] (W: add to the file)
//   e file       edit another file (E: even if this one is not saved)      f file  its name
//   =            print [$]'s number            u   undo the last command
//   q            quit (Q: even if not saved)   wq  write and quit
//   h            explain the last "?"          H   explain every "?" from now on
//   an address alone (or an empty line): go to that line (the next one) and print it
// An error prints "?" (and what is wrong after H). After a change, q asks once ("?"): q again
// quits without saving.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "ed"
#include "tool.h"
#include "subst.h"

static char **g_ln; static long g_n, g_cap;	// the lines, g_ln[1..g_n] (each its own block)
static long   g_cur;				// "."
static int    g_dirty, g_warned, g_verbose;
static char   g_file[256];
static const char *g_errmsg = "";
static const char *g_prompt = "";
static struct t_file *g_in;
static char  *g_mark[26];			// 'a .. 'z: the marked line's text block
static char  *g_re, *g_sre, *g_srepl; static int g_sg, g_snth = 1, g_sp;	// the last /re/ and s

// undo: the text before the last command (the blocks are shared with the live text: a line is
// never changed in place, so a block is freed only when neither list holds it -- they are not:
// an editing session's lines are small, and the process's memory goes with it)
static char **u_ln; static long u_n, u_cur; static int u_ok;

static int fail (const char *msg)
{
	g_errmsg = msg;
	t_puts ("?\n");
	if (g_verbose) { t_puts (msg); t_putc ('\n'); }
	return -1;
}

static void reserve (long n)
{
	if (n + 2 <= g_cap) return;
	g_cap = (n + 2) * 2 + 64;
	g_ln = (char **) t_realloc (g_ln, g_cap * (long) sizeof *g_ln);
}
static void insert (long after, char *text)		// a line after line `after`
{
	reserve (g_n + 1);
	for (long i = g_n; i > after; i--) g_ln[i + 1] = g_ln[i];
	g_ln[after + 1] = text;
	g_n++;
}
static void delete (long a, long b)
{
	long k = b - a + 1;
	for (long i = b + 1; i <= g_n; i++) g_ln[i - k] = g_ln[i];
	g_n -= k;
}
static void snapshot (void)
{
	t_free (u_ln);
	u_ln = (char **) t_malloc ((g_n + 2) * (long) sizeof *u_ln);
	for (long i = 1; i <= g_n; i++) u_ln[i] = g_ln[i];
	u_n = g_n; u_cur = g_cur; u_ok = 1;
}

static long read_file (const char *path, long after)	// -> bytes read, -1: cannot open
{
	long len, bytes = 0;
	char *b = t_slurp (path, &len);
	if (b == 0) return -1;
	long cnt; char **v = t_lines (b, len, &cnt, 0);
	for (long i = 0; i < cnt; i++) insert (after + i, t_strdup (v[i]));
	bytes = len;
	g_cur = after + cnt;
	t_free (v); t_free (b);
	return bytes;
}

static long write_file (const char *path, long a, long b, int append)
{
	struct t_sb sb = { 0, 0, 0 };
	sb_reset (&sb);
	for (long i = a; i <= b; i++) { sb_put (&sb, g_ln[i], t_strlen (g_ln[i])); sb_putc (&sb, '\n'); }
	int ok;
	if (append)
	{
		void *h = t_create (path, 1);
		ok = h != 0;
		if (ok) { t_fwrite (h, sb.p, sb.n); t_fclose (h); }
	}
	else ok = t_save (path, sb.p, sb.n);
	long n = sb.n;
	t_free (sb.p);
	return ok ? n : -1;
}

// ---- addresses --------------------------------------------------------------------

static int search (const char *re, int back, long *out)
{
	struct re_match m;
	if (g_n == 0) return 0;
	long i = g_cur;
	for (long k = 0; k < g_n; k++)
	{
		i = back ? i - 1 : i + 1;
		if (i < 1) i = g_n;
		if (i > g_n) i = 1;
		const char *s = g_ln[i];
		if (re_search (re, s, s + t_strlen (s), s, 0, &m)) { *out = i; return 1; }
	}
	return 0;
}

// one address at *pp -> 1 and *v, 0: none there, -1: an error (reported)
static int address (const char **pp, long *v)
{
	const char *s = *pp;
	long a = 0; int have = 0;
	while (t_isblank (*s)) s++;
	if (t_isdigit (*s)) { t_number (&s, &a); have = 1; }
	else if (*s == '.') { a = g_cur; s++; have = 1; }
	else if (*s == '$') { a = g_n; s++; have = 1; }
	else if (*s == '\'')
	{
		if (s[1] < 'a' || s[1] > 'z' || g_mark[s[1] - 'a'] == 0) return fail ("invalid mark");
		char *want = g_mark[s[1] - 'a'];
		for (a = 1; a <= g_n && g_ln[a] != want; a++) { }
		if (a > g_n) return fail ("the marked line is gone");
		s += 2; have = 1;
	}
	else if (*s == '/' || *s == '?')
	{
		char d = *s++;
		char *re = re_piece (&s, d, 0);
		if (re[0]) { g_re = re; }
		else if (g_re == 0) return fail ("no previous pattern");
		if (!search (g_re, d == '?', &a)) return fail ("no match");
		have = 1;
	}
	for (;;)						// +N -N + -
	{
		while (t_isblank (*s)) s++;
		if (*s != '+' && *s != '-') break;
		int neg = *s++ == '-'; long k = 1;
		if (!have) { a = g_cur; have = 1; }
		if (t_isdigit (*s)) t_number (&s, &k);
		a += neg ? -k : k;
	}
	*pp = s;
	if (!have) return 0;
	if (a < 0 || a > g_n) return fail ("invalid address");
	*v = a;
	return 1;
}

// ---- commands -------------------------------------------------------------------------

static int command (const char *s, int global);

// the lines typed until a "." line, inserted after `after` -> how many, -1 at the input's end
static long input_lines (long after)
{
	long n = 0, len; char *s;
	while ((s = t_getline (g_in, &len)) != 0)
	{
		if (len == 1 && s[0] == '.') break;
		insert (after + n, t_strndup (s, len)); n++;
	}
	g_cur = after + n;
	return n;
}

static void print (long a, long b, char how)
{
	for (long i = a; i <= b; i++)
	{
		if (how == 'n') { t_putnum (i); t_putc ('\t'); }
		if (how == 'l')
		{
			for (const char *p = g_ln[i]; *p; p++)
			{
				if (*p == '\t') t_puts ("\\t");
				else if (*p == '\\') t_puts ("\\\\");
				else if ((unsigned char) *p < 32) { t_putc ('^'); t_putc ((char) (*p + 64)); }
				else t_putc (*p);
			}
			t_putc ('$');
		}
		else t_puts (g_ln[i]);
		t_putc ('\n');
	}
	g_cur = b;
}

static int substitute (const char *s, long a, long b)
{
	if (*s && *s != '\n')
	{
		char d = *s++; int closed;
		if (d == ' ' || d == '\\') return fail ("invalid delimiter");
		char *re = re_piece (&s, d, &closed);
		if (!closed) return fail ("unterminated s command");
		char *repl = re_piece (&s, d, &closed);
		if (re[0]) g_re = re;
		else if (g_re == 0) return fail ("no previous pattern");
		g_sre = g_re; g_srepl = repl; g_sg = 0; g_snth = 1; g_sp = 0;
		for (; *s; s++)
		{
			if (*s == 'g') g_sg = 1;
			else if (*s == 'p') g_sp = 1;
			else if (t_isdigit (*s)) { long v = 1; t_number (&s, &v); g_snth = (int) v; s--; }
			else return fail ("invalid s flag");
		}
		if (!closed) g_sp = 1;				// (s/a/b: prints, as ed does)
	}
	else if (g_sre == 0) return fail ("no previous substitution");
	int any = 0;
	struct t_sb out = { 0, 0, 0 };
	for (long i = a; i <= b; i++)
	{
		sb_reset (&out);
		if (!re_subst (g_sre, g_srepl, 0, g_sg, g_snth, g_ln[i], t_strlen (g_ln[i]), &out)) continue;
		// (a \n in the replacement splits the line)
		long at = i; char *p = out.p;
		delete (i, i); at--;
		for (;;)
		{
			char *e = p;
			while (*e && *e != '\n') e++;
			insert (at, t_strndup (p, e - p)); at++;
			if (*e == '\0') break;
			p = e + 1; b++;
		}
		i = at; g_cur = at; any = 1;
	}
	t_free (out.p);
	if (!any) return fail ("no match");
	g_dirty = 1;
	if (g_sp) print (g_cur, g_cur, 'p');
	return 0;
}

// g/re/cmd and v/re/cmd: the matching lines are noted first (by their blocks), then cmd runs
// on each one that is still there
static int global_cmd (const char *s, long a, long b, int inv)
{
	char d = *s; int closed;
	if (d == '\0' || d == ' ') return fail ("invalid delimiter");
	s++;
	char *re = re_piece (&s, d, &closed);
	if (re[0]) g_re = re;
	else if (g_re == 0) return fail ("no previous pattern");
	if (*s == '\0') s = "p";
	long cnt = 0;
	char **sel = (char **) t_malloc ((b - a + 2) * (long) sizeof *sel);
	struct re_match m;
	for (long i = a; i <= b; i++)
	{
		const char *t = g_ln[i];
		if (re_search (g_re, t, t + t_strlen (t), t, 0, &m) != inv) sel[cnt++] = g_ln[i];
	}
	int rc = 0;
	for (long k = 0; k < cnt && rc == 0; k++)
	{
		long i;
		for (i = 1; i <= g_n && g_ln[i] != sel[k]; i++) { }
		if (i > g_n) continue;
		g_cur = i;
		if (command (s, 1) < 0) rc = -1;
	}
	t_free (sel);
	return rc;
}

static const char *filename (const char *s)
{
	while (t_isblank (*s)) s++;
	if (*s)
	{
		int k = 0;
		for (; s[k] && k < (int) sizeof g_file - 1; k++) g_file[k] = s[k];
		while (k > 0 && t_isblank (g_file[k - 1])) k--;
		g_file[k] = '\0';
	}
	return g_file;
}

// One command line -> 0, -1 an error (reported), 1 quit.
static int command (const char *s, int global)
{
	long a = 0, b = 0; int na = 0, r;
	while (t_isblank (*s)) s++;
	if (*s == ',' || *s == ';')				// , = 1,$   ; = .,$
	{
		a = *s == ',' ? 1 : g_cur; b = g_n; na = 2; s++;
		if (g_n == 0) a = 0;
		long v;
		if ((r = address (&s, &v)) < 0) return -1;
		if (r) b = v;
	}
	else
	{
		if ((r = address (&s, &a)) < 0) return -1;
		if (r)
		{
			na = 1; b = a;
			while (t_isblank (*s)) s++;
			if (*s == ',' || *s == ';')
			{
				if (*s == ';') g_cur = a;
				s++;
				if ((r = address (&s, &b)) < 0) return -1;
				if (!r) b = g_n;
				na = 2;
			}
		}
	}
	if (na == 2 && a > b) return fail ("invalid address");
	while (t_isblank (*s)) s++;
	char c = *s ? *s++ : '\0';

#define DEF(x, y)	do { if (na == 0) { a = (x); b = (y); } } while (0)
#define NEED()		do { if (a < 1 || b > g_n || a > b) return fail ("invalid address"); } while (0)

	if (!global && c != '\0')				// (a command that changes the text: u undoes it)
		for (const char *m = "aicdjsmtgvr"; *m; m++) if (*m == c) snapshot ();
	if (c != 'q' && c != 'e') g_warned = 0;

	switch (c)
	{
	case '\0':						// an address alone / an empty line
		if (na == 0) { a = b = g_cur + 1; }
		if (b < 1 || b > g_n) return fail ("invalid address");
		print (b, b, 'p');
		return 0;
	case 'a': DEF (g_cur, g_cur); if (input_lines (b) > 0) g_dirty = 1; return 0;
	case 'i': DEF (g_cur, g_cur); if (input_lines (b > 0 ? b - 1 : 0) > 0) g_dirty = 1; return 0;
	case 'c':
		DEF (g_cur, g_cur); NEED ();
		delete (a, b); g_dirty = 1;
		input_lines (a - 1);
		return 0;
	case 'd':
		DEF (g_cur, g_cur); NEED ();
		delete (a, b); g_dirty = 1;
		g_cur = a <= g_n ? a : g_n;
		return 0;
	case 'j':
	{
		DEF (g_cur, g_cur + 1); NEED ();
		if (a == b) return 0;
		struct t_sb sb = { 0, 0, 0 };
		for (long i = a; i <= b; i++) sb_put (&sb, g_ln[i], t_strlen (g_ln[i]));
		delete (a, b); insert (a - 1, sb.p);
		g_cur = a; g_dirty = 1;
		return 0;
	}
	case 'p': case 'n': case 'l': DEF (g_cur, g_cur); NEED (); print (a, b, c); return 0;
	case 's': DEF (g_cur, g_cur); NEED (); return substitute (s, a, b);
	case 'm': case 't':
	{
		DEF (g_cur, g_cur); NEED ();
		long to;
		if ((r = address (&s, &to)) < 0) return -1;
		if (!r) to = g_cur;
		long k = b - a + 1;
		char **blk = (char **) t_malloc (k * (long) sizeof *blk);
		for (long i = 0; i < k; i++) blk[i] = c == 't' ? t_strdup (g_ln[a + i]) : g_ln[a + i];
		if (c == 'm')
		{
			if (to >= a && to < b) { t_free (blk); return fail ("invalid destination"); }
			delete (a, b);
			if (to >= b) to -= k;
		}
		for (long i = 0; i < k; i++) insert (to + i, blk[i]);
		t_free (blk);
		g_cur = to + k; g_dirty = 1;
		return 0;
	}
	case 'k':
		DEF (g_cur, g_cur); NEED ();
		if (*s < 'a' || *s > 'z') return fail ("invalid mark");
		g_mark[*s - 'a'] = g_ln[b];
		return 0;
	case 'g': case 'v':
		if (global) return fail ("g inside g");
		DEF (1, g_n);
		if (g_n == 0) return 0;
		NEED ();
		return global_cmd (s, a, b, c == 'v');
	case 'r':
	{
		DEF (g_n, g_n);
		char keep[sizeof g_file]; t_memcpy (keep, g_file, sizeof keep);
		const char *f = filename (s);
		if (f[0] == '\0') return fail ("no file name");
		long n = read_file (f, b);
		if (keep[0]) t_memcpy (g_file, keep, sizeof keep);	// (r does not rename the text)
		if (n < 0) return fail ("cannot open the file");
		t_putnum (n); t_putc ('\n');
		g_dirty = 1;
		return 0;
	}
	case 'w': case 'W':
	{
		int quit = 0;
		if (*s == 'q') { quit = 1; s++; }
		DEF (1, g_n);
		if (g_n > 0) NEED ();
		const char *f = filename (s);
		if (f[0] == '\0') return fail ("no file name");
		long n = write_file (f, a, g_n > 0 ? b : 0, c == 'W');
		if (n < 0) return fail ("cannot write the file");
		t_putnum (n); t_putc ('\n');
		if (na == 0) g_dirty = 0;
		return quit;
	}
	case 'e': case 'E':
	{
		if (c == 'e' && g_dirty && !g_warned) { g_warned = 1; return fail ("the text is not saved (e again to drop it)"); }
		const char *f = filename (s);
		if (f[0] == '\0') return fail ("no file name");
		g_n = 0; g_cur = 0; g_dirty = 0; u_ok = 0;
		long n = read_file (f, 0);
		if (n < 0) return fail ("cannot open the file");
		t_putnum (n); t_putc ('\n');
		return 0;
	}
	case 'f': t_puts (filename (s)); t_putc ('\n'); return 0;
	case '=': DEF (g_n, g_n); t_putnum (b); t_putc ('\n'); return 0;
	case 'u':
	{
		if (!u_ok) return fail ("nothing to undo");
		char **l = (char **) t_malloc ((g_n + 2) * (long) sizeof *l);
		long n = g_n, cur = g_cur;
		for (long i = 1; i <= g_n; i++) l[i] = g_ln[i];
		reserve (u_n);
		for (long i = 1; i <= u_n; i++) g_ln[i] = u_ln[i];
		g_n = u_n; g_cur = u_cur;
		t_free (u_ln); u_ln = l; u_n = n; u_cur = cur;		// (u again: redo)
		g_dirty = 1;
		return 0;
	}
	case 'q':
		if (g_dirty && !g_warned) { g_warned = 1; return fail ("the text is not saved (q again to quit)"); }
		return 1;
	case 'Q': return 1;
	case 'h': t_puts (g_errmsg[0] ? g_errmsg : "no error"); t_putc ('\n'); return 0;
	case 'H': g_verbose = !g_verbose; return 0;
	default:  return fail ("unknown command");
	}
}

int tool_main (int argc, char **argv)
{
	int a = 1;
	if (a + 1 < argc && t_streq (argv[a], "-p")) { g_prompt = argv[a + 1]; a += 2; }
	reserve (0);
	if (a < argc)
	{
		filename (argv[a]);
		long n = read_file (g_file, 0);
		if (n < 0) { t_puts (g_file); t_puts (": a new file\n"); }
		else { t_putnum (n); t_putc ('\n'); }
	}
	g_in = t_open (0);
	for (;;)
	{
		t_puts (g_prompt);
		long len;
		char *s = t_getline (g_in, &len);
		if (s == 0) break;
		char *line = t_strndup (s, len);		// (the input commands read more lines)
		int r = command (line, 0);
		if (r > 0) break;
		t_flush ();
	}
	return 0;
}
