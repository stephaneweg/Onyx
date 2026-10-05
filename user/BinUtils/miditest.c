//
// miditest -- prints the USB MIDI input (kapi v68): the devices attached, then every event
// as it comes (time in ms since the start, device, cable, the bytes, and a note name for
// note on / off). "miditest <seconds>" stops after that long (default 60); Ctrl+C or a
// kill ends it too. Plug a keyboard in while it runs: it is found within ~0.1 s.
//
#include "appkit/appkit.h"
#include "applib.h"

static void put_num (unsigned v, int width)
{
	char b[12]; int n = 0;
	do { b[n++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	while (n < width) b[n++] = ' ';
	while (n > 0) kapi_stdout_write (&b[--n], 1);
}

static void put_hex (unsigned v)
{
	static const char h[] = "0123456789ABCDEF";
	char b[3] = { h[(v >> 4) & 15], h[v & 15], ' ' };
	kapi_stdout_write (b, 3);
}

static void put_note (unsigned n)
{
	static const char *names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	ax_puts (names[n % 12]);
	int oct = (int) (n / 12) - 1;
	if (oct < 0) { ax_puts ("-"); oct = -oct; }
	put_num ((unsigned) oct, 0);
}

int main (void)
{
	if (kapi_abi_version () < 68) { ax_putln ("miditest: the kernel is older than v68 (no MIDI)"); return 1; }
	char args[32]; kapi_get_args (args, sizeof args);
	unsigned secs = 0;
	for (int i = 0; args[i] >= '0' && args[i] <= '9'; i++) secs = secs * 10 + (unsigned) (args[i] - '0');
	if (secs == 0) secs = 60;

	int devs = kapi_midi_devices ();
	ax_puts ("miditest: "); put_num ((unsigned) devs, 0);
	ax_puts (" MIDI device(s); listening for "); put_num (secs, 0); ax_putln (" s");

	struct kapi_midi_event ev[32];
	while (kapi_midi_read (ev, 32) > 0) { }			// (what was queued before)
	unsigned start = kapi_clock_us ();
	unsigned t0 = kapi_get_ticks ();
	unsigned count = 0;
	while ((kapi_get_ticks () - t0) < secs * 100 && !kapi_should_exit ())
	{
		int now = kapi_midi_devices ();
		if (now != devs)
		{
			ax_puts ("-- devices: "); put_num ((unsigned) now, 0); ax_putln ("");
			devs = now;
		}
		int n = kapi_midi_read (ev, 32);
		for (int i = 0; i < n; i++)
		{
			struct kapi_midi_event *e = &ev[i];
			put_num ((e->time_us - start) / 1000, 7); ax_puts (" ms  umidi");
			put_num (e->device, 0); ax_puts (" c"); put_num (e->cable, 0); ax_puts ("  ");
			put_hex (e->status);
			if (e->length > 1) put_hex (e->data1);
			if (e->length > 2) put_hex (e->data2);
			unsigned kind = e->status & 0xF0;
			if ((kind == 0x90 || kind == 0x80) && e->length == 3)
			{
				ax_puts (kind == 0x90 && e->data2 != 0 ? " on  " : " off ");
				put_note (e->data1);
				ax_puts (" ch"); put_num ((e->status & 15) + 1, 0);
				ax_puts (" vel "); put_num (e->data2, 0);
			}
			else if (kind == 0xB0 && e->length == 3)
			{
				ax_puts (" CC "); put_num (e->data1, 0); ax_puts (" = "); put_num (e->data2, 0);
			}
			else if (kind == 0xE0 && e->length == 3)
			{
				ax_puts (" bend "); put_num ((unsigned) e->data1 | ((unsigned) e->data2 << 7), 0);
			}
			ax_putln ("");
			count++;
		}
		if (n == 0) kapi_msleep (2);
	}
	ax_puts ("miditest: "); put_num (count, 0); ax_putln (" events");
	return 0;
}
