//
// tools/tests/clipboard/test.cpp -- the shared clipboard on the PC: clipd (user/Apps/clipd, its loop as
// a thread), a stand-in notifyd (a thread: the notifications counted) and an app (this main thread)
// using user/Kits/uikit/clipboard.h, over the desktop simulator's in-process mailboxes (SIM_IPC=1) and RAM:
// (SIM_RAM). Run by tools/tests/run_clipboard_test.sh.
//
#define CLIPD_NO_MAIN
#include "../../../user/Apps/clipd/main.cpp"
#include "systemkit/systemkit.h"
#include <pthread.h>
#include <unistd.h>
#include <string>
#include <vector>

static int fails;
static void check (bool c, const char *what) { printf ("  %s %s\n", c ? "ok  " : "FAIL", what); if (!c) fails++; }

static void *clipd_thread (void *)
{
	kapi_ipc_register (CLIP_SERVICE);
	static unsigned char buf[520];
	for (;;) { int from, type; int n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 1); if (n >= 0) { buf[n] = 0; handle (from, type, buf, n); } }
	return 0;
}
static std::vector<std::string> g_notes; static pthread_mutex_t g_nl = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_notifyUp;
static void *notify_thread (void *)
{
	kapi_ipc_register (NOTIFY_SERVICE); g_notifyUp = 1;
	char buf[520];
	for (;;)
	{
		int from, type; int n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 1);
		if (n < 0) continue; buf[n] = 0;
		pthread_mutex_lock (&g_nl); g_notes.push_back (buf + strlen (buf) + 1); pthread_mutex_unlock (&g_nl);
	}
	return 0;
}
static std::string last_note ()
{
	for (int i = 0; i < 200; i++) { usleep (1000); }
	pthread_mutex_lock (&g_nl); std::string s = g_notes.empty () ? "" : g_notes.back (); pthread_mutex_unlock (&g_nl);
	return s;
}
// the widget's view: CLIP_LIST from a thread of its own (a pid of its own)
static std::vector<ClipItemMsg> g_list;
static void *widget_list (void *)
{
	kapi_ipc_register ("widget-test");
	g_list.clear ();
	kapi_mailbox_send (kapi_ipc_lookup (CLIP_SERVICE), CLIP_LIST, "", 1);
	for (;;)
	{
		ClipItemMsg m; int from, type;
		int n = kapi_mailbox_recv (&from, &type, &m, sizeof m, 1);
		if (type == CLIP_END) break;
		if (type == CLIP_ITEM && n == (int) sizeof m) g_list.push_back (m);
	}
	return 0;
}
static int g_wnum;
static void list ()
{
	// (each listing a new thread: its own service name, its own mailbox)
	static char name[32]; snprintf (name, sizeof name, "w%d", ++g_wnum);
	pthread_t t; pthread_create (&t, 0, [] (void *) -> void * {
		kapi_ipc_register (name); g_list.clear ();
		kapi_mailbox_send (kapi_ipc_lookup (CLIP_SERVICE), CLIP_LIST, "", 1);
		for (;;) { ClipItemMsg m; int from, type; int n = kapi_mailbox_recv (&from, &type, &m, sizeof m, 1);
			if (type == CLIP_END) break; if (type == CLIP_ITEM && n == (int) sizeof m) g_list.push_back (m); }
		return (void *) 0; }, 0);
	pthread_join (t, 0);
}
static void settle ()		// (the messages sent before are handled: a GET goes after them)
{
	char b[8]; static const char *const f[1] = { "none" }; unsigned char *d; unsigned n;
	clip_get (f, 1, b, sizeof b, &d, &n);
}

