//
// tools/tests/desktop_sim/clipboard_demo.cpp -- the clipboard widget (user/Apps/clipboard) for the screenshots:
// clipd (user/Apps/clipd) as a thread of the same process, its ring seeded with sample copies, a
// stand-in notifyd; then the real widget. Built by shots.sh, run with SIM_IPC=1 (in-process mailboxes).
//
#include <pthread.h>
#include <unistd.h>
#define main widget_main
#include "../../../user/Apps/clipboard/main.cpp"
#undef main
#define CLIPD_NO_MAIN
#include "../../../user/Apps/clipd/main.cpp"

static void *clipd_thread (void *)
{
	static unsigned char buf[520];
	for (;;) { int from, type; int n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 1); if (n >= 0) { buf[n] = 0; handle (from, type, buf, n); } }
	return 0;
}
static void *notify_thread (void *)
{
	char buf[520];
	for (;;) { int f, t; kapi_mailbox_recv (&f, &t, buf, sizeof buf, 1); }
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
int main ()
{
	static unsigned img[8 + 4] = { 468, 300 };
	seed ("text", "ls /bin | grep e", 16, "terminal", 11, 40);
	seed ("url", "https://github.com/stephaneweg/onyx", 34, "jet", 11, 58, "text");
	seed ("text", "static int g_folder;", 20, "tinypad", 12, 12);
	seed ("rtf", "Onyx -- a homemade OS for the Pi 4", 34, "letters", 12, 20, "text");
	seed ("files-cut", "SD:/kernel/sys/kapi.cpp\nSD:/kernel/include/kapi_abi.h", 53, "fileviewer", 12, 28, "text");
	seed ("image", img, 8, "screenshot", 12, 31);
	seed ("text", "Dentist at 17:30 -- call them before", 36, "calendar", 12, 34);
	g_ring.setCursor (g_ring.e[1].id);				// (the image: Ctrl+V pastes it)
	pthread_t a, b;
	pthread_create (&b, 0, [] (void *) -> void * { kapi_ipc_register (NOTIFY_SERVICE); return notify_thread (0); }, 0);
	pthread_create (&a, 0, [] (void *) -> void * { kapi_ipc_register (CLIP_SERVICE); return clipd_thread (0); }, 0);
	while (!kapi_ipc_lookup (CLIP_SERVICE)) usleep (1000);
	return widget_main ();
}
