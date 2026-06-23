//
// kmsg -- stream the kernel log in real time (like dmesg -w). It tees CLogger's
// event ring via kapi_klog_read (the logs still go to their normal output, nothing
// is redirected), prints any pending backlog, then keeps printing new events as they
// arrive. Quit with Ctrl+C. A /bin console program: writes to stdout, watches stdin.
//
#include "kapi.h"
#include "applib.h"

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

int main (void)
{
	char src[64], msg[224]; int sev;
	void *in = kapi_stdin ();

	ax_putln ("kmsg -- kernel log (Ctrl+C to quit)");

	for (;;)
	{
		int got = 0;
		while (kapi_klog_read (&sev, src, sizeof src, msg, sizeof msg))
		{
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