static void *test (void *)
{
	pthread_t a, b;
	pthread_create (&b, 0, notify_thread, 0);
	while (!g_notifyUp) usleep (1000);
	pthread_create (&a, 0, clipd_thread, 0);
	while (!kapi_ipc_lookup (CLIP_SERVICE)) usleep (1000);
	static char t[400000];

	printf ("text\n");
	clip_set_text ("hello");
	check (clip_get_text (t, sizeof t) == 5 && !strcmp (t, "hello"), "a short text (in the message)");
	check (last_note () == "Text copied", "its notification: Text copied");
	std::string big; for (int i = 0; i < 20000; i++) big += "line " + std::to_string (i) + "\n";
	clip_set_text (big.c_str ());
	check (clip_get_text (t, sizeof t) == (int) big.size () && big == t, "a big text (through a RAM: file)");
	check (clip_get_text (t, sizeof t) == (int) big.size (), "pasted again: the cursor stays");

	printf ("image\n");
	std::vector<unsigned> px (300 * 200); for (unsigned i = 0; i < px.size (); i++) px[i] = i * 2654435761u;
	check (clip_set_image (px.data (), 300, 200), "an image copied");
	check (last_note () == "Image copied (300 x 200)", "its notification: Image copied");
	int w = 0, h = 0; unsigned *got = clip_get_image (&w, &h);
	check (got && w == 300 && h == 200 && !memcmp (got, px.data (), px.size () * 4), "the image pasted back");
	delete[] got;
	check (clip_get_text (t, sizeof t) == (int) big.size (), "Ctrl+V in a text field with the image at the cursor: the newest text");

	printf ("files\n");
	clip_set_files ("SD:/docs/a.txt\nSD:/docs/b.txt", 1);
	check (last_note () == "2 items cut", "its notification: 2 items cut");
	int cut = 0; char f[300];
	check (clip_get_file (f, sizeof f, &cut) && !strcmp (f, "SD:/docs/a.txt") && cut == 1, "the first path, cut");
	check (clip_get_text (t, sizeof t) && !strcmp (t, "SD:/docs/a.txt\nSD:/docs/b.txt"), "the paths as text too");
	list ();
	check (g_list.size () == 4 && !strcmp (g_list[0].kind, "files-cut") && !strcmp (g_list[0].preview, "a.txt, b.txt") && g_list[0].nfiles == 2, "the widget's view of it");
	clip_clear (); settle ();
	list ();
	check (g_list.size () == 3 && !strcmp (g_list[0].kind, "image") && g_list[0].cursor, "pasted (moved): the cut item gone, the cursor on the next one");

	printf ("the ring\n");
	for (int i = 0; i < 12; i++) { char s[16]; snprintf (s, sizeof s, "copy %d", i); clip_set_text (s); }
	settle (); list ();
	check (g_list.size () == 10 && !strcmp (g_list[0].preview, "copy 11") && !strcmp (g_list[9].preview, "copy 2"), "10 items kept, the oldest gone");
	unsigned id = g_list[5].id; unsigned char m[4]; clipc_put32 (m, id);
	kapi_mailbox_send (kapi_ipc_lookup (CLIP_SERVICE), CLIP_CURSOR, m, 4);
	check (clip_get_text (t, sizeof t) && !strcmp (t, "copy 6"), "the widget moved the cursor: Ctrl+V pastes that one");
	check (clip_get_text (t, sizeof t) && !strcmp (t, "copy 6"), "... and again");
	kapi_mailbox_send (kapi_ipc_lookup (CLIP_SERVICE), CLIP_DELETE, m, 4); settle ();
	list ();
	check (g_list.size () == 9 && clip_get_text (t, sizeof t) && !strcmp (t, "copy 5"), "deleted: the cursor on the next older one");
	clip_set_text ("new"); settle ();
	check (clip_get_text (t, sizeof t) && !strcmp (t, "new"), "a new copy: the cursor on it");
	kapi_mailbox_send (kapi_ipc_lookup (CLIP_SERVICE), CLIP_CLEAR, "", 1); settle ();
	list ();
	check (g_list.empty () && clip_get_text (t, sizeof t) == 0, "cleared: nothing to paste");
	check (kapi_opendir (CLIP_DIR) && [] { void *d = kapi_opendir (CLIP_DIR); struct kapi_dirent e; int k = 0; while (kapi_readdir (d, &e)) if (e.name[0] != '.') k++; kapi_closedir (d); return k == 0; } (), "no transfer file left in RAM:/clip");
	printf ("%d failure(s)\n", fails);
	fflush (stdout); _exit (fails ? 1 : 0);
	return 0;
}
// (the app is a thread too: the simulator's main thread plays a script at each msleep)
int main () { pthread_t t; pthread_create (&t, 0, test, 0); pthread_join (t, 0); return 0; }
