//
// tools/tests/server_sim/clipsheet_demo.cpp -- the pocket shell's clipboard sheet (user/Apps/pocketshell/clipsheet.h)
// for pocket640.sh: clipd (user/Apps/clipd) as a thread of the same process, its ring seeded with the copies of the
// mock-up (docs/compact-shell/mockups/pocketterm/apps/clipboard-sheet.png); then the real pocket shell, whose main
// is compiled as shell_main (-Dmain=shell_main). Run with SIM_IPC=1 (in-process mailboxes). At each change of
// clipd's cursor a line is logged ("clipsheet: the cursor is <text>"): what a paste would give.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#define CLIPD_NO_MAIN
#include "../../../user/Apps/clipd/main.cpp"

int shell_main (void);

static void *clipd_thread (void *)
{
	kapi_ipc_register (CLIP_SERVICE);
	static unsigned char buf[520];
	for (;;)
	{
		int from, type;
		int n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 1);
		if (n < 0) continue;
		buf[n] = 0;
		handle (from, type, buf, n);
		if (type == CLIP_CURSOR || type == CLIP_DELETE)		// what Ctrl + V gives now, how many are left
		{
			static const char *const f[1] = { "text" };
			const ClipRep *r = g_ring.best (f, 1);
			fprintf (stderr, "clipsheet: %s -- %d entries, the cursor is \"%.*s\"\n", type == CLIP_CURSOR ? "cursor" : "delete", g_ring.n, r ? (int) r->n : 0, r ? (const char *) r->d : "");
		}
	}
	return 0;
}
static void seed (const char *fmt, const void *d, unsigned n, const char *src, int h, int m, const char *fmt2 = 0)
{
	const char *f[2] = { fmt, fmt2 }; const void *dd[2] = { d, d }; unsigned l[2] = { n, n };
	unsigned size = clipc_size (f, l, fmt2 ? 2 : 1);
	unsigned char *c = (unsigned char *) malloc (size);
	clipc_write (c, f, dd, l, fmt2 ? 2 : 1);
	g_ring.put (c, size, src, h, m);
	free (c);
}
#define SEED(text, src, h, m)	seed ("text", text, (unsigned) strlen (text), src, h, m)
int main ()
{
	static unsigned img[8 + 4] = { 960, 600 };
	if (!getenv ("CLIPSHEET_EMPTY"))
	{
		SEED ("192.168.0.10", "terminal", 9, 34);		// (the simulator's clock: 12:34)
		seed ("image", img, 8, "paint", 11, 30);
		SEED ("WHILE NOT ONGOAL ()", "turtle", 11, 34);
		SEED ("Amount: 2.528,90 \xE2\x82\xAC (VAT included), within 30 days of the invoice's date", "mail", 12, 20);
		seed ("files", "SD:/docs/pictures/sunset-sea.jpg", 32, "fileviewer", 12, 32, "text");
		SEED ("- D-pad and A / B do what the arrows, Enter and Esc do", "tinypad", 12, 34);
		if (getenv ("CLIPSHEET_MANY"))				// (more than the sheet shows: it scrolls)
		{
			SEED ("one more", "notes", 12, 34); SEED ("two more", "notes", 12, 34); SEED ("three more", "notes", 12, 34);
		}
	}
	pthread_t a;
	pthread_create (&a, 0, clipd_thread, 0);
	while (!kapi_ipc_lookup (CLIP_SERVICE)) usleep (1000);
	return shell_main ();
}
