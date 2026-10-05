//
// cmd -- the shell, as an ordinary /bin console program. It reads command lines from
// stdin (the terminal feeds the keyboard there) and writes the prompt + output to
// stdout (the terminal displays it). A line is a list of commands (; && ||: cmdparse.h); for
// each it builds a pipeline of stages (split on '|') with redirections (< > >>) -- quotes and
// backslash escapes as in cmdparse.h --, spawns /bin/<cmd> for each with its exact argv, wires
// the stages together, forwards its own stdin to the first stage (so interactive
// programs read the keyboard; Ctrl-D ends that input, Ctrl-C stops the programs), and drains
// the last stage's output to stdout.
//
// The script language (cmdscript.h; docs/04 "Scripts"): variables (name=value, $name), the
// script's arguments ($1 .. $9, $#, $*), $? (the last exit code), $(command), $((arithmetic)),
// file patterns (*.txt), if / elif / else / fi, while / until / for ... done, break, continue.
// Builtins: cd, pwd, clear, exit, source (.), test ([ ]), echo, read, set, unset, shift, true,
// false.
//
// `cmd file [args]` runs a script and exits (the keyboard still goes to the programs it starts);
// `cmd -c "line"` runs one line; `source file [args]` runs a file inside this shell (its cd and
// its variables stay); a command word ending with .sh is a script, run by a cmd of its own
// (looked for in the current folder, then in SD:/bin). Without arguments: the interactive shell,
// until stdin's end (a block typed at the prompt is read up to its fi / done, then run).
//
#include "appkit/appkit.h"
#include "applib.h"
#include "umm.h"
#define CS_MALLOC(n)	umm_malloc ((unsigned long) (n))
#define CS_FREE(p)	umm_free (p)
#include "cmdscript.h"

#define SCRIPT_DEPTH	8		// source inside source ...

static struct CmdStage g_stage[CMD_MAXSTAGES];
static int g_exit = 0, g_exitcode = 0;
static int g_abort = 0;			// Ctrl-C: the rest of the line / of the scripts is dropped
static int g_depth = 0;
static struct CmdVars g_vars;		// the running script's arguments, $?, the variables
static struct CsHost g_host;
static const char **g_args;		// g_vars.argv, ours to shift

// ---- output: stdout, or the text of a $(command) ----------------------------------------

static struct { char *buf; int n, cap, on, over; } g_cap;
static void *g_fout;			// a builtin's `> file`

static void emit (const void *b, unsigned n)
{
	if (g_fout) { kapi_stream_write (g_fout, b, n); return; }
	if (!g_cap.on) { kapi_stdout_write (b, n); return; }
	for (unsigned k = 0; k < n; k++)
	{
		if (g_cap.n >= g_cap.cap) { g_cap.over = 1; return; }
		g_cap.buf[g_cap.n++] = ((const char *) b)[k];
	}
}
static void out (const char *s) { emit (s, (unsigned) ax_strlen (s)); }

// ---- input: the keyboard, read ahead between two commands to catch Ctrl-C --------------------

static char g_q[4096];			// typed while no program was reading: kept for the next reader
static int  g_qh, g_qn, g_ineof;

