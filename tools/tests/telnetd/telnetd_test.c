// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// telnetd_test.c -- the end of a telnetd session, on the PC (tools/tests/run_telnetd_test.sh):
// user/BinUtils/telnetd.c itself and user/BinUtils/shellend.h, against the mock kapi beside this file. The
// client is a script (what it types and when, when it closes, or vanishes without a word), the
// shell a model of /bin/cmd as far as it matters here:
//   - at the prompt it reads lines; `exit` or the end of its input ends it;
//   - `busy` starts a program in the foreground that prints for ever and reads nothing: the
//     shell then passes that output on -- and, as cmd in its stdout_write, sees NOTHING else
//     while the pipe is full; the end of its input does not stop the program, Ctrl-C does;
//   - the "deaf" shell answers nothing at all (stuck in a call).
// The bug this guards (2026-10-05): a client dropped while a program ran left the shell and the
// program for good -- telnetd waited 3 s, let go of the pipes, and both stayed blocked on them.
#include "telnetd.c"

enum { SH_CMD, SH_DEAF };
static struct
{
	int  mode, running, intr;
	long blocked_ms, printed;
	char line[64]; int ll;
} sh;

static void mock_shell_step (void)
{
	struct MockProc *p = &mock_proc;
	static char chunk[512];
	char c;
	if (!p->started || p->done || sh.mode == SH_DEAF) return;
	if (sh.running)
	{
		memset (chunk, 'x', sizeof chunk);
		if (kapi_stream_write_nb (p->out, chunk, sizeof chunk) != (int) sizeof chunk) { sh.blocked_ms += 10; return; }
		sh.printed += (long) sizeof chunk;
	}
	while (kapi_stream_read_nb (p->in, &c, 1) == 1)
	{
		if (c == 3) { sh.intr++; sh.running = 0; sh.ll = 0; continue; }
		if (sh.running) continue;			// (the program's keyboard)
		if (c != '\n') { if (sh.ll < 63) sh.line[sh.ll++] = c; continue; }
		sh.line[sh.ll] = '\0'; sh.ll = 0;
		if (!strcmp (sh.line, "exit")) { p->done = 1; return; }
		if (!strcmp (sh.line, "busy")) sh.running = 1;
		kapi_stream_write_nb (p->out, "$ ", 2);
	}
	if (p->in->n == 0 && p->in->eof && !sh.running) p->done = 1;	// the input's end, at the prompt
}

static struct Session S;
static int g_checks, g_bad;

static void check (int ok, const char *what)
{
	g_checks++;
	if (!ok) { g_bad++; printf ("  FAIL: %s (t = %ld ms)\n", what, mock_now); }
}

static void run (const char *name, int mode, const struct MockEvent *ev, int nev, long close_at, long vanish_at)
{
	printf ("%s\n", name);
	memset (&S, 0, sizeof S); memset (&sh, 0, sizeof sh);
	memset (&mock_proc, 0, sizeof mock_proc); memset (&mock_sock, 0, sizeof mock_sock);
	memset (mock_pipes, 0, sizeof mock_pipes); mock_npipes = 0; mock_now = 0;
	sh.mode = mode;
	mock_sock.ev = ev; mock_sock.nev = nev; mock_sock.close_at = close_at; mock_sock.vanish_at = vanish_at;
	S.used = 1; S.sock = 5; strcpy (S.peer, "192.168.0.9");
	session (&S);
	// whatever happened: the shell is gone and reaped, the two pipes let go, once each
	check (mock_proc.done, "the shell has ended");
	check (mock_proc.reaped == 1, "the shell is reaped");
	check (mock_pipes[0].closed == 1 && mock_pipes[1].closed == 1, "both pipes closed, once");
}

static int sent_has (const char *t)
{
	int n = (int) strlen (t);
	for (int k = 0; k + n <= mock_sock.nsent; k++) if (!memcmp (mock_sock.sent + k, t, (size_t) n)) return 1;
	return 0;
}

