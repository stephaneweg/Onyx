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
		ax_putln ("available: US UK DE FR BE ES IT DV");
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

	// Apply the layout. We do NOT wait for / require the keyboard to be enumerated: the
	// kernel records the layout in a persistent snapshot (kapi_set_keymap_data) and
	// installs it on the keyboard whenever it attaches -- now or later. This kills the
	// old boot race where a slow USB enumeration made `keyb` time out (the ~5 s
	// kapi_kbd_ready poll) and leave the keyboard with no map at all: a dead keyboard
	// while the mouse worked. A SD:/etc/keymaps/<NAME>.kmap file is preferred (so new
	// layouts need no kernel rebuild); see ax_load_keymap.
	int ok = ax_load_keymap (name);

	if (ok)
	{
		ax_puts ("keyboard layout -> ");
		ax_putln (name);
		return 0;
	}
	ax_puts ("keyb: unknown layout '");
	ax_puts (name);
	ax_putln ("' (try US UK DE FR BE ES IT DV)");
	return 1;
}