static void in_poll (void)
{
	void *sin = kapi_stdin ();
	char b[128]; int n;
	if (sin == 0 || g_ineof) return;
	while (g_qn <= (int) (sizeof g_q - sizeof b) && (n = kapi_stream_read_nb (sin, b, sizeof b)) >= 0)
	{
		if (n == 0) { g_ineof = 1; break; }
		for (int k = 0; k < n; k++)
		{
			if (b[k] == 3) { g_abort = 1; g_qn = 0; continue; }	// Ctrl-C: what was typed before it is dropped
			g_q[(g_qh + g_qn++) % (int) sizeof g_q] = b[k];
		}
	}
}
// what is waiting -> the bytes, 0: the input is over, -1: nothing yet
static int in_read_nb (char *b, int cap)
{
	int n = 0;
	while (g_qn > 0 && n < cap) { b[n++] = g_q[g_qh]; g_qh = (g_qh + 1) % (int) sizeof g_q; g_qn--; }
	if (n > 0) return n;
	if (g_ineof) return 0;
	void *sin = kapi_stdin ();
	if (sin == 0) return -1;
	n = kapi_stream_read_nb (sin, b, (unsigned) cap);
	if (n == 0) g_ineof = 1;
	return n;
}
// the next byte, waited for; -1: the input is over
static int in_getc (void)
{
	char c;
	if (g_qn > 0) { c = g_q[g_qh]; g_qh = (g_qh + 1) % (int) sizeof g_q; g_qn--; return (unsigned char) c; }
	if (g_ineof) return -1;
	if (kapi_stdin_read (&c, 1) <= 0) { g_ineof = 1; return -1; }
	return (unsigned char) c;
}

// ---- variables ---------------------------------------------------------------------------

struct Var { char *name, *value; };
static struct Var *g_var; static int g_nvar, g_capvar;

static char *dup (const char *s)
{
	int n = ax_strlen (s);
	char *d = (char *) umm_malloc ((unsigned long) n + 1);
	if (d) for (int k = 0; k <= n; k++) d[k] = s[k];
	return d;
}
static const char *var_get (const char *name)
{
	for (int i = 0; i < g_nvar; i++) if (ax_streq (g_var[i].name, name)) return g_var[i].value;
	return 0;
}
static void var_set (const char *name, const char *value)
{
	char *v = dup (value);
	if (v == 0) return;
	for (int i = 0; i < g_nvar; i++)
		if (ax_streq (g_var[i].name, name)) { umm_free (g_var[i].value); g_var[i].value = v; return; }
	if (g_nvar >= g_capvar)
	{
		int cap = g_capvar * 2 + 16;
		struct Var *nv = (struct Var *) umm_realloc (g_var, (unsigned long) cap * sizeof *nv);
		if (nv == 0) { umm_free (v); return; }
		g_var = nv; g_capvar = cap;
	}
	g_var[g_nvar].name = dup (name); g_var[g_nvar].value = v;
	if (g_var[g_nvar].name) g_nvar++; else umm_free (v);
}
static void var_unset (const char *name)
{
	for (int i = 0; i < g_nvar; i++)
		if (ax_streq (g_var[i].name, name))
		{
			umm_free (g_var[i].name); umm_free (g_var[i].value);
			g_var[i] = g_var[--g_nvar];
			return;
		}
}
// the variables as an environment block ("NAME=value\0...\0") for a program started, or 0
static char *env_block (void)
{
	unsigned long n = 2;
	if (g_nvar == 0) return 0;
	for (int i = 0; i < g_nvar; i++) n += (unsigned long) ax_strlen (g_var[i].name) + (unsigned long) ax_strlen (g_var[i].value) + 2;
	char *b = (char *) umm_malloc (n), *p = b;
	if (b == 0) return 0;
	for (int i = 0; i < g_nvar; i++)
	{
		for (const char *s = g_var[i].name; *s; s++) *p++ = *s;
		*p++ = '=';
		for (const char *s = g_var[i].value; *s; s++) *p++ = *s;
		*p++ = '\0';
	}
	*p++ = '\0'; *p = '\0';
	return b;
}
// the environment this cmd was started with: its first variables
static void env_load (void)
{
	int n = kapi_get_env (0, 0);
	if (n <= 2) return;
	char *b = (char *) umm_malloc ((unsigned long) n + 2);
	if (b == 0) return;
	n = kapi_get_env (b, (unsigned) n);
	if (n > 0)
	{
		b[n] = b[n + 1] = '\0';
		for (char *p = b; *p; )
		{
			char *e = p; int len = ax_strlen (p);
			while (*e && *e != '=') e++;
			if (*e == '=' && e > p) { *e = '\0'; var_set (p, e + 1); }
			p += len + 1;
		}
	}
	umm_free (b);
}

// ---- file patterns and files -----------------------------------------------------------------

