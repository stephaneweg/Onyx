//
// cmd -- the shell, as an ordinary /bin console program. It reads command lines from
// stdin (the terminal feeds the keyboard there) and writes the prompt + output to
// stdout (the terminal displays it). For each line it builds a pipeline of stages
// (split on '|') with redirections (< > >>), spawns /bin/<cmd>.elf for each, wires
// the stages together, forwards its own stdin to the first stage (so interactive
// programs read the keyboard; Ctrl-D ends that input), and drains the last stage's
// output to stdout. Builtins: cd, pwd, clear, exit. Loops until stdin EOF.
//
#include "kapi.h"
#include "applib.h"

#define MAXSTAGES	6

struct Stage { char cmd[64]; char args[160]; char infile[96]; char outfile[96]; int append; };
static struct Stage g_stage[MAXSTAGES];
static int g_exit = 0;

static void out (const char *s) { kapi_stdout_write (s, (unsigned) ax_strlen (s)); }

static void scopy (char *d, const char *s, int cap)
{ int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }

static int read_word (const char *s, int i, char *dst, int cap)
{
	int t = 0;
	while (s[i] && s[i] != ' ' && s[i] != '<' && s[i] != '>' && t < cap - 1) dst[t++] = s[i++];
	dst[t] = '\0';
	return i;
}

static void parse_stage (const char *s, struct Stage *st)
{
	st->cmd[0] = st->args[0] = st->infile[0] = st->outfile[0] = '\0';
	st->append = 0;
	int i = 0;
	while (s[i])
	{
		while (s[i] == ' ') i++;
		if (!s[i]) break;
		if (s[i] == '<') { i++; while (s[i] == ' ') i++; i = read_word (s, i, st->infile, sizeof st->infile); continue; }
		if (s[i] == '>')
		{
			i++; if (s[i] == '>') { st->append = 1; i++; }
			while (s[i] == ' ') i++;
			i = read_word (s, i, st->outfile, sizeof st->outfile); continue;
		}
		char tok[96];
		i = read_word (s, i, tok, sizeof tok);
		if (tok[0] == '\0') continue;
		if (st->cmd[0] == '\0') scopy (st->cmd, tok, sizeof st->cmd);
		else
		{
			int n = ax_strlen (st->args);
			if (n > 0 && n < (int) sizeof st->args - 1) st->args[n++] = ' ';
			scopy (st->args + n, tok, (int) sizeof st->args - n);
		}
	}
}

static void prompt (void)
{
	char cwd[256]; kapi_getcwd (cwd, sizeof cwd);
	out (cwd); out (" $ ");
}

static void run_line (char *input)
{
	int ns = 0, i = 0;
	while (input[i] && ns < MAXSTAGES)
	{
		char sub[256]; int s = 0;
		while (input[i] && input[i] != '|' && s < (int) sizeof sub - 1) sub[s++] = input[i++];
		sub[s] = '\0';
		if (input[i] == '|') i++;
		parse_stage (sub, &g_stage[ns++]);
	}
	if (ns == 0 || g_stage[0].cmd[0] == '\0') return;

	// Builtins (single stage). clear emits form-feed; the terminal clears on it.
	if (ns == 1)
	{
		const char *c = g_stage[0].cmd;
		if (ax_streq (c, "clear")) { kapi_stdout_write ("\f", 1); return; }
		if (ax_streq (c, "exit"))  { g_exit = 1; return; }
		if (ax_streq (c, "pwd"))   { char w[256]; kapi_getcwd (w, sizeof w); out (w); out ("\n"); return; }
		if (ax_streq (c, "cd"))
		{
			const char *d = g_stage[0].args[0] ? g_stage[0].args : "SD:/";
			if (!kapi_chdir (d)) { out ("cd: no such directory: "); out (d); out ("\n"); }
			return;
		}
	}

	void *proc[MAXSTAGES]; int nproc = 0;
	void *owned[MAXSTAGES * 2 + 2]; int nowned = 0;
	void *cin = 0, *pout = 0, *prev = 0; int failed = 0;

	for (int s = 0; s < ns; s++)
	{
		if (g_stage[s].cmd[0] == '\0') { out ("syntax error\n"); failed = 1; break; }
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

		char bin[160]; int p = 0;
		const char *pre = "SD:/bin/";
		for (int k = 0; pre[k]; k++) bin[p++] = pre[k];
		for (int k = 0; g_stage[s].cmd[k] && p < (int) sizeof bin - 6; k++) bin[p++] = g_stage[s].cmd[k];
		const char *suf = ".elf"; for (int k = 0; suf[k]; k++) bin[p++] = suf[k]; bin[p] = '\0';

		void *pr = kapi_spawn (bin, g_stage[s].args, sin, sout);
		if (!pr) { out (g_stage[s].cmd); out (": command not found\n"); failed = 1; break; }
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
	static char line[512];
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
