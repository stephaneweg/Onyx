//
// df -- the volumes' room. Usage: df [volume-or-path ...]   (default: every volume mounted -- SD:,
// SD1:..SD3:, USB:.. (v92 vol_list), RAM:). One line each: the volume, its type, size, used, free (KB / MB / GB), and for
// RAM: (the kernel's RAM volume, lost at a restart) its files and folders. kapi v71 vol_info.
//
#include "appkit/appkit.h"

static void put_u (unsigned long long v) { char t[24]; int k = 0; do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v); char o[24]; int n = 0; while (k) o[n++] = t[--k]; o[n] = 0; ax_puts (o); }

static void put_size (unsigned long long b)		// "512 KB", "37.5 MB", "29.7 GB"
{
	const char *unit = " KB"; unsigned long long d = 1024;
	if (b >= (10ull << 30)) { unit = " GB"; d = 1ull << 30; }
	else if (b >= (10ull << 20)) { unit = " MB"; d = 1ull << 20; }
	unsigned long long whole = b / d, tenth = (b % d) * 10 / d;
	put_u (whole);
	if (d > 1024 && whole < 100) { ax_puts ("."); put_u (tenth); }
	ax_puts (unit);
}

static void pad (int n) { while (n-- > 0) ax_puts (" "); }

static int show (const char *path, int quiet)
{
	struct kapi_vol_info v;
	if (kapi_vol_info (path, &v) != 0)
	{
		if (!quiet) { ax_puts ("df: no volume "); ax_putln (path); }
		return 1;
	}
	ax_puts (path); pad (8 - ax_strlen (path));
	ax_puts (v.type); pad (7 - ax_strlen (v.type));
	ax_puts ("size "); put_size (v.total);
	ax_puts (", used "); put_size (v.used);
	ax_puts (", free "); put_size (v.free);
	if (v.flags & KAPI_VOL_RAM)
	{
		ax_puts (", "); put_u (v.files); ax_puts (" files, "); put_u (v.dirs); ax_puts (" folders (in memory: lost at a restart)");
	}
	kapi_stdout_write ("\n", 1);
	return 0;
}

int main (void)
{
	char args[256];
	int len = kapi_get_args (args, sizeof args), i = 0, any = 0, rc = 0;
	while (i < len)
	{
		while (i < len && args[i] == ' ') i++;
		char tok[128]; int p = 0;
		while (i < len && args[i] != ' ' && p < (int) sizeof tok - 1) tok[p++] = args[i++];
		tok[p] = '\0';
		if (p > 0) { any = 1; rc |= show (tok, 0); }
	}
	if (!any)
	{
		struct kapi_volume v[16];
		int n = kapi_vol_list (v, 16, 0);
		if (n < 0)				// (a kernel before v92)
		{
			static const char *const vols[] = { "SD:", "SD1:", "SD2:", "SD3:", "RAM:" };
			for (unsigned k = 0; k < sizeof vols / sizeof vols[0]; k++) show (vols[k], 1);
		}
		for (int k = 0; k < n && k < 16; k++)
		{
			if (v[k].state != KAPI_VST_MOUNTED) continue;
			char name[12]; int m = 0;
			while (v[k].name[m] && m < 8) { name[m] = v[k].name[m]; m++; }
			name[m++] = ':'; name[m] = 0;
			show (name, 1);
		}
	}
	return rc;
}
