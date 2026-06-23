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

	if (kapi_set_keymap (name))
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
