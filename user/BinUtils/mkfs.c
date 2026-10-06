//
// mkfs -- format a volume: everything on it is erased, a new FAT / FAT32 / exFAT file system made.
// Usage: mkfs [-t auto|fat|fat32|exfat] [-L label] [-c cluster-bytes] [-y] [--card] VOLUME
//   -t      the file system (auto: FAT16 / FAT32 by the size, exFAT from 32 GB -- as Windows)
//   -L      the volume's label (11 characters at most)
//   -y      no question (else: type the volume's name to confirm)
//   --card  allow a partition of the SD card (SD1: .. SD3:). SD:, the system's volume, never.
// A USB volume (USB:, USB2:, USB3:) is formatted whole: one partition over the stick. kapi v92.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see docs/LICENSING.md)
//
#include "appkit/appkit.h"
#include "volutil.h"

static int usage (void)
{
	ax_putln ("usage: mkfs [-t auto|fat|fat32|exfat] [-L label] [-c cluster-bytes] [-y] [--card] VOLUME");
	return 2;
}

static int read_line (char *buf, int cap)
{
	int n = 0;
	for (;;)
	{
		char c;
		if (kapi_stdin_read (&c, 1) <= 0) break;
		if (c == '\r') continue;
		if (c == '\n') break;
		if (n < cap - 1) buf[n++] = c;
	}
	buf[n] = 0;
	return n;
}

static int ieq (const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		char x = *a >= 'a' && *a <= 'z' ? (char) (*a - 32) : *a, y = *b >= 'a' && *b <= 'z' ? (char) (*b - 32) : *b;
		if (x != y) return 0;
	}
	return *a == *b;
}

int main (void)
{
	char args[256], tok[64], vol[16] = "";
	int len = kapi_get_args (args, sizeof args), i = 0, yes = 0;
	struct kapi_format f;
	kapi_memset (&f, 0, sizeof f);
	while (vu_arg (args, len, &i, tok, sizeof tok) > 0)
	{
		if (ax_streq (tok, "-y")) yes = 1;
		else if (ax_streq (tok, "--card")) f.flags |= KAPI_FMT_CARD;
		else if (ax_streq (tok, "-f")) f.flags |= KAPI_FMT_FORCE;
		else if (ax_streq (tok, "-t"))
		{
			if (!vu_arg (args, len, &i, tok, sizeof tok)) return usage ();
			if (ieq (tok, "auto")) f.fs = KAPI_FMT_AUTO;
			else if (ieq (tok, "fat") || ieq (tok, "fat16")) f.fs = KAPI_FMT_FAT;
			else if (ieq (tok, "fat32") || ieq (tok, "vfat")) f.fs = KAPI_FMT_FAT32;
			else if (ieq (tok, "exfat")) f.fs = KAPI_FMT_EXFAT;
			else return usage ();
		}
		else if (ax_streq (tok, "-L"))
		{
			if (!vu_arg (args, len, &i, tok, sizeof tok)) return usage ();
			int k = 0; for (; tok[k] && k < (int) sizeof f.label - 1; k++) f.label[k] = tok[k]; f.label[k] = 0;
		}
		else if (ax_streq (tok, "-c"))
		{
			if (!vu_arg (args, len, &i, tok, sizeof tok)) return usage ();
			unsigned c = 0; for (int k = 0; tok[k] >= '0' && tok[k] <= '9'; k++) c = c * 10 + (unsigned) (tok[k] - '0');
			int k = ax_strlen (tok); char u = k ? tok[k - 1] : 0;
			if (u == 'k' || u == 'K') c <<= 10; else if (u == 'm' || u == 'M') c <<= 20;
			f.cluster = c;
		}
		else if (tok[0] == '-') return usage ();
		else vu_volname (tok, vol, sizeof vol);
	}
	if (!vol[0]) return usage ();
	if (ieq (vol, "SD:") || ieq (vol, "SD0:")) { ax_putln ("mkfs: SD: is the system's volume: it is never formatted"); return 1; }

	if (!yes)
	{
		ax_puts ("Everything on "); ax_puts (vol); ax_putln (" will be erased.");
		ax_puts ("Type the volume's name ("); ax_puts (vol); ax_puts (") to format it: ");
		char ans[32]; read_line (ans, sizeof ans);
		char a2[16]; vu_volname (ans, a2, sizeof a2);
		if (!ans[0] || !ieq (a2, vol)) { ax_putln ("mkfs: not formatted"); return 1; }
	}
	ax_puts ("Formatting "); ax_puts (vol); ax_putln ("...");
	int r = kapi_vol_format (vol, &f);
	if (r != 0)
	{
		ax_puts ("mkfs: "); ax_puts (vol); ax_puts (" not formatted: "); ax_putln (vu_err (r));
		if (r == -KAPI_EPERM && vol[0] == 'S') ax_putln ("(a partition of the SD card: add --card)");
		if (r == -KAPI_EBUSY) ax_putln ("(close its files, or add -f)");
		return 1;
	}
	struct kapi_volume v[16];
	int n = kapi_vol_list (v, 16, KAPI_VOLS_ROOM);
	if (n > 16) n = 16;
	for (int k = 0; k < n; k++)
	{
		char nm[16]; vu_volname (v[k].name, nm, sizeof nm);
		if (!ieq (nm, vol)) continue;
		ax_puts (vol); ax_puts (" formatted: "); ax_puts (v[k].type); ax_puts (", ");
		vu_put_size (v[k].total);
		if (v[k].label[0]) { ax_puts (", \""); ax_puts (v[k].label); ax_puts ("\""); }
		ax_putln ("");
		return 0;
	}
	ax_puts (vol); ax_putln (" formatted");
	return 0;
}
