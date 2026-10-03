//
// uname -- what system this is. Usage: uname [-a] [-s] [-n] [-r] [-v] [-m] [-p]
//   -s  the system's name (Onyx) -- the default
//   -n  the host name
//   -r  the kernel's release: its kapi version ("kapi 79")
//   -v  the kernel's build: the git revision ("+": built from changed sources) and the date
//   -m  the machine (aarch64)
//   -p  the system package installed (SD:/var/pkg/db/onyx.ini's version)
//   -a  all of them, then the board and its memory
// The kernel answers by kapi v79 kernel_info; an older one only tells its kapi version.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#include "kapi.h"
#include "applib.h"

static char s_info[512], s_net[1024], s_file[2048];

// The value of "key value" (text: lines) -> out ("" when there is none).
static void value_of (const char *text, const char *key, char sep, char *out, int cap)
{
	int kl = ax_strlen (key);
	out[0] = 0;
	for (const char *p = text; *p; )
	{
		const char *q = p; while (*q == ' ' || *q == '\t') q++;
		int m = 0; while (m < kl && q[m] == key[m]) m++;
		if (m == kl)
		{
			q += kl; while (*q == ' ' || *q == '\t') q++;
			if (sep == ' ' || *q == sep)
			{
				if (sep != ' ') { q++; while (*q == ' ' || *q == '\t') q++; }
				int n = 0;
				while (*q && *q != '\n' && *q != '\r' && n < cap - 1) out[n++] = *q++;
				out[n] = 0;
				return;
			}
		}
		while (*p && *p != '\n') p++;
		if (*p == '\n') p++;
	}
}

static int read_file (const char *path, char *buf, int cap)
{
	int n = 0;
	void *f = kapi_open (path);
	if (f) { n = kapi_read (f, buf, (unsigned) cap - 1); kapi_close (f); if (n < 0) n = 0; }
	buf[n] = 0;
	return n;
}

static void put_u (unsigned v) { char t[12]; int k = 0; do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v); char o[12]; int n = 0; while (k) o[n++] = t[--k]; o[n] = 0; ax_puts (o); }

int main (void)
{
	char args[64], v[128];
	int s = 0, n = 0, r = 0, ver = 0, m = 0, p = 0, a = 0;
	kapi_get_args (args, sizeof args);
	for (int i = 0; args[i]; i++)
	{
		if (args[i] != '-') continue;
		for (i++; args[i] && args[i] != ' '; i++)
			switch (args[i])
			{
			case 'a': a = 1; break;
			case 's': s = 1; break;
			case 'n': n = 1; break;
			case 'r': r = 1; break;
			case 'v': ver = 1; break;
			case 'm': m = 1; break;
			case 'p': p = 1; break;
			default: ax_putln ("usage: uname [-a] [-s] [-n] [-r] [-v] [-m] [-p]"); return 1;
			}
		if (!args[i]) break;
	}
	if (a) s = n = r = ver = m = p = 1;
	if (!(s | n | r | ver | m | p)) s = 1;

	int known = kapi_kernel_info (s_info, sizeof s_info) > 0;
	int first = 1;
#define SEP() do { if (!first) ax_puts (" "); first = 0; } while (0)
	if (s) { SEP (); value_of (s_info, "name", ' ', v, sizeof v); ax_puts (v[0] ? v : "Onyx"); }
	if (n)
	{
		SEP ();
		kapi_net_info (s_net, sizeof s_net);
		value_of (s_net, "hostname", ' ', v, sizeof v);
		if (!v[0]) { read_file ("SD:etc/system.ini", s_file, sizeof s_file); value_of (s_file, "hostname", '=', v, sizeof v); }
		ax_puts (v[0] ? v : "onyx");
	}
	if (r) { SEP (); ax_puts ("kapi "); put_u (KT->version); }
	if (ver)
	{
		SEP ();
		if (known)
		{
			value_of (s_info, "rev", ' ', v, sizeof v); ax_puts ("#"); ax_puts (v);
			value_of (s_info, "built", ' ', v, sizeof v); ax_puts (" "); ax_puts (v);
		}
		else ax_puts ("(a kernel before kapi 79: its build is in the boot log)");
	}
	if (m) { SEP (); value_of (s_info, "machine", ' ', v, sizeof v); ax_puts (v[0] ? v : "aarch64"); }
	if (p)
	{
		SEP ();
		read_file ("SD:var/pkg/db/onyx.ini", s_file, sizeof s_file);
		value_of (s_file, "version", '=', v, sizeof v);
		ax_puts ("onyx "); ax_puts (v[0] ? v : "?");
	}
	if (a && known)
	{
		value_of (s_info, "model", ' ', v, sizeof v); ax_puts (" ("); ax_puts (v);
		value_of (s_info, "ram", ' ', v, sizeof v); ax_puts (", "); ax_puts (v); ax_puts (" MB)");
	}
	ax_putln ("");
	return 0;
}
