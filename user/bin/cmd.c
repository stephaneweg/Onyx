//
// cmd -- the shell, as an ordinary /bin console program. It reads command lines from
// stdin (the terminal feeds the keyboard there) and writes the prompt + output to
// stdout (the terminal displays it). For each line it builds a pipeline of stages
// (split on '|') with redirections (< > >>) -- quotes and backslash escapes as in
// cmdparse.h --, spawns /bin/<cmd> for each with its exact argv, wires
// the stages together, forwards its own stdin to the first stage (so interactive
// programs read the keyboard; Ctrl-D ends that input), and drains the last stage's
// output to stdout. Builtins: cd, pwd, clear, exit. Loops until stdin EOF.
//
#include "kapi.h"
#include "applib.h"
#include "cmdparse.h"

static struct CmdStage g_stage[CMD_MAXSTAGES];
static int g_exit = 0;

static void out (const char *s) { kapi_stdout_write (s, (unsigned) ax_strlen (s)); }

// A stage spawned: spawn_ex with its exact argv block (argv[0] the program's path), so the child
// gets the words as cmd split them; a kernel without it (< v75): kapi_spawn with the words joined
// by blanks, a word with a blank in double quotes (the kernel splits that again). -> the process
// handle, or 0 (*nf: the program was not found).
static void *spawn_stage (const char *bin, const struct CmdStage *st, void *in, void *outs, int *nf)
{
	static char blk[CMD_ARGV + 300];
	int p = 0;
	for (int k = 0; bin[k]; k++) blk[p++] = bin[k];
	blk[p++] = '\0';
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
	for (int i = 1; i < st->argc; i++)
	{
		const char *w = cmd_arg (st, i);
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

static void run_line (char *input)
{
	const char *err;
	int ns = cmd_parse (input, g_stage, CMD_MAXSTAGES, &err);
	if (ns < 0) { out ("cmd: "); out (err); out ("\n"); return; }
	if (ns == 0) return;

	// Builtins (single stage). clear emits form-feed; the terminal clears on it.
	if (ns == 1)
	{
		const char *c = g_stage[0].argv;
		if (ax_streq (c, "clear")) { kapi_stdout_write ("\f", 1); return; }
		if (ax_streq (c, "exit"))  { g_exit = 1; return; }
		if (ax_streq (c, "pwd"))   { char w[256]; kapi_getcwd (w, sizeof w); out (w); out ("\n"); return; }
		if (ax_streq (c, "cd"))
		{
			const char *d = g_stage[0].argc > 1 ? cmd_arg (&g_stage[0], 1) : "SD:/";
			if (!kapi_chdir (d)) { out ("cd: no such directory: "); out (d); out ("\n"); }
			return;
		}
	}

	void *proc[CMD_MAXSTAGES]; int nproc = 0;
	void *owned[CMD_MAXSTAGES * 2 + 2]; int nowned = 0;
	void *cin = 0, *pout = 0, *prev = 0; int failed = 0;

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

		char bin[300]; int p = 0;
		const char *pre = "SD:/bin/";
		for (int k = 0; pre[k]; k++) bin[p++] = pre[k];
		for (int k = 0; g_stage[s].argv[k] && p < (int) sizeof bin - 1; k++) bin[p++] = g_stage[s].argv[k];
		bin[p] = '\0';

		int nf = 0;
		void *pr = spawn_stage (bin, &g_stage[s], sin, sout, &nf);
		if (!pr)
		{
			out (g_stage[s].argv); out (nf ? ": command not found\n" : ": cannot start\n");
			failed = 1; break;
		}
		proc[nproc++] = pr;
	}

	// Pump: forward our stdin to the first stage (Ctrl-D ends it), drain the last
	// stage's output to our stdout, until every stage has finished.
	void *mystdin = kapi_stdin ();
	char b[256]; int n;
	while (!failed)
	{
		if (cin && mystdin)
			while ((n = kapi_stream_read_nb (mystdin, b, sizeof b)) > 0)
				for (int k = 0; k < n; k++)
				{
					if (b[k] == 4) kapi_stream_eof (cin);		// Ctrl-D
					else kapi_stream_write (cin, &b[k], 1);
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
		kapi_msleep (8);
	}

	for (int k = 0; k < nproc; k++) kapi_wait (proc[k]);
	for (int k = 0; k < nowned; k++) kapi_stream_close (owned[k]);	// pipes + files (NOT our stdin/out)
}

int main (void)
{
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
			if (ch == '\r') continue;
			if (ch == '\n') break;
			if (llen < (int) sizeof line - 1) line[llen++] = ch;
		}
		line[llen] = '\0';
		if (llen > 0) run_line (line);
		if (done) break;
	}
	return 0;
}