int main (void)
{
	{	// the ordinary end: `exit`
		static const struct MockEvent ev[] = { { 100, "exit\r" } };
		run ("exit typed", SH_CMD, ev, 1, -1, -1);
		check (sent_has ("Onyx remote shell") && sent_has ("session closed"), "the greeting and the closing line sent");
		check (!mock_proc.killed && sh.intr == 0, "nothing interrupted, nothing killed");
	}
	{	// the client closes at the prompt
		run ("dropped at the prompt", SH_CMD, 0, 0, 300, -1);
		check (!mock_proc.killed, "the shell left by itself");
		check (mock_now < 300 + 500, "at once");
		check (!sent_has ("session closed"), "nothing sent to a client that is gone");
	}
	{	// the client closes while a program runs: the bug
		static const struct MockEvent ev[] = { { 100, "busy\r" } };
		run ("dropped while a program runs", SH_CMD, ev, 1, 2000, -1);
		check (sh.printed > 4 * MOCK_PIPE_CAP, "the program was printing");
		check (sh.intr == 1 && !sh.running, "the program stopped by Ctrl-C");
		check (!mock_proc.killed, "the shell left by itself");
		check (mock_now < 2000 + 500, "at once");
		check (sh.blocked_ms <= 20, "the shell never waited on a full pipe");
	}
	{	// a shell that answers nothing: terminated, after the delay
		run ("dropped, the shell stuck", SH_DEAF, 0, 0, 300, -1);
		check (mock_proc.killed == 1, "the shell terminated");
		check (mock_proc.kill_at >= 300 + SHELLEND_KILL_MS && mock_proc.kill_at < 300 + SHELLEND_KILL_MS + 200, "after SHELLEND_KILL_MS");
		check (mock_now < 300 + SHELLEND_GIVEUP_MS, "the session's slot freed");
	}
	{	// the client vanishes at the prompt, no close seen: found out by the NOP
		static const struct MockEvent ev[] = { { 100, "\r" } };
		run ("vanished at the prompt", SH_CMD, ev, 1, -1, 1000);
		check (mock_sock.nops == 1, "one NOP sent");
		check (mock_sock.nop_at >= KEEPALIVE_MS && mock_sock.nop_at <= 1000 + KEEPALIVE_MS + 100, "after KEEPALIVE_MS of silence");
		check (!mock_proc.killed && mock_now < mock_sock.nop_at + 500, "the shell left then, by itself");
	}
	{	// the client vanishes while a program prints: found out by that output
		static const struct MockEvent ev[] = { { 100, "busy\r" } };
		run ("vanished while a program runs", SH_CMD, ev, 1, -1, 1000);
		check (sh.intr == 1 && !mock_proc.killed, "the program stopped by Ctrl-C, the shell left by itself");
		check (mock_now < 1000 + 500, "at once");
	}
	{	// a client that says nothing for a long time is still there: the NOPs change nothing
		static const struct MockEvent ev[] = { { 100, "\r" }, { 2 * KEEPALIVE_MS + 100000, "exit\r" } };
		run ("a silent client", SH_CMD, ev, 2, -1, -1);
		check (mock_sock.nops == 2, "a NOP every KEEPALIVE_MS");
		check (sent_has ("session closed") && !mock_proc.killed && sh.intr == 0, "the session ended by its `exit`");
	}
	{	// a connection from which nothing is ever received, its close neither (the kernel's
		// deaf connections): ended after KEEPALIVE_MS, without a NOP
		run ("a deaf connection", SH_CMD, 0, 0, -1, -1);
		check (mock_sock.nops == 0 && !mock_proc.killed, "no NOP, the shell left by itself");
		check (mock_now >= KEEPALIVE_MS && mock_now < KEEPALIVE_MS + 500, "after KEEPALIVE_MS");
		check (!sent_has ("session closed"), "nothing more sent to it");
	}
	printf ("%d checks, %d failed\n", g_checks, g_bad);
	if (g_bad == 0) printf ("all passed\n");
	return g_bad != 0;
}
