//
// eject -- a USB device made safe to remove: its open files synced, the stick's cache flushed, its
// volumes unmounted (USBn, or all its partitions USBnP1..). Usage: eject [-f] [USB1: | USB2: | USB1P2: ...]
// (none: the one USB device mounted; any of a device's volumes names the device)
//   -f   even with files still open on it (the programs' further calls on them fail)
// kapi v93 vol_eject.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see docs/LICENSING.md)
//
#include "appkit/appkit.h"
#include "volutil.h"

int main (void)
{
	char args[256], tok[64], vol[16] = "";
	int len = kapi_get_args (args, sizeof args), i = 0;
	unsigned flags = 0;
	while (vu_arg (args, len, &i, tok, sizeof tok) > 0)
	{
		if (ax_streq (tok, "-f")) flags |= KAPI_EJECT_FORCE;
		else if (tok[0] == '-') { ax_putln ("usage: eject [-f] [USB1: | USB2: | USB3: | USB1P2: ...]"); return 2; }
		else vu_volname (tok, vol, sizeof vol);
	}
	if (!vol[0])					// the one USB device mounted (its partitions: one device)
	{
		struct kapi_volume v[24];
		int n = kapi_vol_list (v, 24, 0), found = 0;
		const char *dev = 0;
		if (n > 24) n = 24;
		for (int k = 0; k < n; k++)
		{
			if (!(v[k].flags & KAPI_VF_REMOVABLE) || v[k].state != KAPI_VST_MOUNTED) continue;
			if (dev && ax_streq (dev, v[k].device)) continue;
			if (found++) { ax_putln ("eject: several USB devices are mounted: say which (eject USB2:)"); return 2; }
			dev = v[k].device;
			vu_volname (v[k].name, vol, sizeof vol);
		}
		if (!found) { ax_putln ("eject: no USB volume is mounted"); return 1; }
	}
	int r = kapi_vol_eject (vol, flags);
	ax_puts (vol);
	if (r == 0) { ax_putln (" can be removed safely"); return 0; }
	ax_puts (" not ejected: "); ax_putln (vu_err (r));
	if (r == -KAPI_EBUSY) ax_putln ("(its files were synced; close them, or eject -f to eject anyway)");
	return 1;
}
