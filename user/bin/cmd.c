//
// cmd -- the shell, as an ordinary /bin console program. It reads command lines from
// stdin (the terminal feeds the keyboard there) and writes the prompt + output to
// stdout (the terminal displays it). A line is a list of commands (; && ||: cmdparse.h); for
// each it builds a pipeline of stages (split on '|') with redirections (< > >>) -- quotes and
// backslash escapes as in cmdparse.h --, spawns /bin/<cmd> for each with its exact argv, wires
// the stages together, forwards its own stdin to the first stage (so interactive
// programs read the keyboard; Ctrl-D ends that input, Ctrl-C stops the programs), and drains
// the last stage's output to stdout. Builtins: cd, pwd, clear, exit, source (.).
//
// Scripts (docs/04 "Scripts"): `cmd file [args]` runs the file's lines and exits (the keyboard
// still goes to the programs it starts); `cmd -c "line"` runs one line; `source file [args]`
// runs a file inside this shell (its cd stays); a command word ending with .sh is a script,
// run by a cmd of its own (looked for in the current folder, then in SD:/bin). In a script:
// # comments, $1 .. $9, $#, $*, $? (the last exit code). Without arguments: the interactive
// shell, until stdin's end.
//
#include "kapi.h"
#include "applib.h"
#include "umm.h"
#include "cmdparse.h"

#define SCRIPT_DEPTH	8		// source inside source ...

static struct CmdStage g_stage[CMD_MAXSTAGES];
static int g_exit = 0, g_exitcode = 0;
static int g_status = 0;		// $?: the last command's exit code
static int g_abort = 0;			// Ctrl-C: the rest of the line / of the scripts is dropped
static int g_depth = 0;
static struct CmdVars g_vars;		// the running script's arguments

static void out (const char *s) { kapi_stdout_write (s, (unsigned) ax_strlen (s)); }

static int run_script (const char *path, int argc, const char *const *argv);

// A stage spawned: spawn_ex with its exact argv block (argv[0] the program's path; `first`, when
// set, before the stage's own arguments: the script a cmd is started for), so the child
// gets the words as cmd split them; a kernel without it (< v75): kapi_spawn with the words joined
// by blanks, a word with a blank in double quotes (the kernel splits that again). -> the process
// handle, or 0 (*nf: the program was not found).
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
	a.path = bin; a.argv = blk; a.in = in; a.out = outs;
	long long h = kapi_spawn_ex (&a);
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

static int ends_with (const char *s, const char *suf)
{
	int n = ax_strlen (s), m = ax_strlen (suf);
	return n > m && ax_streq (s + n - m, suf);
}
static int file_exists (const char *p)
{
	void *f = kapi_open (p);
	if (f) kapi_close (f);
	return f != 0;
}

static void prompt (void)
{
	char cwd[256]; kapi_getcwd (cwd, sizeof cwd);
	out (cwd); out (" $ ");
}

