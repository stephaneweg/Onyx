//
// sed -- the stream editor: applies a script to each line of its input.
//   usage: sed [-n] [-i] [-e script]... [script] [file ...]
// -n: print only what p asks for; -i: rewrite each file in place (instead of printing);
// several -e scripts are joined. No file: stdin.
//
// A script is commands separated by ; or line feeds, each  [address[,address]][!]command :
//   addresses   N (line N), $ (the last line), /re/ (the lines matching: regex.h), a pair
//               "first,last" (a range); ! after them: the lines NOT selected
//   s/re/new/flags   substitute (flags: g every match, a number N the Nth one, p print the
//               line if changed, i ignore case; another delimiter than / may be used;
//               in new: & the match, \1..\9 the groups, \n a line feed)
//   d  delete the line          p  print it          q  quit (after this line)
//   =  print its number         y/abc/xyz/  replace each character of abc by its match in xyz
//   a text  add a line after    i text  insert one before    c text  replace the line(s)
// Not there: the hold space (h g x), n N D, labels and branches, { } blocks, w files.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "sed"
#include "tool.h"
#include "subst.h"

struct addr { int type; long n; char *re; };		// type: 0 none, 1 a line, 2 the last, 3 /re/

struct cmd
{
	struct addr a1, a2;
	int   neg, active;				// !; inside its range
	char  c;
	char *re, *repl;				// s (and y: the two sets)
	int   g, p, icase, nth;
	char *text;					// a i c
};

static struct cmd *g_cmd; static int g_ncmd, g_capcmd;
static int o_quiet, o_inplace;
static char *g_lastre;

static void bad (const char *what, const char *at)
{
	char b[48]; int n = 0;
	for (; at && at[n] && at[n] != '\n' && n < 40; n++) b[n] = at[n];
	b[n] = '\0';
	t_err (what, at ? b : 0);
	t_exit (2);
}

static const char *parse_addr (const char *s, struct addr *a)
{
	a->type = 0; a->re = 0; a->n = 0;
	if (t_isdigit (*s)) { a->type = 1; t_number (&s, &a->n); }
	else if (*s == '$') { a->type = 2; s++; }
	else if (*s == '/')
	{
		int closed;
		s++;
		a->type = 3; a->re = re_piece (&s, '/', &closed);
		if (!closed) bad ("unterminated address", a->re);
		if (a->re[0] == '\0') { if (!g_lastre) bad ("no previous regular expression", 0); a->re = g_lastre; }
		else g_lastre = a->re;
	}
	return s;
}

static void parse (const char *s)
{
	for (;;)
	{
		while (t_isblank (*s) || *s == ';' || *s == '\n') s++;
		if (*s == '\0') return;
		if (*s == '#') { while (*s && *s != '\n') s++; continue; }
		if (g_ncmd >= g_capcmd) { g_capcmd = g_capcmd * 2 + 8; g_cmd = (struct cmd *) t_realloc (g_cmd, g_capcmd * (long) sizeof *g_cmd); }
		struct cmd *c = &g_cmd[g_ncmd++];
		c->neg = c->active = c->g = c->p = c->icase = 0; c->nth = 1; c->re = c->repl = c->text = 0;
		s = parse_addr (s, &c->a1);
		c->a2.type = 0;
		if (c->a1.type && *s == ',')
		{
			s = parse_addr (s + 1, &c->a2);
			if (!c->a2.type) bad ("an address is expected after ,", s);
		}
		while (t_isblank (*s)) s++;
		if (*s == '!') { c->neg = 1; s++; while (t_isblank (*s)) s++; }
		c->c = *s;
		if (*s == '\0') bad ("a command is missing", 0);
		s++;
		switch (c->c)
		{
		case 's': case 'y':
		{
			char d = *s; int closed;
			if (d == '\0' || d == '\n' || d == '\\') bad ("unterminated command", s - 1);
			s++;
			c->re = re_piece (&s, d, &closed);
			if (!closed) bad ("unterminated command", c->re);
			c->repl = re_piece (&s, d, &closed);
			if (!closed) bad ("unterminated command", c->repl);
			if (c->c == 'y')
			{
				if (t_strlen (c->re) != t_strlen (c->repl)) bad ("y: the two sets differ in length", c->re);
				break;
			}
			if (c->re[0] == '\0') { if (!g_lastre) bad ("no previous regular expression", 0); c->re = g_lastre; }
			else g_lastre = c->re;
			for (;; s++)
			{
				if (*s == 'g') c->g = 1;
				else if (*s == 'p') c->p = 1;
				else if (*s == 'i' || *s == 'I') c->icase = 1;
				else if (t_isdigit (*s)) { long v = 1; t_number (&s, &v); c->nth = (int) v; s--; }
				else break;
			}
			break;
		}
		case 'a': case 'i': case 'c':
		{
			while (t_isblank (*s)) s++;
			if (*s == '\\') { s++; if (*s == '\n') s++; }
			const char *e = s;
			while (*e && *e != '\n') e++;
			c->text = t_strndup (s, e - s);
			s = e;
			break;
		}
		case 'd': case 'p': case 'q': case '=': break;
		default: bad ("unknown command", s - 1);
		}
		while (t_isblank (*s)) s++;
		if (*s && *s != ';' && *s != '\n' && *s != '#') bad ("extra characters after a command", s);
	}
}

