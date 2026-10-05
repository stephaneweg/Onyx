// gamepad_test -- user/gamepad.h (the button mapping and SD:/etc/gamepad.ini) against a fake kapi.
#include "appkit/appkit.h"
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
	// a generic pad whose triggers are axes: L2 on axis 3 (rest 0, pressed 255), R2 on the lower half of axis 4
	fake_ini = "[1234:5678]\nl2_axis = 3\nr2_axis = -4\n";
	pad_config_reload ();
	memset (&fake_pad, 0, sizeof fake_pad); fake_pad.vid = 0x1234; fake_pad.pid = 0x5678; fake_pad.focus = 1; fake_pad.naxes = 4;
	for (int i = 0; i < 4; i++) { fake_pad.axes[i].minimum = 0; fake_pad.axes[i].maximum = 255; fake_pad.axes[i].value = 128; }
	fake_pad.axes[2].value = 0;
	assert (pad_buttons (0) == 0);
	fake_pad.axes[2].value = 200; fake_pad.axes[3].value = 10;
	printf ("triggers: %x\n", pad_buttons (0)); assert (pad_buttons (0) == (PAD_L2 | PAD_R2));
	// a pad whose d-pad is on axes 3 / 4 (axes 1 / 2 idle at 127), mapped so by the Gamepad app: the
	// right stick of the defaults (3 / 4) must not see it (n64emu: C buttons at every move)
	fake_ini = "[2222:3333]\ndpad = axes\nx_axis = 3\ny_axis = 4\n";
	pad_config_reload ();
	memset (&fake_pad, 0, sizeof fake_pad); fake_pad.vid = 0x2222; fake_pad.pid = 0x3333; fake_pad.focus = 1; fake_pad.naxes = 4;
	for (int i = 0; i < 4; i++) { fake_pad.axes[i].minimum = 0; fake_pad.axes[i].maximum = 255; fake_pad.axes[i].value = 127; }
	fake_pad.axes[2].value = 255;
	pad_read (0, &in);
	printf ("d-pad on axes 3/4: %x, rx %d\n", in.buttons, in.rx); assert (in.buttons == PAD_RIGHT && in.rx == 0 && in.lx == 1000);
	puts ("ok"); return 0;
}