// One pipeline (a command, or commands joined by |) -> its exit code (the last stage's).
static int run_pipeline (const char *input)
{
	const char *err;
	int ns = cmd_parse (input, g_stage, CMD_MAXSTAGES, &err);
	if (ns < 0) { out ("cmd: "); out (err); out ("\n"); return 2; }
	if (ns == 0) return g_status;

	// Builtins (single stage). clear emits form-feed; the terminal clears on it.
	if (ns == 1)
	{
		const struct CmdStage *st = &g_stage[0];
		const char *c = st->argv;
		if (ax_streq (c, "clear")) { kapi_stdout_write ("\f", 1); return 0; }
		if (ax_streq (c, "exit"))
		{
			g_exit = 1; g_exitcode = g_status;
			if (st->argc > 1)
			{
				const char *n = cmd_arg (st, 1); int v = 0;
				for (int k = 0; n[k] >= '0' && n[k] <= '9'; k++) v = v * 10 + (n[k] - '0');
				g_exitcode = v;
			}
			return g_exitcode;
		}
		if (ax_streq (c, "pwd"))   { char w[256]; kapi_getcwd (w, sizeof w); out (w); out ("\n"); return 0; }
		if (ax_streq (c, "cd"))
		{
			const char *d = st->argc > 1 ? cmd_arg (st, 1) : "SD:/";
			if (!kapi_chdir (d)) { out ("cd: no such directory: "); out (d); out ("\n"); return 1; }
			return 0;
		}
		if (ax_streq (c, "source") || ax_streq (c, "."))
		{
			if (st->argc < 2) { out ("usage: source <script> [arguments]\n"); return 2; }
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
			const char *src = has_dir || file_exists (word) ? word : bin;
			for (; src[k] && k < (int) sizeof script - 1; k++) script[k] = src[k];
			script[k] = '\0';
			if (!file_exists (script))
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
	void *mystdin = kapi_stdin ();
	char b[256]; int n, intr = -1;			// intr: ticks since the interruption
	if (failed && nproc > 0) { intr = 0; if (cin) kapi_stream_write (cin, "\x03", 1); }
	while (nproc > 0)
	{
		if (mystdin)
		{
			while ((n = kapi_stream_read_nb (mystdin, b, sizeof b)) > 0)
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
			if (n == 0 && cin) { kapi_stream_eof (cin); mystdin = 0; }
		}
		if (pout)
			while ((n = kapi_stream_read_nb (pout, b, sizeof b)) > 0) kapi_stdout_write (b, n);

		int alldone = 1;
		for (int k = 0; k < nproc; k++) if (!kapi_proc_done (proc[k])) alldone = 0;
		if (alldone)
		{
			if (pout) while ((n = kapi_stream_read_nb (pout, b, sizeof b)) > 0) kapi_stdout_write (b, n);
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

// One line: its commands (; && ||), each with its $ variables replaced.
static void run_line (const char *input)
{
	char *seg = (char *) umm_malloc (2 * CMD_LINE);
	if (seg == 0) { out ("cmd: out of memory\n"); return; }
	char *exp = seg + CMD_LINE;
	int pos = 0, op = 0, prev = ';';
	while (!g_exit && !g_abort && cmd_next (input, &pos, seg, CMD_LINE, &op))
	{
		int run = prev == ';' || (prev == '&' && g_status == 0) || (prev == '|' && g_status != 0);
		prev = op ? op : ';';
		if (!run) continue;
		g_vars.status = g_status;
		if (cmd_expand (seg, exp, CMD_LINE, &g_vars) < 0) { out ("cmd: line too long\n"); g_status = 2; continue; }
		g_status = run_pipeline (exp);
	}
	umm_free (seg);
}

// A script's lines, one after the other (argv[0]: its name, then its arguments) -> the last
// exit code; `exit` ends it (and the shell that runs it).
static int run_script (const char *path, int argc, const char *const *argv)
{
	if (g_depth >= SCRIPT_DEPTH) { out ("cmd: scripts nested too deep\n"); return 2; }
	void *f = kapi_open (path);
	if (f == 0) { out ("cmd: cannot open "); out (path); out ("\n"); return 127; }
	unsigned size = kapi_fsize (f), n = 0;
	char *text = (char *) umm_malloc (size + 1);
	if (text == 0) { kapi_close (f); out ("cmd: out of memory\n"); return 2; }
	while (n < size)				// (a read may return less than asked)
	{
		int r = kapi_read (f, text + n, size - n);
		if (r <= 0) break;
		n += (unsigned) r;
	}
	kapi_close (f);
	text[n] = '\0';

	struct CmdVars saved = g_vars;
	g_vars.argc = argc; g_vars.argv = argv;
	g_depth++;
	unsigned start = 0;
	for (unsigned i = 0; i <= n && !g_exit && !g_abort; i++)
	{
		if (i < n && text[i] != '\n') continue;
		unsigned end = i;
		if (end > start && text[end - 1] == '\r') end--;
		text[end] = '\0';
		if (end - start >= CMD_LINE) { out ("cmd: line too long\n"); g_status = 2; }
		else if (end > start) run_line (text + start);
		start = i + 1;
	}
	g_depth--;
	g_vars = saved;
	umm_free (text);
	return g_status;
}

static int shell (void)
{
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
			g_vars.argc = argc - 2; g_vars.argv = argv + 2;
			if (ax_strlen (argv[2]) >= CMD_LINE) { out ("cmd: line too long\n"); return 2; }
			run_line (argv[2]);
		}
		else g_status = run_script (argv[1], argc - 1, argv + 1);
		return g_exit ? g_exitcode : g_status;
	}

	static char line[CMD_LINE];
	out ("Onyx shell (cmd) -- type a command, or `exit`.\n");

	while (!g_exit)
	{
		prompt ();
		int llen = 0, done = 0;
		for (;;)				// read one command line from stdin
		{
			char ch; int n = kapi_stdin_read (&ch, 1);
			if (n <= 0) { done = 1; break; }		// stdin EOF -> shell ends
			if (ch == 4) { if (llen == 0) done = 1; break; }	// Ctrl-D
			if (ch == 3) { llen = 0; continue; }		// Ctrl-C at the prompt: the line dropped
			if (ch == '\r') continue;
			if (ch == '\n') break;
			if (llen < (int) sizeof line - 1) line[llen++] = ch;
		}
		line[llen] = '\0';
		g_abort = 0;
		if (llen > 0) run_line (line);
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