static int match_addr (const struct addr *a, long lineno, int last, const char *ps, long len)
{
	struct re_match m;
	switch (a->type)
	{
	case 1:  return lineno == a->n;
	case 2:  return last;
	case 3:  return re_search (a->re, ps, ps + len, ps, 0, &m);
	default: return 1;
	}
}

// One file (or stdin) through the script -> 1 when q asked to stop.
static int run (struct t_file *f)
{
	struct t_sb ps = { 0, 0, 0 }, next = { 0, 0, 0 }, tmp = { 0, 0, 0 }, app = { 0, 0, 0 };
	long len, lineno = 0;
	int quit = 0;
	char *s = t_getline (f, &len);
	int have = s != 0, nl = f->nl;
	if (have) { sb_reset (&ps); sb_put (&ps, s, len); }
	while (have && !quit)
	{
		lineno++;
		int this_nl = nl;
		s = t_getline (f, &len);				// (one line ahead: is this the last one?)
		int more = s != 0;
		if (more) { sb_reset (&next); sb_put (&next, s, len); nl = f->nl; }
		int last = !more, deleted = 0;
		sb_reset (&app);

		for (int i = 0; i < g_ncmd && !deleted; i++)
		{
			struct cmd *c = &g_cmd[i];
			int sel, range_end = 1;
			if (c->a2.type == 0) sel = match_addr (&c->a1, lineno, last, ps.p, ps.n);
			else if (!c->active)
			{
				sel = match_addr (&c->a1, lineno, last, ps.p, ps.n);
				if (sel && !(c->a2.type == 1 && c->a2.n <= lineno)) { c->active = 1; range_end = 0; }
			}
			else
			{
				sel = 1;
				if (c->a2.type == 1 ? lineno >= c->a2.n : match_addr (&c->a2, lineno, last, ps.p, ps.n)) c->active = 0;
				else range_end = 0;
			}
			if (last) range_end = 1;
			if (sel == c->neg) continue;
			switch (c->c)
			{
			case 's':
				sb_reset (&tmp);
				if (re_subst (c->re, c->repl, c->icase, c->g, c->nth, ps.p, ps.n, &tmp))
				{
					struct t_sb sw = ps; ps = tmp; tmp = sw;
					if (c->p) { t_put (ps.p, ps.n); t_putc ('\n'); }
				}
				break;
			case 'y':
				for (long k = 0; k < ps.n; k++)
					for (int j = 0; c->re[j]; j++)
						if (ps.p[k] == c->re[j]) { ps.p[k] = c->repl[j]; break; }
				break;
			case 'd': deleted = 1; break;
			case 'p': t_put (ps.p, ps.n); t_putc ('\n'); break;
			case '=': t_putnum (lineno); t_putc ('\n'); break;
			case 'q': quit = 1; break;
			case 'a': sb_put (&app, c->text, t_strlen (c->text)); sb_putc (&app, '\n'); break;
			case 'i': t_puts (c->text); t_putc ('\n'); break;
			case 'c':
				if (c->neg || range_end) { t_puts (c->text); t_putc ('\n'); }
				deleted = 1;
				break;
			}
			if (quit) break;
		}
		if (!deleted && !o_quiet)
		{
			t_put (ps.p, ps.n);
			if (this_nl || app.n) t_putc ('\n');
		}
		t_put (app.p, app.n);
		if (f->is_stdin) t_flush ();
		have = more;
		if (more) { struct t_sb sw = ps; ps = next; next = sw; }
	}
	t_free (ps.p); t_free (next.p); t_free (tmp.p); t_free (app.p);
	return quit;
}

int tool_main (int argc, char **argv)
{
	struct t_sb script = { 0, 0, 0 };
	int a = 1, have_e = 0;
	for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++)
	{
		if (t_streq (argv[a], "--")) { a++; break; }
		if (t_streq (argv[a], "-n")) o_quiet = 1;
		else if (t_streq (argv[a], "-i")) o_inplace = 1;
		else if (t_streq (argv[a], "-e") && a + 1 < argc)
		{
			a++; have_e = 1;
			sb_put (&script, argv[a], t_strlen (argv[a])); sb_putc (&script, '\n');
		}
		else { t_err ("unknown option", argv[a]); return 2; }
	}
	if (!have_e)
	{
		if (a >= argc) { t_puts ("usage: sed [-n] [-i] [-e script]... [script] [file ...]\n"); return 2; }
		sb_put (&script, argv[a], t_strlen (argv[a])); a++;
	}
	parse (script.p);

	if (a >= argc)
	{
		if (o_inplace) { t_err ("-i needs a file", 0); return 2; }
		struct t_file *f = t_open (0);
		run (f);
		t_close (f);
		return 0;
	}
	int rc = 0;
	for (int i = a; i < argc; i++)
	{
		struct t_file *f = t_open (argv[i]);
		if (f == 0) { t_err ("cannot open", argv[i]); rc = 1; continue; }
		if (o_inplace) t_capture ();
		int quit = run (f);
		t_close (f);
		if (o_inplace)
		{
			long n; char *b = t_captured (&n);
			if (!t_save (argv[i], b, n)) { t_err ("cannot write", argv[i]); rc = 1; }
			t_free (b);
			for (int k = 0; k < g_ncmd; k++) g_cmd[k].active = 0;
		}
		else if (quit) break;
	}
	return rc;
}
