//
// mount -- the volumes. Usage:
//   mount            every volume: its name, state, file system, size, free space, label, files open
//   mount USB1:      a USB device ejected but still plugged in (or SD1:..SD3:) mounted again
// USB devices are mounted by themselves when they are plugged in: USB1: (USB2:, USB3:) a device whole
// (one partition or none), USB1P1:, USB1P2:... its partitions when it has several; USB: is USB1:. kapi v93.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see docs/LICENSING.md)
//
#include "appkit/appkit.h"
#include "volutil.h"

#define MAXV	16

static void list (void)
{
	static struct kapi_volume v[MAXV];
	int n = kapi_vol_list (v, MAXV, KAPI_VOLS_ROOM);
	if (n < 0) { ax_puts ("mount: "); ax_putln (vu_err (n)); return; }
	if (n > MAXV) n = MAXV;
	for (int i = 0; i < n; i++)
	{
		char name[12]; int k = 0;
		while (v[i].name[k] && k < 8) { name[k] = v[i].name[k]; k++; }
		name[k++] = ':'; name[k] = 0;
		ax_puts (name); vu_pad (7 - k);
		if (v[i].state != KAPI_VST_MOUNTED)
		{
			ax_puts (vu_state (&v[i]));
			if (v[i].device_size) { ax_puts (", device "); vu_put_size (v[i].device_size); }
			if (v[i].device[0]) { ax_puts (" ("); ax_puts (v[i].device); ax_puts (")"); }
			ax_putln ("");
			continue;
		}
		ax_puts (v[i].type); vu_pad (7 - ax_strlen (v[i].type));
		vu_put_size (v[i].total);
		if (v[i].free != ~0ull) { ax_puts (", free "); vu_put_size (v[i].free); }
		if (v[i].label[0]) { ax_puts (", \""); ax_puts (v[i].label); ax_puts ("\""); }
		if (v[i].flags & KAPI_VF_SYSTEM) ax_puts (", system");
		if (v[i].flags & KAPI_VF_REMOVABLE) ax_puts (", removable");
		if (v[i].flags & KAPI_VF_RAM) ax_puts (", in memory");
		if (v[i].open) { ax_puts (", "); vu_put_u (v[i].open); ax_puts (" open"); }
		if (v[i].device[0]) { ax_puts (" ("); ax_puts (v[i].device); ax_puts (")"); }
		ax_putln ("");
	}
}

int main (void)
{
	char args[256], tok[64];
	int len = kapi_get_args (args, sizeof args), i = 0, rc = 0, any = 0;
	while (vu_arg (args, len, &i, tok, sizeof tok) > 0)
	{
		any = 1;
		char vol[16]; vu_volname (tok, vol, sizeof vol);
		int r = kapi_vol_mount (vol);
		ax_puts (vol);
		if (r == 0) ax_putln (" mounted");
		else { ax_puts (" "); ax_putln (vu_err (r)); rc = 1; }
	}
	if (!any) list ();
	return rc;
}
