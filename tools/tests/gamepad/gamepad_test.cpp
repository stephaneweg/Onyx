// gamepad_test -- user/gamepad.h (the button mapping and SD:/etc/gamepad.ini) against a fake kapi.
#include "kapi.h"
#include "gamepad.h"
#include <assert.h>
int main ()
{
	// a generic SNES-style pad: axes d-pad (0..255), buttons 1..10
	memset (&fake_pad, 0, sizeof fake_pad); fake_pad.vid = 0x0079; fake_pad.pid = 0x0011; fake_pad.focus = 1;
	fake_pad.naxes = 2; for (int i = 0; i < 2; i++) { fake_pad.axes[i].minimum = 0; fake_pad.axes[i].maximum = 255; fake_pad.axes[i].value = 127; }
	fake_pad.nbuttons = 10;
	assert (pad_buttons (0) == 0);
	fake_pad.axes[0].value = 0; fake_pad.buttons = 1 << 2;	// left + button 3 (bottom)
	printf ("generic: %x\n", pad_buttons (0)); assert (pad_buttons (0) == (PAD_LEFT | PAD_A));
	// a hat pad
	fake_pad.axes[0].value = 127; fake_pad.nhats = 1; fake_pad.hats[0] = 3; fake_pad.buttons = 1 << 9;
	printf ("hat: %x\n", pad_buttons (0)); assert (pad_buttons (0) == (PAD_DOWN | PAD_RIGHT | PAD_START));
	fake_pad.hats[0] = 8; fake_pad.nhats = 0;
	// its own section: button 1 = A, d-pad on buttons 5..8
	fake_ini = "# test\n[default]\na = 9\n\n[0079:0011] ; mine\ndpad = buttons\nup = 5\ndown = 6 \nleft=7\nright = 8\na = 1\nstick = 0\n";
	pad_config_reload ();
	fake_pad.buttons = 1 | (1 << 4); fake_pad.axes[0].value = 0;	// (the stick ignored: stick = 0)
	printf ("section: %x\n", pad_buttons (0)); assert (pad_buttons (0) == (PAD_A | PAD_UP));
	// another generic pad: [default]
	fake_pad.pid = 0x0006; fake_pad.buttons = 1 << 8; fake_pad.axes[0].value = 127;
	printf ("default: %x\n", pad_buttons (0)); assert (pad_buttons (0) == PAD_A);
	// a known pad (Xbox): Circle bits, A = bit 9, up = bit 15; no focus -> nothing
	fake_pad.props = 1; fake_pad.vid = 0x045e; fake_pad.pid = 0x028e; fake_pad.buttons = (1 << 9) | (1 << 15) | (1 << 14);
	printf ("known: %x\n", pad_buttons (0)); assert (pad_buttons (0) == (PAD_A | PAD_UP | PAD_START));
	fake_pad.focus = 0; assert (pad_buttons (0) == 0);
	struct pad_input in; fake_pad.focus = 1; fake_pad.axes[0].value = 255; pad_read (0, &in); printf ("lx %d\n", in.lx); assert (in.lx == 1000);
	puts ("ok"); return 0;
}