static int lower (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int wild (const char *p, const char *s)			// * ? ; upper / lower case alike
{
	for (; *p; p++, s++)
	{
		if (*p == '*')
		{
			while (p[1] == '*') p++;
			for (;; s++)
			{
				if (wild (p + 1, s)) return 1;
				if (*s == '\0') return 0;
			}
		}
		if (*s == '\0') return 0;
		if (*p != '?' && lower ((unsigned char) *p) != lower ((unsigned char) *s)) return 0;
	}
	return *s == '\0';
}
static int casecmp (const char *a, const char *b)
{
	while (*a && lower ((unsigned char) *a) == lower ((unsigned char) *b)) { a++; b++; }
	return lower ((unsigned char) *a) - lower ((unsigned char) *b);
}

// cmd_glob_hook: the names that match the pattern's last part, in its folder, sorted
static int glob (const char *pat, char *outb, int cap, int *count)
{
	char dir[CMD_PATH]; int dl = 0;
	const char *name = pat;
	*count = 0;
	for (const char *p = pat; *p; p++) if (*p == '/' || *p == ':') name = p + 1;
	dl = (int) (name - pat);
	if (dl >= CMD_PATH) return 0;
	for (int k = 0; k < dl; k++) { if (pat[k] == '*' || pat[k] == '?') return 0; dir[k] = pat[k]; }
	dir[dl] = '\0';
	void *d = kapi_opendir (dl ? dir : ".");
	if (d == 0) return 0;

	char *names = (char *) umm_malloc ((unsigned long) cap + 1);
	const char **v = (const char **) umm_malloc (1024 * sizeof *v);
	int used = 0, n = 0;
	if (names && v)
		for (;;)
		{
			struct kapi_dirent2 e2; struct kapi_dirent e1;
			const char *nm;
			int r = kapi_dir_read (d, &e2);
			if (r == -KAPI_ENOSYS) { if (!kapi_readdir (d, &e1)) break; nm = e1.name; }
			else { if (r <= 0) break; nm = e2.name; }
			if (ax_streq (nm, ".") || ax_streq (nm, "..") || !wild (name, nm)) continue;
			int len = dl + ax_strlen (nm) + 1;
			if (used + len > cap || n >= 1024) { n = 0; break; }	// (too many: the pattern stays as written)
			v[n++] = names + used;
			for (int k = 0; k < dl; k++) names[used++] = dir[k];
			for (int k = 0; nm[k]; k++) names[used++] = nm[k];
			names[used++] = '\0';
		}
	kapi_closedir (d);
	int o = 0;
	if (names && v)
	{
		for (int i = 1; i < n; i++)				// sorted (an insertion sort: a folder's names)
		{
			const char *x = v[i]; int j = i;
			while (j > 0 && casecmp (v[j - 1], x) > 0) { v[j] = v[j - 1]; j--; }
			v[j] = x;
		}
		for (int i = 0; i < n; i++) { for (const char *s = v[i]; *s; s++) outb[o++] = *s; outb[o++] = '\0'; }
		*count = n;
	}
	umm_free (v); umm_free (names);
	return o;
}

// -> 0 nothing there, 1 a file, 2 a folder
static int file_kind (const char *path)
{
	struct kapi_stat st;
	int r = kapi_path_stat (path, &st);
	if (r == 0) return (st.mode & KAPI_S_IFDIR) ? 2 : 1;
	if (r != -KAPI_ENOSYS) return 0;
	void *d = kapi_opendir (path);				// (a kernel before v75)
	if (d) { kapi_closedir (d); return 2; }
	void *f = kapi_open (path);
	if (f) kapi_close (f);
	return f != 0;
}

static int ends_with (const char *s, const char *suf)
{
	int n = ax_strlen (s), m = ax_strlen (suf);
	return n > m && ax_streq (s + n - m, suf);
}

// ---- running ---------------------------------------------------------------------------------

static int run_script (const char *path, int argc, const char *const *argv);

// A stage spawned: spawn_ex with its exact argv block (argv[0] the program's path; `first`, when
// set, before the stage's own arguments: the script a cmd is started for) and the variables as
// its environment, so the child gets the words as cmd split them; a kernel without it (< v75):
// kapi_spawn with the words joined by blanks, a word with a blank in double quotes (the kernel
// splits that again). -> the process handle, or 0 (*nf: the program was not found).
static void *spawn_stage (const char *bin, const char *first, const struct CmdStage *st, void *in, void *outs, int *nf)
{
	static char blk[CMD_ARGV + 600];
	int p = 0;
	for (int k = 0; bin[k]; k++) blk[p++] = bin[k];
	blk[p++] = '\0';
	if (first) { for (int k = 0; first[k]; k++) blk[p++] = first[k]; blk[p++] = '\0'; }
	const char *rest = st->argv + ax_strlen (st->argv) + 1;		// (after the command word)
	int nrest = st->len - (int) (rest - st->argv);
	for (int k = 0; k < nrest; k++) blk[p++] = rest[k];
	blk[p++] = '\0';
	struct kapi_spawn_attr a = { 0 };
	char *env = env_block ();
	a.path = bin; a.argv = blk; a.envp = env; a.in = in; a.out = outs;
	long long h = kapi_spawn_ex (&a);
	umm_free (env);
	*nf = h == -KAPI_ENOENT;
	if (h > 0) return (void *) (unsigned long) h;
	if (h != -KAPI_ENOSYS) return 0;

	static char line[1024];					// (the child keeps 1023)
	int o = 0;
	for (int i = first ? 0 : 1; i < st->argc; i++)
	{
		const char *w = i == 0 ? first : cmd_arg (st, i);
		int q = 0;
		for (int k = 0; w[k]; k++) if (cmd_blank (w[k])) q = 1;
		if (o > 0 && o < (int) sizeof line - 1) line[o++] = ' ';
		if (q && o < (int) sizeof line - 1) line[o++] = '"';
		for (int k = 0; w[k] && o < (int) sizeof line - 1; k++) line[o++] = w[k];
		if (q && o < (int) sizeof line - 1) line[o++] = '"';
	}
	line[o] = '\0';
	void *pr = kapi_spawn (bin, line, in, outs);
	*nf = pr == 0;
	return pr;
}

static void prompt (void)
{
	char cwd[256]; kapi_getcwd (cwd, sizeof cwd);
	out (cwd); out (" $ ");
}

// A command the shell runs itself -> its exit code, or -1: not one (a program).
static int builtin (const struct CmdStage *st)
{
	const char *c = st->argv;
	const char *av[64]; int ac = 0;
	for (const char *p = c; ac < 64 && ac < st->argc; p += ax_strlen (p) + 1) av[ac++] = p;

	if (ax_streq (c, "clear")) { emit ("\f", 1); return 0; }	// (form-feed: the terminal clears on it)
	if (ax_streq (c, "true"))  return 0;
	if (ax_streq (c, "false")) return 1;
	if (ax_streq (c, "exit"))
	{
		g_exit = 1; g_exitcode = g_vars.status;
		if (ac > 1) g_exitcode = (int) cs_number (av[1]);
		return g_exitcode;
	}
	if (ax_streq (c, "pwd"))   { char w[256]; kapi_getcwd (w, sizeof w); out (w); out ("\n"); return 0; }
	if (ax_streq (c, "cd"))
	{
		const char *d = ac > 1 ? av[1] : "SD:/";
		if (!kapi_chdir (d)) { out ("cd: no such directory: "); out (d); out ("\n"); return 1; }
		return 0;
	}
	if (ax_streq (c, "test"))
	{
		int r = st->argc > 64 ? 2 : cs_test (ac - 1, av + 1, file_kind);
		if (r == 2) out ("test: bad expression\n");
		return r;
	}
	if (ax_streq (c, "["))
	{
		if (st->argc > 64 || !ax_streq (av[ac - 1], "]")) { out ("[: the closing ] is missing\n"); return 2; }
		int r = cs_test (ac - 2, av + 1, file_kind);
		if (r == 2) out ("[: bad expression\n");
		return r;
	}
	if (ax_streq (c, "echo"))
	{
		const char *p = c + ax_strlen (c) + 1;
		int nl = 1, first = 1;
		if (st->argc > 1 && ax_streq (p, "-n")) { nl = 0; p += 3; }
		for (; *p; p += ax_strlen (p) + 1)
		{
			if (!first) emit (" ", 1);
			out (p); first = 0;
		}
		if (nl) emit ("\n", 1);
		return 0;
	}
	if (ax_streq (c, "set"))
	{
		if (ac == 1)
		{
			for (int i = 0; i < g_nvar; i++) { out (g_var[i].name); out ("="); out (g_var[i].value); out ("\n"); }
			return 0;
		}
		out ("usage: set (lists the variables; name=value sets one, unset name removes it)\n");
		return 2;
	}
	if (ax_streq (c, "unset")) { for (int i = 1; i < ac; i++) var_unset (av[i]); return 0; }
	if (ax_streq (c, "shift"))
	{
		int n = ac > 1 ? (int) cs_number (av[1]) : 1;
		if (n < 0 || n > g_vars.argc - 1 || g_args == 0) return 1;
		for (int k = 1; k + n < g_vars.argc; k++) g_args[k] = g_args[k + n];
		g_vars.argc -= n;
		return 0;
	}
	if (ax_streq (c, "read"))			// read [name]: a line typed (or of the input) into the variable
	{
		static char line[CMD_LINE];
		int n = 0, ch;
		while ((ch = in_getc ()) >= 0 && ch != '\n')
		{
			if (ch == 3) { g_abort = 1; return 130; }
			if (ch == 4) { ch = -1; break; }
			if (ch != '\r' && n < (int) sizeof line - 1) line[n++] = (char) ch;
		}
		line[n] = '\0';
		if (ac > 1) var_set (av[1], line);
		return ch < 0 && n == 0 ? 1 : 0;
	}
	if (ax_streq (c, "source") || ax_streq (c, "."))
	{
		if (ac < 2) { out ("usage: source <script> [arguments]\n"); return 2; }
		// (its words copied: the script's own lines are parsed into g_stage)
		char *blk = (char *) umm_malloc ((unsigned long) st->len + 2);
		const char **argv = (const char **) umm_malloc ((unsigned long) st->argc * sizeof *argv);
		if (blk == 0 || argv == 0) { out ("cmd: out of memory\n"); return 2; }
		for (int k = 0; k <= st->len; k++) blk[k] = st->argv[k];
		int argc = 0;
		const char *p = blk + ax_strlen (blk) + 1;
		for (int k = 1; k < st->argc; k++) { argv[argc++] = p; p += ax_strlen (p) + 1; }
		int r = run_script (argv[0], argc, argv);
		umm_free (argv); umm_free (blk);
		return r;
	}
	return -1;
}

// One pipeline (a command, or commands joined by |), its $ replaced -> its exit code (the last
// stage's).
static int run_pipeline (const char *input)
{
	const char *err;
	int ns = cmd_parse (input, g_stage, CMD_MAXSTAGES, &err);
	if (ns < 0) { out ("cmd: "); out (err); out ("\n"); return 2; }
	if (ns == 0) return g_vars.status;
	if (ns == 1)
	{
		const struct CmdStage *st = &g_stage[0];
		void *fo = 0;
		if (st->outfile[0] && (ax_streq (st->argv, "echo") || ax_streq (st->argv, "pwd") || ax_streq (st->argv, "set")))
		{
			fo = kapi_file_out (st->outfile, st->append);		// a builtin's output into a file
			if (!fo) { out ("cannot open output file\n"); return 1; }
		}
		g_fout = fo;
		int r = builtin (st);
		g_fout = 0;
		if (fo) kapi_stream_close (fo);
		if (r >= 0) return r;
	}

	void *proc[CMD_MAXSTAGES]; int nproc = 0;
	int is_cmd[CMD_MAXSTAGES];			// the stage is a cmd (a script)
	void *owned[CMD_MAXSTAGES * 2 + 2]; int nowned = 0;
	void *cin = 0, *pout = 0, *prev = 0; int failed = 0, status = 0;

	for (int s = 0; s < ns; s++)
	{
		void *sin, *sout;
		if (s == 0)
		{
			if (g_stage[0].infile[0])
			{
				sin = kapi_file_in (g_stage[0].infile);
				if (!sin) { out ("cannot open input file\n"); failed = 1; break; }
				owned[nowned++] = sin;
			}
			else { cin = kapi_pipe (); sin = cin; owned[nowned++] = cin; }	// forward keyboard here
		}
		else sin = prev;

		if (s == ns - 1)
		{
			if (g_stage[s].outfile[0])
			{
				sout = kapi_file_out (g_stage[s].outfile, g_stage[s].append);
				if (!sout) { out ("cannot open output file\n"); failed = 1; break; }
				owned[nowned++] = sout;
			}
			else { pout = kapi_pipe (); sout = pout; owned[nowned++] = pout; }	// drained to our stdout
		}
		else { sout = kapi_pipe (); owned[nowned++] = sout; prev = sout; }

		const char *word = g_stage[s].argv;
		char bin[300], script[300]; int p = 0;
		const char *pre = "SD:/bin/", *first = 0;
		for (int k = 0; pre[k]; k++) bin[p++] = pre[k];
		for (int k = 0; word[k] && p < (int) sizeof bin - 1; k++) bin[p++] = word[k];
		bin[p] = '\0';
		if (ends_with (word, ".sh"))			// a script: a cmd of its own runs it
		{
			int has_dir = 0, k = 0;
			for (int j = 0; word[j]; j++) if (word[j] == '/' || word[j] == ':') has_dir = 1;
			const char *src = has_dir || file_kind (word) == 1 ? word : bin;
			for (; src[k] && k < (int) sizeof script - 1; k++) script[k] = src[k];
			script[k] = '\0';
			if (file_kind (script) != 1)
			{
				out (word); out (": script not found\n");
				status = 127; failed = 1; break;
			}
			first = script;
			const char *sh = "SD:/bin/cmd";
			for (k = 0; sh[k]; k++) bin[k] = sh[k];
			bin[k] = '\0';
		}

		int nf = 0;
		void *pr = spawn_stage (bin, first, &g_stage[s], sin, sout, &nf);
		if (!pr)
		{
			out (word); out (nf ? ": command not found\n" : ": cannot start\n");
			status = nf ? 127 : 126; failed = 1; break;
		}
		is_cmd[nproc] = ax_streq (bin, "SD:/bin/cmd");
		proc[nproc++] = pr;
	}
	if (failed && status == 0) status = 1;

	// Pump: forward our stdin to the first stage (Ctrl-D ends it), drain the last
	// stage's output to our stdout, until every stage has finished. Ctrl-C (or a stage that
	// could not start: the others would wait for it for ever): handed to the first stage, then
	// the stages still there are terminated -- the programs after 40 ms, a cmd running a script
	// after 2 s only (it stops what IT started, then ends by itself).
	char b[256]; int n, intr = -1;			// intr: ticks since the interruption
	int in_done = 0;
	if (failed && nproc > 0) { intr = 0; if (cin) kapi_stream_write (cin, "\x03", 1); }
	while (nproc > 0)
	{
		if (!in_done)
		{
			while ((n = in_read_nb (b, sizeof b)) > 0)
				for (int k = 0; k < n; k++)
				{
					if (b[k] == 3)
					{
						if (cin) kapi_stream_write (cin, &b[k], 1);
						g_abort = 1;
						if (intr < 0) intr = 0;
					}
					else if (!cin) { }
					// Ctrl-D: the end of the first stage's input (a cmd gets the
					// key itself: it ends the input of what IT runs)
					else if (b[k] == 4 && !is_cmd[0]) kapi_stream_eof (cin);
					else kapi_stream_write (cin, &b[k], 1);
				}
			// our own input is over (a script's cmd whose caller got Ctrl-D, a session
			// that is gone): so is the first stage's
			if (n == 0) { if (cin) kapi_stream_eof (cin); in_done = 1; }
		}
		if (pout)
			while ((n = kapi_stream_read_nb (pout, b, sizeof b)) > 0) emit (b, (unsigned) n);

		int alldone = 1;
		for (int k = 0; k < nproc; k++) if (!kapi_proc_done (proc[k])) alldone = 0;
		if (alldone)
		{
			if (pout) while ((n = kapi_stream_read_nb (pout, b, sizeof b)) > 0) emit (b, (unsigned) n);
			break;
		}
		if (intr >= 0 && ++intr % 5 == 0)
			for (int k = 0; k < nproc; k++)
			{
				struct kapi_proc_status ps;
				if (kapi_proc_done (proc[k]) || intr < (is_cmd[k] ? 250 : 5)) continue;
				if (kapi_proc_wait (proc[k], KAPI_WAIT_NOHANG | KAPI_WAIT_KEEP, &ps) == 0 && ps.pid > 0)
					kapi_kill_pid (ps.pid, 1);
			}
		kapi_msleep (8);
	}

	for (int k = 0; k < nproc; k++)
	{
		int r = kapi_wait (proc[k]);
		if (!failed && k == nproc - 1) status = r;
	}
	for (int k = 0; k < nowned; k++) kapi_stream_close (owned[k]);	// pipes + files (NOT our stdin/out)
	if (g_abort) status = 130;
	return status;
}

// ---- the script language's side (struct CsHost) ----------------------------------------------

static int host_stop (void)
{
	if (!g_exit && !g_abort) in_poll ();		// (Ctrl-C between two commands: a loop of builtins)
	return g_exit || g_abort;
}
static void host_error (const char *m) { out ("cmd: "); out (m); out ("\n"); }

// $(command): what it prints
static int host_capture (const char *command, char *outb, int cap)
{
	__typeof__ (g_cap) saved = g_cap;
	g_cap.buf = outb; g_cap.n = 0; g_cap.cap = cap - 1; g_cap.on = 1; g_cap.over = 0;
	cs_run_line (&g_host, command);
	outb[g_cap.n] = '\0';
	int n = g_cap.over ? -1 : g_cap.n;
	g_cap = saved;
	return n;
}

// A text's lines run (its blocks: if, while, for); the text is cut in place.
static void run_text (char *text)
{
	int n = 0;
	char **lines = cs_lines (text, &n);
	if (lines == 0) { out ("cmd: out of memory\n"); return; }
	cs_exec (&g_host, lines, 0, n);
	umm_free (lines);
}

// A script's lines (argv[0]: its name, then its arguments) -> the last exit code; `exit` ends it
// (and the shell that runs it).
static int run_script (const char *path, int argc, const char *const *argv)
{
	if (g_depth >= SCRIPT_DEPTH) { out ("cmd: scripts nested too deep\n"); return 2; }
	void *f = kapi_open (path);
	if (f == 0) { out ("cmd: cannot open "); out (path); out ("\n"); return 127; }
	unsigned size = kapi_fsize (f), n = 0;
	char *text = (char *) umm_malloc (size + 1);
	const char **args = (const char **) umm_malloc ((unsigned long) (argc + 1) * sizeof *args);
	if (text == 0 || args == 0) { kapi_close (f); umm_free (text); umm_free (args); out ("cmd: out of memory\n"); return 2; }
	while (n < size)				// (a read may return less than asked)
	{
		int r = kapi_read (f, text + n, size - n);
		if (r <= 0) break;
		n += (unsigned) r;
	}
	kapi_close (f);
	text[n] = '\0';
	for (unsigned k = 0; k < n; k++) if (text[k] == '\0') text[k] = ' ';
	for (int k = 0; k < argc; k++) args[k] = argv[k];

	int sargc = g_vars.argc; const char *const *sargv = g_vars.argv; const char **sargs = g_args;
	g_vars.argc = argc; g_vars.argv = args; g_args = args;
	g_depth++;
	run_text (text);
	g_depth--;
	g_vars.argc = sargc; g_vars.argv = sargv; g_args = sargs;
	umm_free (args); umm_free (text);
	return g_vars.status;
}

static int shell (void)
{
	g_vars.get = var_get; g_vars.run = host_capture;
	g_host.vars = &g_vars; g_host.pipeline = run_pipeline; g_host.set = var_set;
	g_host.stop = host_stop; g_host.error = host_error;
	cmd_glob_hook = glob;
	env_load ();

	// cmd file [args] / cmd -c "line": run and exit
	static char blk[CMD_ARGV + 600];
	static const char *argv[CMD_ARGV / 2];
	int argc = 0;
	int n = kapi_get_argv (blk, sizeof blk - 2);
	if (n > 0)
	{
		if (n > (int) sizeof blk - 2) n = (int) sizeof blk - 2;
		blk[n] = blk[n + 1] = '\0';
		for (const char *p = blk; *p && argc < (int) (sizeof argv / sizeof argv[0]); p += ax_strlen (p) + 1)
			argv[argc++] = p;
	}
	else						// (a kernel before v75: one word, the script)
	{
		static char args[300];
		kapi_get_args (args, sizeof args);
		int i = 0; while (args[i] == ' ') i++;
		int e = i; while (args[e] && args[e] != ' ') e++;
		args[e] = '\0';
		argv[argc++] = "cmd";
		if (args[i]) argv[argc++] = &args[i];
	}
	if (argc >= 2)
	{
		if (ax_streq (argv[1], "-c"))
		{
			if (argc < 3) { out ("usage: cmd [script [arguments]] | cmd -c \"command line\"\n"); return 2; }
			g_vars.argc = argc - 2; g_vars.argv = argv + 2; g_args = argv + 2;
			char *text = dup (argv[2]);
			if (text == 0) return 2;
			run_text (text);
		}
		else g_vars.status = run_script (argv[1], argc - 1, argv + 1);
		return g_exit ? g_exitcode : g_vars.status;
	}

	out ("Onyx shell (cmd) -- type a command, or `exit`.\n");

	// The interactive shell: a line, or a block (if / while / for) read up to its end, then run.
	unsigned long cap = CMD_LINE;
	char *text = (char *) umm_malloc (cap);
	if (text == 0) return 2;
	while (!g_exit)
	{
		prompt ();
		unsigned long len = 0;
		int depth = 0, done = 0;
		for (;;)				// read the command: its lines
		{
			unsigned long start = len;
			int ch;
			for (;;)			// one line from stdin
			{
				ch = in_getc ();
				if (ch < 0) { done = 1; break; }			// stdin EOF -> shell ends
				if (ch == 4) { if (len == start) done = 1; break; }	// Ctrl-D
				if (ch == 3) { len = start = 0; depth = 0; continue; }	// Ctrl-C: what was typed is dropped
				if (ch == '\r') continue;
				if (ch == '\n') break;
				if (len - start >= CMD_LINE - 1) continue;
				if (len + 2 >= cap)
				{
					char *t = (char *) umm_realloc (text, cap * 2);
					if (t == 0) continue;
					text = t; cap *= 2;
				}
				text[len++] = (char) ch;
			}
			text[len] = '\0';
			depth += cs_depth (text + start);
			if (done || depth <= 0) break;
			text[len++] = '\n';		// (inside a block: its next line)
			out ("> ");
		}
		g_abort = 0;
		if (len > 0 && !(done && depth > 0)) run_text (text);
		if (done) break;
	}
	return g_exit ? g_exitcode : 0;
}

int main (void)
{
	int r = shell ();
	kapi_exit (r);			// (the exit code: what a caller's && || and $? read)
	return r;
}
