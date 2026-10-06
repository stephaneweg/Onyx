//
// eject -- a USB volume made safe to remove: its open files synced, the stick's cache flushed, the
// volume unmounted. Usage: eject [-f] [USB: | USB2: | USB3:]   (none: the one USB volume mounted)
//   -f   even with files still open on it (the programs' further calls on them fail)
// kapi v91 vol_eject.
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
		else if (tok[0] == '-') { ax_putln ("usage: eject [-f] [USB: | USB2: | USB3:]"); return 2; }
		else vu_volname (tok, vol, sizeof vol);
	}
	if (!vol[0])					// the one USB volume mounted
	{
		struct kapi_volume v[16];
		int n = kapi_vol_list (v, 16, 0), found = 0;
		if (n > 16) n = 16;
		for (int k = 0; k < n; k++)
		{
			if (!(v[k].flags & KAPI_VF_REMOVABLE) || v[k].state != KAPI_VST_MOUNTED) continue;
			if (found++) { ax_putln ("eject: several USB volumes are mounted: say which (eject USB2:)"); return 2; }
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
