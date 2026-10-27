//
// kmsg -- stream the kernel log in real time (like dmesg -w). It tees CLogger's
// event ring via kapi_klog_read (the logs still go to their normal output, nothing
// is redirected), prints any pending backlog, then keeps printing new events as they
// arrive. Quit with Ctrl+C. A /bin console program: writes to stdout, watches stdin.
// Each line starts with its time since boot, "[   12.345]" (kapi v100; none on an older kernel):
// the kernel keeps the last 1024 events, a whole boot when kmsg is run soon after it.
//
#include "appkit/appkit.h"

static const char *sev_tag (int s)
{
	switch (s)
	{
	case 0:  return "PANIC";
	case 1:  return "ERROR";
	case 2:  return "WARN ";
	case 3:  return "NOTE ";
	default: return "DEBUG";
	}
}

// "[ssss.mmm] " -- the seconds since boot, to the millisecond
static void put_time (unsigned long long us)
{
	char b[24]; int i = 0;
	unsigned long long s = us / 1000000; unsigned ms = (unsigned) (us / 1000 % 1000);
	char d[20]; int n = 0;
	do { d[n++] = (char) ('0' + s % 10); s /= 10; } while (s != 0);
	b[i++] = '[';
	for (int k = n; k < 5; k++) b[i++] = ' ';
	while (n > 0) b[i++] = d[--n];
	b[i++] = '.';
	b[i++] = (char) ('0' + ms / 100); b[i++] = (char) ('0' + ms / 10 % 10); b[i++] = (char) ('0' + ms % 10);
	b[i++] = ']'; b[i++] = ' '; b[i] = '\0';
	ax_puts (b);
}

// The next event, its time when the kernel gives it (has_time 0: an older kernel, no time)
static int next_event (int *sev, unsigned long long *us, int *has_time, char *src, unsigned sc, char *msg, unsigned mc)
{
	if (*has_time)
	{
		int r = kapi_klog_read_at (sev, us, src, sc, msg, mc);
		if (r >= 0) return r;
		*has_time = 0;
	}
	return kapi_klog_read (sev, src, sc, msg, mc);
}

int main (void)
{
	char src[64], msg[224]; int sev;
	unsigned long long us; int has_time = 1;
	void *in = kapi_stdin ();

	ax_putln ("kmsg -- kernel log (Ctrl+C to quit)");

	for (;;)
	{
		int got = 0;
		while (next_event (&sev, &us, &has_time, src, sizeof src, msg, sizeof msg))
		{
			if (has_time) put_time (us);
			ax_puts (sev_tag (sev));
			ax_puts (" ");
			ax_puts (src);
			ax_puts (": ");
			ax_putln (msg);
			got = 1;
		}

		if (in != 0)				// Ctrl+C (byte 3) on stdin ends it
		{
			char c;
			if (kapi_stream_read_nb (in, &c, 1) > 0 && c == 3) break;
		}
		kapi_msleep (got ? 5 : 60);		// poll faster while logs are flowing
	}

	ax_putln ("[kmsg stopped]");
	return 0;
}
