//
// keyb -- show or switch the keyboard layout. With no argument it prints the current
// layout and the available ones; `keyb <XX>` switches to that compiled-in country
// map at runtime.
//   usage: keyb            show current + available
//          keyb FR         switch to the French (azerty) layout
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char args[32];
	kapi_get_args (args, sizeof (args));

	int i = 0;
	while (args[i] == ' ') i++;

	if (args[i] == '\0')			// no argument: report
	{
		char cur[16];
		kapi_get_keymap (cur, sizeof (cur));
		ax_puts ("current: ");
		ax_putln (cur[0] ? cur : "(boot default)");
		ax_putln ("available: US UK DE FR ES IT DV");
		ax_putln ("usage: keyb <XX>");
		return 0;
	}

	// Uppercase the requested code (LoadMap matches "FR", "US", ...).
	char name[8]; int n = 0;
	while (args[i] != '\0' && args[i] != ' ' && n < 7)
	{
		char c = args[i++];
		if (c >= 'a' && c <= 'z') c -= 32;
		name[n++] = c;
	}
	name[n] = '\0';

	// Wait for the keyboard to finish USB enumeration before applying the layout: at
	// boot, autostart runs `keyb FR` before the keyboard is up. Poll the kernel for
	// readiness (ABI v26) up to ~5 s. The kernel no longer applies any cmdline layout.
	int waited = 0;
	while (!kapi_kbd_ready () && waited < 5000) { kapi_msleep (50); waited += 50; }
	if (!kapi_kbd_ready ())
	{
		ax_putln ("keyb: no keyboard attached");
		return 1;
	}

	// Prefer a layout file SD:/etc/keymaps/<NAME>.kmap (so new layouts need no kernel
	// rebuild); the kernel copies the table, so our buffer is transient. Fall back to
	// the compiled-in country map if the file is missing/invalid.
	char path[64]; int p = 0;
	const char *pre = "SD:/etc/keymaps/";
	for (int k = 0; pre[k]; k++) path[p++] = pre[k];
	for (int k = 0; name[k] && p < (int) sizeof path - 6; k++) path[p++] = name[k];
	const char *suf = ".kmap"; for (int k = 0; suf[k]; k++) path[p++] = suf[k];
	path[p] = '\0';

	int ok = 0;
	void *f = kapi_open (path);
	if (f)
	{
		static unsigned char buf[2048];
		int n = kapi_read (f, buf, sizeof buf);
		kapi_close (f);
		if (n > 0) ok = kapi_set_keymap_data (name, buf, (unsigned) n);
	}
	if (!ok) ok = kapi_set_keymap (name);		// fallback: compiled-in country map

	if (ok)
	{
		ax_puts ("keyboard layout -> ");
		ax_putln (name);
		return 0;
	}
	ax_puts ("keyb: unknown layout '");
	ax_puts (name);
	ax_putln ("' (try US UK DE FR ES IT DV)");
	return 1;
}
