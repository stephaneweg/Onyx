//
// remotekeys.h -- the keyboard of a remote client (vncd, rdpd): X11 keysyms (what VNC
// viewers send; rdpd's client sends the same) turned into Onyx input -- the cooked key
// strings apps receive (kapi_inject_key), the held keys (kapi_inject_key_held: games) and
// the modifiers (kapi_inject_modifiers). One client at a time (static state).
//
#ifndef REMOTEKEYS_H
#define REMOTEKEYS_H

#include "appkit/appkit.h"

static int g_ctrl;

static unsigned g_mods;		// MOD_* currently held on the remote client

// "ESC[<n>X" plain, or with modifiers held "ESC[<n|1>;<m>X" (Home/End use 'H'/'F'
// in the modifier form, as Circle's keymap does).
static const char *nav (const char *n, char fin, char modfin)
{
	static char buf[16];
	int i = 0;
	buf[i++] = 0x1b; buf[i++] = '[';
	if (g_mods & (MOD_SHIFT | MOD_ALT | MOD_CTRL))
	{
		unsigned m = 1 + ((g_mods & MOD_SHIFT) ? 1 : 0) + ((g_mods & MOD_ALT) ? 2 : 0)
			       + ((g_mods & MOD_CTRL) ? 4 : 0);
		if (modfin) { buf[i++] = '1'; fin = modfin; }
		else if (*n) while (*n) buf[i++] = *n++;
		else buf[i++] = '1';
		buf[i++] = ';'; buf[i++] = (char) ('0' + m);
	}
	else while (*n) buf[i++] = *n++;
	buf[i++] = fin; buf[i] = 0;
	return buf;
}

// RFB keysym -> the held-key code of kapi_inject_key_held (games), 0 = not tracked.
static int held_code (unsigned sym)
{
	switch (sym)
	{
	case 0xFF51: case 0xFF96: return KEY_LEFT;
	case 0xFF52: case 0xFF97: return KEY_UP;
	case 0xFF53: case 0xFF98: return KEY_RIGHT;
	case 0xFF54: case 0xFF99: return KEY_DOWN;
	case 0xFF0D: case 0xFF8D: return KEY_ENTER;
	case 0xFF1B: return 27;
	case 0x20: return ' ';
	}
	if (sym >= 'a' && sym <= 'z') return (int) sym;
	if (sym >= 'A' && sym <= 'Z') return (int) (sym - 'A' + 'a');
	if (sym >= '0' && sym <= '9') return (int) sym;
	return 0;
}

static void key_event (int down, unsigned sym)
{
	int hc = held_code (sym);
	if (hc) kapi_inject_key_held (hc, down);

	// Modifiers: keep our Ctrl (control chars) and tell the kernel (drag & drop copy).
	unsigned m = 0;
	if (sym == 0xFFE3 || sym == 0xFFE4) m = MOD_CTRL;			// Control L/R
	else if (sym == 0xFFE1 || sym == 0xFFE2) m = MOD_SHIFT;			// Shift L/R
	else if (sym == 0xFFE9 || sym == 0xFFEA || sym == 0xFFE7 || sym == 0xFFE8) m = MOD_ALT; // Alt/Meta
	if (m)
	{
		g_mods = down ? (g_mods | m) : (g_mods & ~m);
		if (m == MOD_CTRL) g_ctrl = down;
		kapi_inject_modifiers (g_mods);
		return;
	}
	if (!down) return;

	const char *s = 0;
	char one[2] = { 0, 0 };
	switch (sym)
	{
	case 0xFF08: s = "\b"; break;			// BackSpace
	case 0xFF09: s = "\t"; break;			// Tab
	case 0xFF0D: case 0xFF8D: s = "\n"; break;	// Return / KP_Enter
	case 0xFF1B: s = "\x1b"; break;			// Escape
	case 0xFFFF: case 0xFF9F: s = "\x1b[3~"; break;	// Delete
	// Navigation keys: with Shift / Alt / Ctrl held, send the xterm modifier form
	// (ESC[1;<m>C, ESC[5;<m>~, m = 1 + Shift 1 + Alt 2 + Ctrl 4) like a USB keyboard
	// through Circle's keymap, so the modifier travels with the key itself.
	case 0xFF50: case 0xFF95: s = nav ("1", '~', 'H'); break;	// Home
	case 0xFF57: case 0xFF9C: s = nav ("4", '~', 'F'); break;	// End
	case 0xFF55: case 0xFF9A: s = nav ("5", '~', 0); break;	// Page Up
	case 0xFF56: case 0xFF9B: s = nav ("6", '~', 0); break;	// Page Down
	case 0xFF51: case 0xFF96: s = nav ("", 'D', 0); break;	// Left
	case 0xFF52: case 0xFF97: s = nav ("", 'A', 0); break;	// Up
	case 0xFF53: case 0xFF98: s = nav ("", 'C', 0); break;	// Right
	case 0xFF54: case 0xFF99: s = nav ("", 'B', 0); break;	// Down
	case 0xFFBE: s = "\x1b[[A"; break;		// F1..F5 as Circle's keymap sends them
	case 0xFFBF: s = "\x1b[[B"; break;
	case 0xFFC0: s = "\x1b[[C"; break;
	case 0xFFC1: s = "\x1b[[D"; break;
	case 0xFFC2: s = "\x1b[[E"; break;
	case 0xFFC3: s = "\x1b[17~"; break;		// F6..F12
	case 0xFFC4: s = "\x1b[18~"; break;
	case 0xFFC5: s = "\x1b[19~"; break;
	case 0xFFC6: s = "\x1b[20~"; break;
	case 0xFFC7: s = "\x1b[21~"; break;
	case 0xFFC8: s = "\x1b[23~"; break;
	case 0xFFC9: s = "\x1b[24~"; break;
	default:
		if (sym >= 0xFFB0 && sym <= 0xFFB9) sym = '0' + (sym - 0xFFB0);	// keypad digits
		else if (sym == 0xFFAA) sym = '*'; else if (sym == 0xFFAB) sym = '+';
		else if (sym == 0xFFAD) sym = '-'; else if (sym == 0xFFAE) sym = '.';
		else if (sym == 0xFFAF) sym = '/';
		if (sym >= 0x20 && sym <= 0xFF)		// ASCII + Latin-1
		{
			unsigned c = sym;
			if (g_ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) c &= 0x1F;
			one[0] = (char) c; s = one;
		}
	}
	if (s) kapi_inject_key (s);
}

#endif
