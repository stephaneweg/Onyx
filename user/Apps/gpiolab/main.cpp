//
// gpiolab -- GPIO Lab, the Raspberry Pi's 40-pin header on the screen (GPIOKit: gpiokit/gpiokit.h).
//
//   The header   the 40 pins as on the board (pin 1 top left, the USB ports down): power, ground and
//                the GPIOs, each in its mode's colour, its level lit; the system's pins (the serial
//                console, the HAT EEPROM) greyed. A click (or the arrows) chooses a GPIO.
//   The pin      what it is (its header pin, its other functions, who has it), its mode (Free, Input,
//                Pull-up, Pull-down, Output, PWM), an output's level (Space) or a blink, PWM's frequency
//                and duty (a servo's 50 Hz), its edges logged, a place in the timing chart; in the
//                simulator, what drives an input from outside.
//   Timing chart the chosen pins' levels over the last seconds (sampled at the screen's rate; a PWM pin
//                drawn as its band, its frequency and duty written), paused or running.
//   I2C bus      the bus on GPIO 2 / 3 scanned: the 128 addresses, what usually answers at each found;
//                a BME280's readings, an SSD1306's picture (the simulator's) and a test text sent to it.
//   Edges        the edges of the pins logged, with their time.
// The simulator (GPIOKit's: a board in memory) runs when the system has no GPIO (a PC, a kernel older
// than kapi 92), or when chosen (Board > Use the Simulator): nothing then touches the real pins.
// Arguments: --sim (the simulator), --demo (a set-up to look at: an LED blinking on GPIO 17, a servo on
// GPIO 18, a button on GPIO 27, the bus scanned), --tab chart | i2c | edges.
// The words are in the system's language (uikit/lang.h: English here, SD:/apps/gpiolab.app/lang/fr.txt). The
// Code view's BASIC takes the French words beside the English ones whatever the language (bas::frenchDialect:
// MODEBROCHE, BROCHE, SI ... ALORS); in French its errors, the first sketch and the examples
// (SD:/basic/examples/fr) are the French ones.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"			// FreeType's text (DejaVu Sans) for every widget
#include "gpiokit/gpiokit.h"
#include "filekit/filekit.h"		// (the sketch, the examples: fk_load / fk_save)
#include "basic/bas.h"			// Onyx BASIC built in: the Code view runs its program here
#include <stdio.h>
#include <string.h>

using namespace uikit;

static int W = 1120, H = 640;		// (smaller on a small screen: main)
#define BANNER	48			// the 3.3 V warning's band
#define HIST	512			// the timing chart's samples (the screen's rate: about 8 s)
#define NWATCH	6			// pins in the chart at most
#define NLOG	200			// edges kept

// ---- the state ---------------------------------------------------------------------------------------
static gk_pin_info g_pin[GK_PINS];
static int  g_sel = 17;				// the chosen GPIO
static unsigned g_hist[HIST]; static int g_nh = 0, g_h0 = 0;	// read_all samples (a ring)
static bool g_paused = false;
static int  g_watch[NWATCH] = { 17, 18, 27, 22, -1, -1 };
static bool g_blink[GK_PINS]; static unsigned long long g_blinkUs;
static gk_event g_log[NLOG]; static int g_nlog = 0, g_log0 = 0;
static unsigned g_edgeCount[GK_PINS];
static unsigned long long g_t0;			// the edges' times: from the app's start
static unsigned char g_map[16]; static int g_found = -1;	// the I2C scan (-1: not done)
static char g_bme[96], g_i2cMsg[160];
static int  g_freqIx[GK_PINS], g_duty[GK_PINS];	// a PWM pin's choices (the frequency's entry, duty 0..1000: tenths of %)
static bool g_demo, g_btnSim;
static int  g_tab = 0;
static char g_status[256];
static bool g_fr;				// the language is French (uk_lang): BASIC's messages, the first sketch, the examples

static const char *const FREQS[] = { "50 Hz (servo)", "100 Hz", "500 Hz", "1 kHz", "10 kHz", "25 kHz" };
static const int FREQ_HZ[] = { 50, 100, 500, 1000, 10000, 25000 };

// colours
#define C_PCB		0x1E5B3A
#define C_PCB_EDGE	0x16452B
#define C_3V3		0xF0A030
#define C_5V		0xE0483C
#define C_GND		0x303030
static unsigned mode_col (int mode, int level)
{
	switch (mode)
	{
	case GK_IN: case GK_IN_PULLUP: case GK_IN_PULLDOWN: return level ? 0x5AA9FF : 0x2C5C9A;
	case GK_OUT:	return level ? 0xFFD94A : 0x8A6D16;
	case GK_PWM:	return 0xB57BFF;
	case GK_I2C:	return 0x3CC8B4;
	case GK_SPI:	return 0xFF7BB0;
	case GK_FREE:	return 0xC9D6CD;
	default:	return 0xE0E0E0;			// (an ALT function)
	}
}

static class HeaderView *g_header;
static class PinPanel *g_panel;
static class ChartView *g_chart;
static class I2CView *g_i2c;
static class EdgeView *g_edges;
static SegmentedControl *g_modes, *g_tabs, *g_drive;
static Button *g_btLevel, *g_btPause, *g_btScan, *g_btOled;
static Checkbox *g_cbBlink, *g_cbEdges, *g_cbWatch;
static Dropdown *g_ddFreq;
static Slider *g_slDuty;
static Label *g_lbDuty, *g_lbFreq, *g_lbDrive;

static void sync_controls (void);
// The Code view (the mini IDE): its widgets, and the program's run
static int  g_view = 0;				// 0 the pins, 1 the code
static SegmentedControl *g_viewSw;
static ToggleSwitch *g_simSw;			// the simulator, on / off: above the header
static bool g_running;
static bool g_hasHw;				// the system has a GPIO (kapi >= 92): measured once, at the start
static CodeEdit *g_ed;
static class ConsoleView *g_con;
static Button *g_btRun, *g_btStep, *g_btStop;
static Slider *g_speed;
static Label *g_lbSpeed;
static Dropdown *g_ddEx;
static TextFace *g_mono;
static Root *g_root;
static bool g_stepMode, g_waitStep, g_stopReq, g_quit;
static int  g_runReq;				// 1 run, 2 step: the main loop starts it (never from a callback)
static long g_keyQ[16]; static int g_nkeyQ;	// the keys typed while a program runs (INKEY$)
static void apply_view (void);
static void refresh_pins (void);
static void log_events (const gk_event *ev, int n);
static void set_status (const char *s) { snprintf (g_status, sizeof g_status, "%s", s); }
static void fail (int r, const char *what) { char t[240]; snprintf (t, sizeof t, TR ("%s: %s"), what, TR (gk_error (r))); set_status (t); }
// GPIOKit's own words, shown through uk_tr: its errors (gk_error),
// TR: the system uses this pin, or it is not this program's
// TR: no answer on the bus
// TR: out of memory
// TR: a bad buffer
// TR: another program has it
// TR: no GPIO on this system (update it, or use the simulator)
// TR: not possible on this pin, or a value out of range
// TR: failed
// a mode's name (gk_mode_name; "tag|": shorter, in the header's tags),
// TR: free
// TR: input
// TR: pull-up
// TR: pull-down
// TR: output
// TR: tag|pull-up
// TR: tag|pull-down
// why a pin is the system's (gk_pin_info.reason) and what answers at an I2C address (gk_i2c_guess)
// TR: serial console
// TR: HAT ID EEPROM
// TR: MCP23017 / PCF8574 port expander
// TR: SSD1306 / SH1106 OLED display
// TR: DS3231 / DS1307 clock, MPU-6050
// TR: MPU-6050 (AD0 high)
// TR: BME280 / BMP280 sensor

static bool watched (int pin) { for (int i = 0; i < NWATCH; i++) if (g_watch[i] == pin) return true; return false; }
static bool is_input (int m) { return m == GK_IN || m == GK_IN_PULLUP || m == GK_IN_PULLDOWN; }
static bool mine (int pin) { return g_pin[pin].owner != 0 && g_pin[pin].owner == (unsigned) kapi_getpid (0) ? true : gk_available () == 2 && g_pin[pin].owner != 0; }

// ---- the header ------------------------------------------------------------------------------------------
#define ROWH	23
#define PINX0	190			// the odd pins' column (x of their centre)
#define PINX1	226			// the even pins'
#define HTOP	40
class HeaderView : public Widget
{
public:
	HeaderView (int l, int t, int w, int h) : Widget (l, t, w, h) { canFocus = true; }
	void pinPos (int hp, int *x, int *y) { *x = hp & 1 ? PINX0 : PINX1; *y = HTOP + 16 + ((hp - 1) / 2) * ROWH; }
	void onDraw () override
	{
		unsigned bg = bgColor ();
		canvas.clear (bg);
		uk_text_l (canvas, 4, 4, 20, TR ("40-PIN HEADER"), uk_mix (bg, C_TEXT, 150), 2);
		uk_text_l (canvas, 4, 20, 18, gk_available () == 2 ? TR ("simulated: click a dot to switch it") : TR ("click an output's dot to switch it"),
			   gk_available () == 2 ? 0x7A5CC8 : uk_mix (bg, C_TEXT, 120));
		// the board
		int bx = PINX0 - 24, by = HTOP, bw = PINX1 - PINX0 + 48, bh = 20 * ROWH + 8;
		uk_rbox (canvas, bx, by, bw, bh, 8, C_PCB, C_PCB);
		uk_rline (canvas, bx, by, bw, bh, 8, C_PCB_EDGE, 255);
		canvas.fillRect (PINX0 - 13, by + 4, PINX1 - PINX0 + 26, bh - 8, 0x14402A);	// (the plastic)
		for (int hp = 1; hp <= 40; hp++)
		{
			int x, y; pinPos (hp, &x, &y);
			int g = gk_header_gpio (hp);
			const char *lab = gk_header_label (hp);
			unsigned c; bool reserved = false, chosen = g >= 0 && g == g_sel;
			if (g < 0) c = !strcmp (lab, "GND") ? C_GND : !strcmp (lab, "3V3") ? C_3V3 : C_5V;
			else { reserved = (g_pin[g].flags & GK_F_RESERVED) != 0; c = reserved ? 0x6A6F6C : mode_col (g_pin[g].mode, g_pin[g].level); }
			// the pin: a square pad for pin 1 (as printed), round for the others
			if (chosen) uk_rbox (canvas, x - 13, y - 11, 26, 22, 6, C_ACCENT, C_ACCENT);
			if (hp == 1) uk_rbox (canvas, x - 8, y - 8, 16, 16, 2, 0xD8C690, 0xB8A670);
			else { VPath p; p.circle (V (x), V (y), V (8)); p.fill (canvas, 0xD8C690); }
			VPath d; d.circle (V (x), V (y), V (5)); d.fill (canvas, c);
			if (reserved) { VPath s; s.line (V (x - 4), V (y + 4), V (x + 4), V (y - 4), V (2)); s.fill (canvas, 0xE8E8E8); }
			// its label, outside the board: the odd pins' to the left, the even pins' to the right
			char num[4]; snprintf (num, sizeof num, "%d", hp);
			unsigned ink = reserved ? C_DIS : C_TEXT, dim = uk_mix (bg, C_TEXT, 130);
			int nw = uk_text_w (num);
			if (hp & 1)
			{
				uk_text_l (canvas, bx - 6 - nw, y - 9, 18, num, dim);
				int tx = bx - 34;
				int lw = uk_text_w (lab, g >= 0 ? 2 : 0);
				uk_text_l (canvas, tx - lw, y - 9, 18, lab, g < 0 ? dim : ink, g >= 0 ? 2 : 0);
				if (g >= 0) side (g, tx - lw - 8, y, true, reserved);
			}
			else
			{
				uk_text_l (canvas, bx + bw + 6, y - 9, 18, num, dim);
				int tx = bx + bw + 34;
				uk_text_l (canvas, tx, y - 9, 18, lab, g < 0 ? dim : ink, g >= 0 ? 2 : 0);
				if (g >= 0) side (g, tx + uk_text_w (lab, 2) + 8, y, false, reserved);
			}
		}
		// the key
		int ky = HTOP + 20 * ROWH + 18;
		static const struct { const char *n; unsigned c; } KEY[] = {
			{ TRN ("input"), 0x5AA9FF }, { TRN ("output"), 0xFFD94A }, { "PWM", 0xB57BFF }, { "I2C", 0x3CC8B4 },
			{ "SPI", 0xFF7BB0 }, { TRN ("free"), 0xC9D6CD }, { TRN ("system"), 0x6A6F6C } };
		int kx = 4;
		for (unsigned i = 0; i < sizeof KEY / sizeof KEY[0]; i++)
		{
			const char *kn = TR (KEY[i].n);
			int w = uk_text_w (kn) + 22;
			if (kx + w > width) { kx = 4; ky += 20; }
			VPath p; p.circle (V (kx + 6), V (ky + 9), V (5)); p.fill (canvas, KEY[i].c);
			uk_text_l (canvas, kx + 15, ky, 18, kn, uk_mix (bg, C_TEXT, 150));
			kx += w;
		}
	}
	// a GPIO's mode tag and level dot beside its label (toward the outside: right = false -> to the left)
	void side (int g, int x, int y, bool leftward, bool reserved)
	{
		unsigned bg = bgColor ();
		const char *f = reserved ? (g == 14 || g == 15 ? TR ("console") : "EEPROM") : g_pin[g].mode != GK_FREE ? TRC ("tag", gk_mode_name (g_pin[g].mode)) : gk_gpio_function (g);
		if (!f || !*f) return;
		int w = uk_text_w (f) + 10;
		int tx = leftward ? x - w : x;
		unsigned face = reserved ? uk_mix (bg, C_TEXT, 30) : g_pin[g].mode != GK_FREE ? uk_mix (bg, mode_col (g_pin[g].mode, 1), 90) : uk_mix (bg, C_TEXT, 22);
		uk_rbox (canvas, tx, y - 9, w, 18, 9, face, face);
		uk_text_c (canvas, tx, y - 9, w, 18, f, reserved ? C_DIS : C_TEXT);
	}
	int hit (int mx, int my)
	{
		for (int hp = 1; hp <= 40; hp++)
		{
			int x, y; pinPos (hp, &x, &y);
			if (my < y - ROWH / 2 || my >= y + ROWH / 2) continue;
			bool odd = hp & 1;
			if (odd ? mx < (PINX0 + PINX1) / 2 : mx >= (PINX0 + PINX1) / 2) return gk_header_gpio (hp);
		}
		return -1;
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (bl == 1)
		{
			int g = hit (mx, my);
			if (g >= 0)
			{
				// a click on a pin's own dot: an output of GPIO Lab's (or of its program's) turned over -- on the real
				// header too; on the simulator, an input driven High, then Low (a wire, a button)
				int hp = gk_header_pin (g), px, py; pinPos (hp, &px, &py);
				bool onDot = (mx - px) * (mx - px) + (my - py) * (my - py) <= 100;
				if (onDot && g_pin[g].mode == GK_OUT)
				{
					g_blink[g] = false;
					int r = gk_toggle (g);
					if (r < 0) fail (r, TR ("Output"));
					else { char t[160]; snprintf (t, sizeof t, r ? TR ("GPIO %d set high by a click.") : TR ("GPIO %d set low by a click."), g); set_status (t); }
					refresh_pins ();
				}
				else if (onDot && gk_available () == 2 && is_input (g_pin[g].mode))
				{
					gk_sim_input (g, g_pin[g].level ? 0 : 1);
					refresh_pins ();
				}
				else if (onDot && g_pin[g].mode == GK_FREE && !(g_pin[g].flags & GK_F_RESERVED))
					set_status (TR ("A free pin: make it an Output (the Pins view, or PINMODE in a program), then click its dot to switch it."));
				g_sel = g; sync_controls ();
			}
			setFocus ();
			return true;
		}
		return false;
	}
	bool onKey (long k) override
	{
		int hp = gk_header_pin (g_sel);
		int step = k == KEY_UP ? -2 : k == KEY_DOWN ? 2 : k == KEY_LEFT ? -1 : k == KEY_RIGHT ? 1 : 0;
		if (!step) return false;
		for (int n = hp + step; n >= 1 && n <= 40; n += step)
			if (gk_header_gpio (n) >= 0) { g_sel = gk_header_gpio (n); sync_controls (); return true; }
		return true;
	}
};

// ---- the chosen pin -----------------------------------------------------------------------------------------
class PinPanel : public Widget
{
public:
	PinPanel (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	unsigned bgColor () override { return uk_mix (C_BG, C_FIELD, 140); }
	void onDraw () override
	{
		unsigned bg = bgColor ();
		canvas.clear (parent ? parent->bgColor () : C_BG);
		uk_rbox (canvas, 0, 0, width, height, 8, bg, bg);
		uk_rline (canvas, 0, 0, width, height, 8, uk_tone (C_BG, 80), 200);
		const gk_pin_info &p = g_pin[g_sel];
		char t[240];
		snprintf (t, sizeof t, "GPIO %d", g_sel);
		uk_text_l (canvas, 14, 10, 26, t, C_TEXT, 2);
		int x = 14 + uk_text_w (t, 2) + 12;
		const char *f = gk_gpio_function (g_sel);
		if (*f) snprintf (t, sizeof t, TR ("header pin %d  -  %s"), gk_header_pin (g_sel), f);
		else snprintf (t, sizeof t, TR ("header pin %d"), gk_header_pin (g_sel));
		uk_text_l (canvas, x, 14, 20, t, uk_mix (bg, C_TEXT, 150));
		// its level, as a lamp
		int lv = p.level;
		unsigned lc = lv ? 0x5BD46A : 0x50585A;
		VPath l; l.circle (V (width - 30), V (23), V (9)); l.fill (canvas, lc);
		const char *ls = lv ? TR ("HIGH") : TR ("LOW");
		uk_text_l (canvas, width - 46 - uk_text_w (ls, 2), 13, 20, ls, lv ? C_TEXT : C_DIS, 2);	// (its end against the lamp)
		if (p.flags & GK_F_RESERVED)
		{
			snprintf (t, sizeof t, TR ("Used by the system (%s): GPIO Lab cannot take it."), TR (p.reason));
			uk_text_l (canvas, 14, 52, 20, t, C_DIS);
			return;
		}
		if (p.owner != 0 && !mine (g_sel))
		{
			snprintf (t, sizeof t, TR ("Another program (pid %u) has this pin."), p.owner);
			uk_text_l (canvas, 14, 52, 20, t, C_DIS);
			return;
		}
		uk_text_l (canvas, 14, 46, 18, TR ("MODE"), uk_mix (bg, C_TEXT, 140), 2);
		if (p.mode == GK_I2C || p.mode == GK_SPI)
			uk_text_l (canvas, 14, 120, 20, p.mode == GK_I2C ? TR ("A pin of the I2C bus (the I2C tab: Close Bus gives it back).") : TR ("A pin of SPI 0."), C_TEXT);
		if (p.mode == GK_PWM)
		{
			snprintf (t, sizeof t, TR ("%u Hz, %u.%02u %% high: a pulse of %u us every %u us"), p.pwm_freq, p.pwm_duty / 100, p.pwm_duty % 100,
				  (unsigned) ((unsigned long long) p.pwm_duty * 100 / (p.pwm_freq ? p.pwm_freq : 1)), 1000000u / (p.pwm_freq ? p.pwm_freq : 1));
			uk_text_l (canvas, 14, height - 30, 18, t, uk_mix (bg, C_TEXT, 150));
		}
	}
};

// ---- the timing chart ---------------------------------------------------------------------------------------
class ChartView : public Widget
{
public:
	ChartView (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		unsigned bg = bgColor ();
		canvas.clear (bg);
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD);
		unsigned grid = uk_mix (C_FIELD, C_FIELD_TEXT, 20), dim = uk_mix (C_FIELD, C_FIELD_TEXT, 130);
		int lx = 92, cw = width - lx - 10;
		// the seconds, as lines (a sample a frame: about 60 a second)
		for (int s = 1; s * 60 < HIST; s++)
		{
			int x = lx + cw - s * 60 * cw / HIST;
			if (x < lx) break;
			canvas.fillRect (x, 6, 1, height - 26, grid);
			char t[8]; snprintf (t, sizeof t, "-%ds", s);
			uk_text_l (canvas, x - uk_text_w (t) / 2, height - 20, 16, t, dim);
		}
		int n = 0; for (int i = 0; i < NWATCH; i++) if (g_watch[i] >= 0) n++;
		if (n == 0) { uk_text_c (canvas, 0, 0, width, height, TR ("No pin chosen: tick \"Show in the timing chart\" on a pin."), dim); return; }
		int rowh = (height - 30) / n; if (rowh > 50) rowh = 50;
		int y = 8;
		for (int i = 0; i < NWATCH; i++)
		{
			int g = g_watch[i];
			if (g < 0) continue;
			const gk_pin_info &p = g_pin[g];
			char t[24]; snprintf (t, sizeof t, "GPIO%d", g);
			uk_text_l (canvas, 10, y + rowh / 2 - 15, 18, t, C_FIELD_TEXT, 2);
			uk_text_l (canvas, 10, y + rowh / 2 + 1, 16, p.flags & GK_F_RESERVED ? TR ("system") : TR (gk_mode_name (p.mode)), dim);
			unsigned col = mode_col (p.mode == GK_FREE ? GK_IN : p.mode, 1);
			int hi = y + 6, lo = y + rowh - 8;
			canvas.fillRect (lx, y + rowh - 1, cw, 1, grid);
			if (p.mode == GK_PWM)				// (too fast for the samples: its band)
			{
				int d = (int) p.pwm_duty;
				for (int x = lx; x < lx + cw; x += 6)
				{
					int w = 6 * d / 10000; if (w < 1 && d > 0) w = 1;
					canvas.fillRect (x, hi, 1, lo - hi, uk_mix (C_FIELD, col, 200));
					if (w > 0) canvas.fillRect (x, hi, w, 2, col);
					canvas.fillRect (x + w, lo - 1, 6 - w, 2, col);
				}
				char f[48]; snprintf (f, sizeof f, TR ("%u Hz  %u.%u %%"), p.pwm_freq, p.pwm_duty / 100, (p.pwm_duty % 100) / 10);
				int fw = uk_text_w (f, 2) + 12;
				uk_rbox (canvas, lx + cw - fw - 4, y + rowh / 2 - 10, fw, 20, 6, C_FIELD, C_FIELD);
				uk_text_c (canvas, lx + cw - fw - 4, y + rowh / 2 - 10, fw, 20, f, C_FIELD_TEXT, 2);
			}
			else if (g_nh > 1)
			{
				int px = -1, pl = 0;
				for (int k = 0; k < g_nh; k++)
				{
					unsigned s = g_hist[(g_h0 + k) % HIST];
					int lv = (s >> g) & 1;
					int x = lx + cw - (g_nh - 1 - k) * cw / HIST;
					int yy = lv ? hi : lo;
					if (px >= 0)
					{
						canvas.fillRect (px, (pl ? hi : lo) - 1, x - px + 1, 2, col);
						if (lv != pl) canvas.fillRect (x - 1, hi, 2, lo - hi, col);
						if (pl) canvas.fillRect (px, hi + 1, x - px, lo - hi - 1, uk_mix (C_FIELD, col, 40));
					}
					px = x; pl = lv; (void) yy;
				}
			}
			y += rowh;
		}
		if (g_paused) { const char *ps = TR ("PAUSED"); uk_text_l (canvas, width - 14 - uk_text_w (ps, 2), 4, 18, ps, C_ACCENT, 2); }
	}
};

// ---- the I2C bus --------------------------------------------------------------------------------------------
// A BME280's readings, by the datasheet's integer formulas (BST-BME280-DS002 4.2.3, 8.2).
static bool bme280 (int a, char *out, int cap)
{
	if (gk_i2c_reg_read (a, 0xD0) != 0x60) return false;
	unsigned char c[26], h[7], d[8];
	unsigned char r = 0x88;
	if (gk_i2c_write_read (a, &r, 1, c, 26) < 0) return false;
	r = 0xE1; if (gk_i2c_write_read (a, &r, 1, h, 7) < 0) return false;
	gk_i2c_reg_write (a, 0xF2, 1);				// humidity x1
	gk_i2c_reg_write (a, 0xF4, 0x25);			// temperature x1, pressure x1, forced
	kapi_msleep (10);
	r = 0xF7; if (gk_i2c_write_read (a, &r, 1, d, 8) < 0) return false;
	#define U16(i) ((int) (c[i] | c[(i) + 1] << 8))
	#define S16(i) ((int) (short) (c[i] | c[(i) + 1] << 8))
	int T1 = U16 (0), T2 = S16 (2), T3 = S16 (4);
	int P1 = U16 (6), P2 = S16 (8), P3 = S16 (10), P4 = S16 (12), P5 = S16 (14), P6 = S16 (16), P7 = S16 (18), P8 = S16 (20), P9 = S16 (22);
	int H1 = c[25], H2 = (short) (h[0] | h[1] << 8), H3 = h[2], H4 = (signed char) h[3] * 16 | (h[4] & 15), H5 = (signed char) h[5] * 16 | h[4] >> 4, H6 = (signed char) h[6];
	int adcP = d[0] << 12 | d[1] << 4 | d[2] >> 4, adcT = d[3] << 12 | d[4] << 4 | d[5] >> 4, adcH = d[6] << 8 | d[7];
	int v1 = (((adcT >> 3) - (T1 << 1)) * T2) >> 11;
	int v2 = (((((adcT >> 4) - T1) * ((adcT >> 4) - T1)) >> 12) * T3) >> 14;
	int tf = v1 + v2, T = (tf * 5 + 128) >> 8;		// 1/100 degree
	long long p1 = (long long) tf - 128000, p2 = p1 * p1 * P6;
	p2 += (p1 * P5) << 17; p2 += (long long) P4 << 35;
	p1 = ((p1 * p1 * P3) >> 8) + ((p1 * P2) << 12);
	p1 = (((1LL << 47) + p1) * P1) >> 33;
	long long P = 0;
	if (p1 != 0)
	{
		P = 1048576 - adcP; P = (((P << 31) - p2) * 3125) / p1;
		p1 = ((long long) P9 * (P >> 13) * (P >> 13)) >> 25; p2 = ((long long) P8 * P) >> 19;
		P = ((P + p1 + p2) >> 8) + ((long long) P7 << 4);	// 1/256 Pa
	}
	int x = tf - 76800;
	x = (((((adcH << 14) - (H4 << 20) - (H5 * x)) + 16384) >> 15) * (((((((x * H6) >> 10) * (((x * H3) >> 11) + 32768)) >> 10) + 2097152) * H2 + 8192) >> 14));
	x = x - (((((x >> 15) * (x >> 15)) >> 7) * H1) >> 4);
	if (x < 0) x = 0;
	if (x > 419430400) x = 419430400;
	int Hm = (x >> 12) * 10 / 1024;				// 1/10 %
	long long hPa10 = P / 256 / 10;
	snprintf (out, cap, "%d.%d C   %lld.%lld hPa   %d.%d %%", T / 100, (T < 0 ? -T : T) % 100 / 10, hPa10 / 10, hPa10 % 10, Hm / 10, Hm % 10);
	return true;
	#undef U16
	#undef S16
}

// A few capitals in 5 x 7 for the SSD1306 test (the columns, bit 0 at the top).
static const struct { char c; unsigned char col[5]; } GLYPHS[] = {
	{ 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } }, { 'B', { 0x7F, 0x49, 0x49, 0x49, 0x36 } },
	{ 'G', { 0x3E, 0x41, 0x49, 0x49, 0x7A } }, { 'I', { 0x00, 0x41, 0x7F, 0x41, 0x00 } },
	{ 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } }, { 'N', { 0x7F, 0x04, 0x08, 0x10, 0x7F } },
	{ 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } }, { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
	{ 'X', { 0x63, 0x14, 0x08, 0x14, 0x63 } }, { 'Y', { 0x07, 0x08, 0x70, 0x08, 0x07 } },
	{ ' ', { 0, 0, 0, 0, 0 } },
};
static void oled_text (unsigned char *page, const char *s, int x0)
{
	for (; *s; s++, x0 += 12)
		for (unsigned i = 0; i < sizeof GLYPHS / sizeof GLYPHS[0]; i++)
			if (GLYPHS[i].c == *s)
				for (int k = 0; k < 5 && x0 + 2 * k + 1 < 128; k++)
				{
					// twice as large: each column twice, each row twice (two pages)
					unsigned v = 0; for (int b = 0; b < 7; b++) if (GLYPHS[i].col[k] >> b & 1) v |= 3u << (2 * b);
					for (int dx = 0; dx < 2; dx++) { page[x0 + 2 * k + dx] = (unsigned char) v; page[128 + x0 + 2 * k + dx] = (unsigned char) (v >> 8); }
				}
}
static void oled_test (int a)
{
	static const unsigned char INIT[] = { 0x00, 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00,
					       0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
					       0x21, 0, 127, 0x22, 0, 7 };
	if (gk_i2c_write (a, INIT, sizeof INIT) < 0) { snprintf (g_i2cMsg, sizeof g_i2cMsg, TR ("The display at 0x%02X did not answer."), a); return; }
	static unsigned char buf[1 + 1024];
	buf[0] = 0x40;
	unsigned char *fb = buf + 1;
	memset (fb, 0, 1024);
	for (int x = 0; x < 128; x++) { fb[x] |= 0x01; fb[7 * 128 + x] |= 0x80; }	// a frame
	for (int p = 0; p < 8; p++) { fb[p * 128] = 0xFF; fb[p * 128 + 127] = 0xFF; }
	oled_text (fb + 1 * 128, "ONYX", 40);
	oled_text (fb + 4 * 128, "GPIO LAB", 16);
	for (int x = 8; x < 120; x += 4) fb[6 * 128 + x] |= 0x18;
	gk_i2c_write (a, buf, sizeof buf);
	snprintf (g_i2cMsg, sizeof g_i2cMsg, TR ("A test picture was sent to the display at 0x%02X."), a);
}

class I2CView : public Widget
{
public:
	I2CView (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		unsigned bg = bgColor (), dim = uk_mix (bg, C_TEXT, 140);
		canvas.clear (bg);
		// the 128 addresses, 8 rows of 16
		int cs = 17, gx = 26, gy = 22;
		for (int c = 0; c < 16; c++) { char t[4]; snprintf (t, sizeof t, "%X", c); uk_text_c (canvas, gx + c * cs, 2, cs, 18, t, dim); }
		for (int r = 0; r < 8; r++)
		{
			char t[4]; snprintf (t, sizeof t, "%X0", r); uk_text_l (canvas, 0, gy + r * cs, cs, t, dim);
			for (int c = 0; c < 16; c++)
			{
				int a = r * 16 + c;
				bool res = a < 0x08 || a > 0x77, on = g_found > 0 && (g_map[a / 8] >> (a % 8) & 1);
				unsigned f = on ? C_ACCENT : res ? uk_mix (bg, C_TEXT, 14) : C_FIELD;
				uk_rbox (canvas, gx + c * cs + 1, gy + r * cs + 1, cs - 2, cs - 2, 3, f, f);
				if (!on && !res) uk_rline (canvas, gx + c * cs + 1, gy + r * cs + 1, cs - 2, cs - 2, 3, uk_tone (C_BG, 80), 120);
			}
		}
		int ty = gy + 8 * cs + 10, rx = gx + 16 * cs + 18;
		if (g_found < 0) uk_text_l (canvas, 0, ty, 20, TR ("Not scanned yet: Scan opens the bus (GPIO 2 SDA, GPIO 3 SCL)."), dim);
		else
		{
			char t[96]; snprintf (t, sizeof t, g_found == 1 ? TR ("1 device answered.") : TR ("%d devices answered."), g_found);
			uk_text_l (canvas, 0, ty, 20, t, C_TEXT, 2);
		}
		if (g_i2cMsg[0]) uk_text_l (canvas, 0, ty + 22, 20, g_i2cMsg, dim);
		// what was found, what it is
		int y = 2;
		uk_text_l (canvas, rx, y, 18, TR ("FOUND"), dim, 2); y += 22;
		for (int a = 0; a < 128 && g_found > 0; a++)
		{
			if (!(g_map[a / 8] >> (a % 8) & 1)) continue;
			char t[128]; const char *gs = gk_i2c_guess (a);
			snprintf (t, sizeof t, "0x%02X  %s", a, *gs ? TR (gs) : TR ("unknown device"));
			uk_text_l (canvas, rx, y, 20, t, C_TEXT); y += 22;
			if ((a == 0x76 || a == 0x77) && g_bme[0]) { uk_text_l (canvas, rx + 46, y, 20, g_bme, C_ACCENT, 2); y += 24; }
		}
		// the simulated display's picture
		static unsigned char oled[1024];
		if (gk_available () == 2 && g_found > 0 && (g_map[0x3C / 8] >> (0x3C % 8) & 1) && gk_sim_display (oled))
		{
			int ox = rx, oy = y + 6, s = 2;
			uk_text_l (canvas, ox, oy, 18, TR ("THE DISPLAY AT 0x3C (SIMULATED)"), dim, 2); oy += 22;
			uk_rbox (canvas, ox - 6, oy - 6, 128 * s + 12, 64 * s + 12, 6, 0x0A0A0C, 0x0A0A0C);
			for (int py = 0; py < 64; py++)
				for (int px = 0; px < 128; px++)
					if (oled[(py / 8) * 128 + px] >> (py % 8) & 1) canvas.fillRect (ox + px * s, oy + py * s, s, s, 0x8FE3FF);
		}
	}
};

// ---- the edges ------------------------------------------------------------------------------------------------
class EdgeView : public Widget
{
public:
	EdgeView (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override
	{
		unsigned bg = bgColor (), dim = uk_mix (C_FIELD, C_FIELD_TEXT, 130);
		canvas.clear (bg);
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD);
		uk_text_l (canvas, 12, 6, 18, TR ("TIME (S)"), dim, 2);
		uk_text_l (canvas, 110, 6, 18, TR ("PIN"), dim, 2);
		uk_text_l (canvas, 190, 6, 18, TR ("EDGE"), dim, 2);
		int rows = (height - 34) / 20;
		if (g_nlog == 0) uk_text_c (canvas, 0, 0, width, height, TR ("No edge yet: tick \"Log its edges\" on an input."), dim);
		for (int i = 0; i < rows && i < g_nlog; i++)
		{
			const gk_event &e = g_log[(g_log0 + g_nlog - 1 - i) % NLOG];	// (the newest first)
			unsigned long long us = e.us > g_t0 ? e.us - g_t0 : 0;
			char t[48]; int y = 28 + i * 20;
			snprintf (t, sizeof t, "%llu.%06llu", us / 1000000, us % 1000000); uk_text_l (canvas, 12, y, 20, t, C_FIELD_TEXT);
			snprintf (t, sizeof t, "GPIO%d", e.pin); uk_text_l (canvas, 110, y, 20, t, C_FIELD_TEXT, 2);
			const char *es = e.edge == GK_RISING ? TR ("rising  (to high)") : TR ("falling (to low)");
			uk_text_l (canvas, 190, y, 20, es, e.edge == GK_RISING ? 0x3B9A4A : 0xC05A2A);
			if (e.lost) { int lx = 190 + uk_text_w (es) + 14; uk_text_l (canvas, lx < 340 ? 340 : lx, y, 20, TR ("(edges lost before)"), dim); }
		}
		// the counts
		int x = width - 170, y = 28;
		uk_text_l (canvas, x, 6, 18, TR ("COUNTED"), dim, 2);
		for (int p = 0; p < GK_PINS; p++)
		{
			if (!g_edgeCount[p]) continue;
			char t[48]; snprintf (t, sizeof t, "GPIO%d  %u", p, g_edgeCount[p]);
			uk_text_l (canvas, x, y, 20, t, C_FIELD_TEXT); y += 20;
			if (y > height - 20) break;
		}
	}
};

// ---- the actions ------------------------------------------------------------------------------------------------
static void refresh_pins (void)
{
	if (gk_info (g_pin, GK_PINS) < 0) memset (g_pin, 0, sizeof g_pin);
}
static void redraw_all (void)
{
	((Widget *) g_header)->invalidate (true);
	((Widget *) g_panel)->invalidate (true);
	((Widget *) g_chart)->invalidate (true);
	((Widget *) g_i2c)->invalidate (true);
	((Widget *) g_edges)->invalidate (true);
}

static void apply_pwm (int g)
{
	int r = gk_pwm (g, FREQ_HZ[g_freqIx[g]], g_duty[g] * 10);
	if (r < 0) fail (r, "PWM");
	else { char t[160]; snprintf (t, sizeof t, TR ("GPIO %d: PWM at %d Hz, %d.%d %% high."), g, FREQ_HZ[g_freqIx[g]], g_duty[g] / 10, g_duty[g] % 10); set_status (t); }
}
static void show_duty (void) { char t[64]; snprintf (t, sizeof t, TR ("Duty  %d.%d %%"), g_duty[g_sel] / 10, g_duty[g_sel] % 10); g_lbDuty->setText (t); }
static void on_mode (Widget &)
{
	static const int MODES[] = { GK_FREE, GK_IN, GK_IN_PULLUP, GK_IN_PULLDOWN, GK_OUT, GK_PWM };
	int m = MODES[g_modes->selected < 0 ? 0 : g_modes->selected];
	int r;
	g_blink[g_sel] = false;
	if (m == GK_PWM)
	{
		if (!(g_pin[g_sel].flags & GK_F_PWM)) { set_status (TR ("PWM is on GPIO 12, 13, 18 and 19 only.")); refresh_pins (); sync_controls (); return; }
		apply_pwm (g_sel);
	}
	else
	{
		r = gk_mode (g_sel, m);
		if (r < 0) fail (r, TR ("Mode"));
		else
		{
			char t[96];
			if (m == GK_FREE) snprintf (t, sizeof t, TR ("GPIO %d: given back."), g_sel);
			else snprintf (t, sizeof t, TR ("GPIO %d: %s."), g_sel, TR (gk_mode_name (m)));
			set_status (t);
		}
	}
	refresh_pins (); sync_controls (); redraw_all ();
}
static void toggle_level (void)
{
	if (g_pin[g_sel].mode != GK_OUT) return;
	g_blink[g_sel] = false;
	int r = gk_toggle (g_sel);
	if (r < 0) fail (r, TR ("Output"));
	refresh_pins (); sync_controls (); redraw_all ();
}
static void on_level (Widget &) { toggle_level (); }
static void on_blink (Widget &) { g_blink[g_sel] = g_cbBlink->checked; }
static void on_edges (Widget &)
{
	int r = gk_edges (g_sel, g_cbEdges->checked ? GK_BOTH : 0);
	if (r < 0) { fail (r, TR ("Edges")); g_cbEdges->checked = false; g_cbEdges->invalidate (true); }
	refresh_pins ();
}
static void on_watch (Widget &)
{
	if (g_cbWatch->checked && !watched (g_sel))
	{
		int i = 0; while (i < NWATCH && g_watch[i] >= 0) i++;
		if (i == NWATCH) { for (int k = 0; k < NWATCH - 1; k++) g_watch[k] = g_watch[k + 1]; i = NWATCH - 1; }
		g_watch[i] = g_sel;
	}
	if (!g_cbWatch->checked) for (int i = 0; i < NWATCH; i++) if (g_watch[i] == g_sel) g_watch[i] = -1;
	((Widget *) g_chart)->invalidate (true);
}
static void on_freq (Widget &) { g_freqIx[g_sel] = g_ddFreq->sel; if (g_pin[g_sel].mode == GK_PWM) apply_pwm (g_sel); refresh_pins (); sync_controls (); redraw_all (); }
static void on_duty (Widget &)
{
	g_duty[g_sel] = g_slDuty->value;
	show_duty ();
	if (g_pin[g_sel].mode == GK_PWM) apply_pwm (g_sel);
	refresh_pins (); redraw_all ();
}
static void on_drive (Widget &)
{
	int d = g_drive->selected;
	gk_sim_input (g_sel, d == 0 ? -1 : d == 1 ? 0 : 1);
	refresh_pins (); redraw_all ();
}
static void on_pause (Widget &) { g_paused = !g_paused; snprintf (g_btPause->text, sizeof g_btPause->text, "%s", g_paused ? TRC ("chart", "Run") : TR ("Pause")); g_btPause->invalidate (true); g_chart->invalidate (true); }
static void do_scan (void)
{
	int r = gk_i2c_open (0);
	if (r < 0) { fail (r, "I2C"); g_found = -1; }
	else
	{
		g_found = gk_i2c_scan (g_map);
		if (g_found < 0) { fail (g_found, TR ("I2C scan")); g_found = -1; }
		g_bme[0] = 0;
		for (int a = 0x76; a <= 0x77 && g_found > 0; a++) if (g_map[a / 8] >> (a % 8) & 1) bme280 (a, g_bme, sizeof g_bme);
		set_status (TR ("The I2C bus scanned."));
	}
	refresh_pins (); sync_controls (); redraw_all ();
}
static void on_scan (Widget &) { do_scan (); }
static void on_oled (Widget &)
{
	int a = g_found > 0 && (g_map[0x3C / 8] >> 4 & 1) ? 0x3C : g_found > 0 && (g_map[0x3D / 8] >> 5 & 1) ? 0x3D : -1;
	if (a < 0) { snprintf (g_i2cMsg, sizeof g_i2cMsg, "%s", TR ("No SSD1306 display found (0x3C or 0x3D).")); }
	else oled_test (a);
	g_i2c->invalidate (true);
}
static void on_tab (Widget &)
{
	g_tab = g_tabs->selected < 0 ? 0 : g_tabs->selected;
	g_chart->hidden = g_tab != 0; g_btPause->hidden = g_tab != 0;
	g_i2c->hidden = g_tab != 1; g_btScan->hidden = g_tab != 1; g_btOled->hidden = g_tab != 1;
	g_edges->hidden = g_tab != 2;
	redraw_all ();
	Root::current ()->invalidate (true);
}

// The controls follow the chosen pin.
static void sync_controls (void)
{
	const gk_pin_info &p = g_pin[g_sel];
	bool usable = !(p.flags & GK_F_RESERVED) && (p.owner == 0 || mine (g_sel));
	bool bus = p.mode == GK_I2C || p.mode == GK_SPI;
	g_modes->hidden = !usable || bus;
	if (usable && !bus)
	{
		int s = p.mode == GK_FREE ? 0 : p.mode == GK_IN ? 1 : p.mode == GK_IN_PULLUP ? 2 : p.mode == GK_IN_PULLDOWN ? 3 : p.mode == GK_OUT ? 4 : p.mode == GK_PWM ? 5 : -1;
		g_modes->selected = s;
		g_modes->setEnabled (5, (p.flags & GK_F_PWM) != 0);
	}
	bool out = usable && p.mode == GK_OUT, pwm = usable && (p.flags & GK_F_PWM) && p.mode == GK_PWM, in = usable && is_input (p.mode);
	g_btLevel->hidden = !out; g_cbBlink->hidden = !out;
	snprintf (g_btLevel->text, sizeof g_btLevel->text, "%s", p.level ? TR ("Set Low") : TR ("Set High"));
	g_cbBlink->checked = g_blink[g_sel];
	g_ddFreq->hidden = !pwm; g_slDuty->hidden = !pwm; g_lbDuty->hidden = !pwm; g_lbFreq->hidden = !pwm;
	if (pwm)
	{
		g_ddFreq->sel = g_freqIx[g_sel];
		g_slDuty->value = g_duty[g_sel];
		show_duty ();
	}
	g_cbEdges->hidden = !in; g_cbEdges->checked = (p.flags & GK_F_EDGES) != 0;
	bool sim = gk_available () == 2;
	g_drive->hidden = !(in && sim); g_lbDrive->hidden = !(in && sim);
	g_cbWatch->checked = watched (g_sel); g_cbWatch->hidden = false;
	Widget *const all[] = { g_modes, g_btLevel, g_cbBlink, g_ddFreq, g_slDuty, g_lbDuty, g_lbFreq, g_cbEdges, g_drive, g_lbDrive, g_cbWatch };
	for (Widget *w : all) w->invalidate (true);
	g_header->invalidate (true);
	g_panel->invalidate (true);
	if (g_view == 1 && g_ed) apply_view ();		// (the Code view: the pin's controls stay hidden)
}

static void sync_sim_switch (void)
{
	if (!g_simSw) return;
	g_simSw->setOn (gk_available () == 2);
	g_simSw->disabled = !g_hasHw;		// (no GPIO here: the simulator only)
	g_simSw->invalidate (true);
}
static void use_sim_as (bool on)
{
	if (g_running) { set_status (TR ("Stop the program first: then switch the simulator.")); sync_sim_switch (); return; }
	if (on == (gk_available () == 2)) { sync_sim_switch (); return; }
	gk_release ();
	gk_sim (on ? 1 : 0);
	set_status (gk_available () == 2 ? TR ("The simulator drives the pins: nothing touches the real header.") : TR ("The Raspberry Pi's header."));
	memset (g_blink, 0, sizeof g_blink);
	g_found = -1;
	if (!on && !g_hasHw) set_status (TR ("No GPIO on this system (a PC, or a system older than kapi 92): the simulator stays."));
	sync_sim_switch ();
	refresh_pins (); sync_controls (); redraw_all ();
	if (g_root) g_root->invalidate (true);
}
static void use_sim (void) { use_sim_as (gk_available () != 2); }
static void on_sim_switch (Widget &) { use_sim_as (g_simSw->on); }
static void release_all (void)
{
	gk_release ();
	memset (g_blink, 0, sizeof g_blink);
	g_found = -1;
	set_status (TR ("Every pin given back: they are inputs again."));
	refresh_pins (); sync_controls (); redraw_all ();
}

// The demonstration: an LED on GPIO 17, a servo on GPIO 18, a button on GPIO 27 (pulled up: pressed = 0),
// a switch on GPIO 22, the bus scanned, the display's test.
static void demo (void)
{
	gk_mode (17, GK_OUT); g_blink[17] = true;
	g_freqIx[18] = 0; g_duty[18] = 75; gk_servo (18, 1500);
	gk_mode (27, GK_IN_PULLUP); gk_edges (27, GK_BOTH);
	gk_mode (22, GK_IN_PULLDOWN);
	g_btnSim = gk_available () == 2;
	do_scan ();
	if (g_found > 0) oled_test (0x3C);
	g_sel = 18;
	set_status (TR ("The demonstration: an LED on GPIO 17, a servo on GPIO 18, a button on GPIO 27."));
}

class LabRoot : public Root
{
public:
	unsigned frames;
	LabRoot () : Root (W, H, TR ("GPIO Lab")), frames (0) {}
	void onDraw () override
	{
		Root::onDraw ();
		// the warning: 3.3 V
		unsigned amber = 0xF6C343, ink = 0x3A2A00;
		uk_rbox (canvas, 12, 8, width - 24 - 200, BANNER - 14, 8, amber, uk_mix (amber, 0xE0A020, 128));
		VPath t; int tx = 32, ty = 25;
		int tri[6] = { V (tx), V (ty - 10), V (tx + 11), V (ty + 9), V (tx - 11), V (ty + 9) };
		t.poly (tri, 3); t.fill (canvas, ink);
		uk_text_c (canvas, tx - 6, ty - 7, 12, 16, "!", amber, 2);
		const char *w1 = TR ("3.3 V only: never connect 5 V to a GPIO pin."), *w2 = TR ("16 mA a pin at most: an LED through 330 ohm.");
		int x2 = 52 + uk_text_w (w1, 2) + 10;
		uk_text_l (canvas, 52, 11, 28, w1, ink, 2);
		if (x2 + uk_text_w (w2) <= width - 224) uk_text_l (canvas, x2, 11, 28, w2, ink);	// (where the band has the room)
		// what drives the pins, and the last message
		const char *who = gk_available () == 2 ? TR ("SIMULATOR") : gk_available () == 1 ? TR ("RASPBERRY PI HEADER") : TR ("NO GPIO");
		int ww = uk_text_w (who, 2) + 20;
		unsigned bc = gk_available () == 2 ? 0x8E6BD8 : gk_available () == 1 ? 0x3B9A4A : 0x9A3B3B;
		uk_rbox (canvas, width - ww - 12, height - 30, ww, 22, 11, bc, bc);
		uk_text_c (canvas, width - ww - 12, height - 30, ww, 22, who, 0xFFFFFF, 2);
		uk_text_l (canvas, 16, height - 30, 22, g_status, uk_mix (bg, C_TEXT, 150));
	}
	void onTick () override
	{
		frames++;
		unsigned long long now = gk_now_us ();
		// the blinks: 2 Hz
		if (now - g_blinkUs >= 250000)
		{
			g_blinkUs = now;
			for (int p = 0; p < GK_PINS; p++) if (g_blink[p]) gk_toggle (p);
		}
		// the demonstration's button: pressed a little now and then (the simulator only)
		if (g_btnSim)
		{
			unsigned ph = (unsigned) (now / 1000 % 2400);
			gk_sim_input (27, (ph > 600 && ph < 900) || (ph > 1500 && ph < 1650) ? 0 : 1);
			gk_sim_input (22, ph > 1200 ? 1 : 0);
		}
		// the levels, a sample a frame
		if (!g_paused)
		{
			unsigned s = gk_read_all ();
			if (g_nh < HIST) g_hist[(g_h0 + g_nh++) % HIST] = s;
			else { g_hist[g_h0] = s; g_h0 = (g_h0 + 1) % HIST; }
		}
		// the edges
		gk_event ev[64];
		int n = g_running ? 0 : gk_events (ev, 64, 0);	// (a program running takes them: ON PIN; LabHost logs them)
		log_events (ev, n);
		unsigned before = 0, after = 0;
		for (int p = 0; p < GK_PINS; p++) before = before * 31 + g_pin[p].level + g_pin[p].mode * 7 + g_pin[p].owner;
		refresh_pins ();
		for (int p = 0; p < GK_PINS; p++) after = after * 31 + g_pin[p].level + g_pin[p].mode * 7 + g_pin[p].owner;
		if (after != before) { g_header->invalidate (true); g_panel->invalidate (true); if (g_pin[g_sel].mode == GK_OUT) sync_controls (); }
		if (g_tab == 0 && !g_paused && (frames & 1)) g_chart->invalidate (true);
		if (g_tab == 2 && (n > 0 || g_running)) g_edges->invalidate (true);
		if (g_tab == 1 && (frames % 60) == 0 && g_bme[0] && g_found > 0)
		{
			for (int a = 0x76; a <= 0x77; a++) if (g_map[a / 8] >> (a % 8) & 1) bme280 (a, g_bme, sizeof g_bme);
			g_i2c->invalidate (true);
		}
		if ((frames % 30) == 0) invalidate (true);	// (the status line)
	}
	bool onKey (long k) override;
};

// ---- the Code view: a mini IDE ----------------------------------------------------------------------------------
// The program is Onyx BASIC (its GPIO statements: docs/04 §13), compiled and run HERE, in GPIO Lab's own process:
// its pins are GPIO Lab's, so the header shows them live. The VM runs from the main loop (main: never from a
// button's callback); it gives the window a round through the host's poll () and its waits (pump), and the
// statement hook (Host::onStatement) lights the line it runs -- slowed down by the Speed slider, or a line at a
// time (Step). The program's pins stay as it left them when it ends (Board > Release Every Pin gives them back;
// a program run by /bin/basic gives them back at its end).
#define CON_LINES	300
class ConsoleView : public Widget
{
public:
	struct Line { char t[160]; unsigned char kind; };	// 0 the program's, 1 an error, 2 GPIO Lab's
	Line lines[CON_LINES]; int n = 0, first = 0; bool open = false;	// open: the last line goes on
	ConsoleView (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	Line &at (int i) { return lines[(first + i) % CON_LINES]; }
	void newLine (int kind)
	{
		if (n == CON_LINES) { first = (first + 1) % CON_LINES; n--; }
		Line &L = at (n++); L.t[0] = 0; L.kind = (unsigned char) kind; open = true;
	}
	void put (const char *s, int len, int kind)
	{
		for (int i = 0; i < len; i++)
		{
			if (!open || at (n - 1).kind != kind) newLine (kind);
			if (s[i] == '\n') { open = false; continue; }
			if (s[i] == '\r') continue;
			Line &L = at (n - 1); int k = (int) strlen (L.t);
			if (k < (int) sizeof L.t - 1) { L.t[k] = s[i]; L.t[k + 1] = 0; }
		}
		invalidate (true);
	}
	void say (const char *s, int kind) { if (open) open = false; put (s, (int) strlen (s), kind); open = false; }
	void clear () { n = 0; first = 0; open = false; invalidate (true); }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		unsigned bg = 0x1B1F23;
		uk_rbox (canvas, 0, 0, width, height, 6, bg, bg);
		UkFaceScope fs (g_mono);
		int lh = uk_fh () + 2, rows = (height - 10) / lh;
		int from = n > rows ? n - rows : 0;
		for (int i = from, y = 5; i < n; i++, y += lh)
		{
			Line &L = at (i);
			unsigned c = L.kind == 1 ? 0xFF7A6E : L.kind == 2 ? 0x8FB7D9 : 0xE6E6E6;
			uk_text_l (canvas, 10, y, lh, L.t, c);
		}
		if (n == 0) uk_text_l (canvas, 10, 5, lh, TR ("(what the program PRINTs comes here)"), 0x707880);
	}
};

static bool pump (void)				// a round of the window while the program runs
{
	if (!g_root->step ()) { g_quit = true; return false; }
	return true;
}
static const int SPEED_MS[11] = { 0, 900, 600, 400, 250, 150, 90, 50, 25, 8, 0 };	// a line's wait, by the Speed

class LabHost : public bas::Host
{
public:
	unsigned lastPump = 0;
	int curLine = 0;
	void out (const char *s, int n) override { g_con->put (s, n, 0); }
	int inputLine (char *buf, int cap) override
	{
		(void) cap; buf[0] = 0;
		g_con->say (TR ("INPUT: no keyboard input in GPIO Lab -- the program stops here."), 1);
		return -1;
	}
	int inkey (char *o) override
	{
		if (!g_nkeyQ) return 0;
		long k = g_keyQ[0];
		for (int i = 1; i < g_nkeyQ; i++) g_keyQ[i - 1] = g_keyQ[i];
		g_nkeyQ--;
		if (k > 0 && k < 256) { o[0] = (char) k; return 1; }
		o[0] = 0; o[1] = k == KEY_UP ? 72 : k == KEY_DOWN ? 80 : k == KEY_LEFT ? 75 : k == KEY_RIGHT ? 77 : 0;
		return o[1] ? 2 : 0;
	}
	int keyPending (char *o) override { if (!g_nkeyQ) return 0; long k = g_keyQ[0]; if (k > 0 && k < 256) { o[0] = (char) k; return 1; } return 0; }
	void cls (int) override { g_con->clear (); }
	bool poll () override
	{
		unsigned now = kapi_clock_us ();
		if ((unsigned) (now - lastPump) >= 15000u) { lastPump = now; pump (); }
		return !g_stopReq && !g_quit;
	}
	void sleepMs (int ms) override
	{
		unsigned long long end = gk_now_us () + (unsigned long long) (ms > 0 ? ms : 0) * 1000u;
		for (;;)
		{
			if (!pump () || g_stopReq) return;
			unsigned long long now = gk_now_us ();
			if (now >= end) return;
			unsigned long long left = (end - now) / 1000u;
			kapi_msleep ((unsigned) (left > 10 ? 10 : left > 0 ? left : 1));
		}
	}
	double timer () override { return (double) (gk_now_us () / 1000u) / 1000.0; }
	unsigned clockUs () override { return kapi_clock_us (); }
	unsigned seed () override { return (unsigned) gk_now_us (); }
	void notify (const char *title, const char *text) override { char t[200]; snprintf (t, sizeof t, TR ("%s: %s"), title, text); g_con->say (t, 2); }
	// a line about to run: lit, then the Speed's wait -- or, step by step, the wait for Step
	bool onStatement (int line) override
	{
		curLine = line;
		int sp = g_speed ? g_speed->value : 10;
		if (g_stepMode || sp < 10)
		{
			if (g_ed->hiLine != line) { g_ed->hiLine = line; g_ed->showLine (line - 1); g_ed->invalidate (true); }
			if (g_stepMode)
			{
				g_waitStep = true;
				while (g_waitStep && g_stepMode && !g_stopReq && pump ()) kapi_msleep (10);
			}
			else sleepMs (SPEED_MS[sp]);
		}
		else if (poll () && g_ed->hiLine != line && (unsigned) (kapi_clock_us () - lastPump) < 2000u)
		{ g_ed->hiLine = line; g_ed->invalidate (true); }	// (full speed: the line shown at each round of the window)
		return !g_stopReq && !g_quit;
	}
	// GPIO: GPIOKit itself (linked in): the program's pins are GPIO Lab's
	bool i2c = false, spi = false;
	int gpio (int op, int a, int b, int c, const char *in, int inLen, char *out, int outCap) override
	{
		int r;
		switch (op)
		{
		case GP_MODE:	return gk_mode (a, b);
		case GP_WRITE:	return gk_write (a, b);
		case GP_READ:	return gk_read (a);
		case GP_PWM:	return gk_pwm (a, b, c);
		case GP_SERVO:	return gk_servo (a, b);
		case GP_EDGES:	return gk_edges (a, b);
		case GP_EVENTS:
		{
			gk_event ev[32]; int max = outCap / 2 < 32 ? outCap / 2 : 32;
			r = max > 0 ? gk_events (ev, max, 0) : 0;
			for (int k = 0; k < r; k++) { out[2 * k] = (char) ev[k].pin; out[2 * k + 1] = (char) ev[k].edge; }
			log_events (ev, r);			// (the Edges tab sees them too)
			return r;
		}
		case GP_FREE:	if (a < 0) { i2c = spi = false; return gk_release (); } return gk_mode (a, GK_FREE);
		case GP_SIM:	return gk_sim (a);
		case GP_I2C_OPEN: r = gk_i2c_open (a); i2c = r >= 0; return r;
		case GP_SPI_OPEN: r = gk_spi_open (a, b); spi = r >= 0; return r;
		default: break;
		}
		if (op == GP_SPI_XFER) { if (!spi && (r = gk_spi_open (0, 0)) < 0) return r; spi = true; return gk_spi_transfer (a, in, out, inLen); }
		if (!i2c) { if ((r = gk_i2c_open (0)) < 0) return r; i2c = true; }
		switch (op)
		{
		case GP_I2C_REG_READ:	return gk_i2c_reg_read (a, b);
		case GP_I2C_REG_WRITE:	return gk_i2c_reg_write (a, b, c);
		case GP_I2C_XFER:	r = gk_i2c_write_read (a, in, inLen, out, b); return r < 0 ? r : b > 0 ? r : 0;
		case GP_I2C_SCAN:	return outCap >= 16 ? gk_i2c_scan ((unsigned char *) out) : GP_NODEV;
		default:		return GP_NODEV;
		}
	}
	const char *gpioError (int code) override { return TR (gk_error (code)); }
};

static void log_events (const gk_event *ev, int n)
{
	for (int i = 0; i < n; i++)
	{
		if (g_nlog < NLOG) g_log[(g_log0 + g_nlog++) % NLOG] = ev[i];
		else { g_log[g_log0] = ev[i]; g_log0 = (g_log0 + 1) % NLOG; }
		if (ev[i].pin < GK_PINS) g_edgeCount[ev[i].pin]++;
	}
}

// The sketch: kept beside the app between two sessions
#define SKETCH "SD:/apps/gpiolab.app/sketch.bas"
static const char *const DEFAULT_SKETCH =
	"' GPIO Lab: write BASIC here and press Run (F5) -- the header on the left\n"
	"' shows the pins as the program drives them. Step (F8): a line at a time.\n"
	"' An LED on GPIO 17 (pin 11, through 330 ohm to GND) blinks; a button on\n"
	"' GPIO 27 (pin 13, to GND) is counted. On the simulator, click GPIO 27's\n"
	"' dot on the header to press it.\n"
	"PINMODE 17, \"OUT\"\n"
	"PINMODE 27, \"PULLUP\"\n"
	"ON PIN (27, 2) GOSUB Pressed\n"
	"FOR i = 1 TO 10\n"
	"  PIN 17 = 1: PAUSE 300\n"
	"  PIN 17 = 0: PAUSE 300\n"
	"NEXT\n"
	"PRINT \"Done:\"; presses; \"presses\"\n"
	"END\n"
	"\n"
	"Pressed:\n"
	"  presses = presses + 1\n"
	"  PRINT \"Pressed!\"; presses\n"
	"  RETURN\n";
// The same in French (the comments in UTF-8; the texts it PRINTs in ASCII: BASIC's console is code page 437)
static const char *const DEFAULT_SKETCH_FR =
	"' GPIO Lab : écrivez du BASIC ici et appuyez sur Exécuter (F5) -- le connecteur\n"
	"' à gauche montre les broches comme le programme les pilote. Pas à pas (F8) :\n"
	"' une ligne à la fois. Une LED sur le GPIO 17 (broche 11, par 330 ohms vers GND)\n"
	"' clignote ; un bouton sur le GPIO 27 (broche 13, vers GND) est compté. Sur le\n"
	"' simulateur, cliquez sur le point du GPIO 27 du connecteur pour l'enfoncer.\n"
	"MODEBROCHE 17, \"SORTIE\"\n"
	"MODEBROCHE 27, \"RAPPELHAUT\"\n"
	"SUR BROCHE (27, 2) GOSUB Appui\n"
	"POUR i = 1 JUSQUE 10\n"
	"  BROCHE 17 = 1: PAUSE 300\n"
	"  BROCHE 17 = 0: PAUSE 300\n"
	"SUITE\n"
	"AFFICHER \"Fini :\"; appuis; \"appuis\"\n"
	"FIN\n"
	"\n"
	"Appui:\n"
	"  appuis = appuis + 1\n"
	"  AFFICHER \"Appui !\"; appuis\n"
	"  RETOUR\n";
static const char *first_sketch (void) { return g_fr ? DEFAULT_SKETCH_FR : DEFAULT_SKETCH; }
static void save_sketch (void) { if (g_ed) fk_save (SKETCH, g_ed->text (), (unsigned) g_ed->length ()); }
static bool load_into_editor (const char *path)
{
	void *b = 0; unsigned n = 0;
	if (fk_load (path, &b, &n) != 0 || b == 0) return false;
	g_ed->setText ((const char *) b);
	fk_free (b);
	g_ed->clearMarks (); g_ed->hiLine = 0; g_ed->invalidate (true);
	return true;
}

static void set_run_buttons (void)
{
	snprintf (g_btRun->text, sizeof g_btRun->text, "%s", g_running && !g_stepMode ? TR ("Running") : g_running ? TR ("Continue") : TR ("Run"));
	g_btStop->disabled = !g_running;
	g_ed->readonly = g_running;
	Widget *const ws[] = { g_btRun, g_btStep, g_btStop };
	for (Widget *w : ws) w->invalidate (true);
}

// A compiler's or a run's error in the console, at its line (in French: BASIC's words in French too)
static void say_error (const bas::Error &err)
{
	char m[240], t[320];
	if (g_fr) bas::frenchMessage (err.msg, m, sizeof m);
	else snprintf (m, sizeof m, "%s", err.msg);
	snprintf (t, sizeof t, TR ("Line %d: %s"), err.line, m);
	g_con->say (t, 1);
}

// The program in the editor, compiled and run (the main loop calls it: g_runReq)
static void run_program (bool step)
{
	save_sketch ();
	g_con->clear ();
	g_ed->clearMarks (); g_ed->hiLine = 0;
	bas::Error err; err.line = 0; err.msg[0] = 0;
	bas::Program *p = bas::compile (g_ed->text (), &err);
	if (!p)
	{
		say_error (err);
		if (err.line > 0) { g_ed->addMark (err.line); g_ed->hiLine = err.line; g_ed->gotoLine (err.line - 1); }
		g_ed->invalidate (true);
		return;
	}
	bas::setManaged (p, true);			// (the VM: the statement hook lights the lines)
	LabHost host;
	host.lineHook = true;
	g_running = true; g_stepMode = step; g_waitStep = false; g_stopReq = false; g_nkeyQ = 0;
	set_run_buttons ();
	((Widget *) g_header)->setFocus ();		// (the keys go to the program: INKEY$)
	g_con->say (step ? TR ("Step by step: F8 (or Step) runs the lit line.") : TR ("Running..."), 2);
	unsigned long long t0 = gk_now_us ();
	int r = bas::run (p, host, &err);
	unsigned long long ms = (gk_now_us () - t0) / 1000u;
	char t[200];
	if (r != 0 && err.msg[0])
	{
		say_error (err);
		if (err.line > 0) { g_ed->addMark (err.line); g_ed->hiLine = err.line; g_ed->gotoLine (err.line - 1); }
	}
	else
	{
		snprintf (t, sizeof t, g_stopReq ? TR ("Stopped (%llu.%llu s).") : TR ("Ended (%llu.%llu s): the pins stay as it left them."), ms / 1000, ms % 1000 / 100);
		g_con->say (t, 2);
		g_ed->hiLine = 0;
	}
	bas::destroy (p);
	g_running = false; g_stepMode = false; g_waitStep = false;
	refresh_pins (); sync_controls (); set_run_buttons ();
	g_ed->invalidate (true);
	g_ed->setFocus ();
}

static void do_run (void)
{
	if (g_running) { g_stepMode = false; g_waitStep = false; set_run_buttons (); return; }	// (Continue)
	g_runReq = 1;
}
static void do_step (void)
{
	if (g_running) { if (g_stepMode) g_waitStep = false; else g_stepMode = true; set_run_buttons (); return; }
	g_runReq = 2;
}
static void do_stop (void) { if (g_running) g_stopReq = true; }
static void cb_run (Widget &) { do_run (); }
static void cb_step (Widget &) { do_step (); }
static void cb_stop (Widget &) { do_stop (); }
static void cb_speed (Widget &) { char t[48]; snprintf (t, sizeof t, g_speed->value >= 10 ? TR ("Speed: full") : TR ("Speed: %d"), g_speed->value); g_lbSpeed->setText (t); }
#define NEX	7
static const char *const EXAMPLES[NEX] = { TRN ("Examples..."), TRN ("Blink an LED"), TRN ("A button"), TRN ("A servo"), TRN ("A BME280 sensor (I2C)"),
	TRN ("An SSD1306 display (I2C)"), TRN ("The first sketch") };
static const char *g_exNames[NEX];		// EXAMPLES in the language (main; the Dropdown keeps the pointers)
// (in SD:/basic/examples; written in French: in its fr/)
static const char *const EXAMPLE_FILES[NEX] = { 0, "gpio_blink.bas", "gpio_button.bas", "gpio_servo.bas", "gpio_bme280.bas", "gpio_oled.bas", 0 };
static void cb_example (Widget &)
{
	int i = g_ddEx->sel;
	if (i <= 0 || g_running) return;
	if (i == 6) { g_ed->replaceAll (first_sketch (), 0); }
	else
	{
		// in French, the example written in French -- else (or none there) the English one
		char path[96], t[240];
		snprintf (path, sizeof path, "SD:/basic/examples/fr/%s", EXAMPLE_FILES[i]);
		bool ok = g_fr && load_into_editor (path);
		if (!ok) { snprintf (path, sizeof path, "SD:/basic/examples/%s", EXAMPLE_FILES[i]); ok = load_into_editor (path); }
		if (!ok) { snprintf (t, sizeof t, TR ("Cannot read %s"), path); g_con->say (t, 1); }
		else { snprintf (t, sizeof t, TR ("%s: %s"), g_exNames[i], path); g_con->say (t, 2); }
	}
	g_ddEx->sel = 0; g_ddEx->invalidate (true);
}
static void on_view (Widget &)
{
	g_view = g_viewSw->selected == 1 ? 1 : 0;
	apply_view ();
	if (g_view == 1) g_ed->setFocus (); else ((Widget *) g_header)->setFocus ();
}

// Which of the right side's widgets the view shows
static void apply_view (void)
{
	bool code = g_view == 1;
	Widget *const pins[] = { g_panel, g_modes, g_btLevel, g_cbBlink, g_ddFreq, g_slDuty, g_lbDuty, g_lbFreq, g_cbEdges, g_drive, g_lbDrive,
				 g_cbWatch, g_tabs, g_btPause, g_btScan, g_btOled, g_chart, g_i2c, g_edges };
	if (code) for (Widget *w : pins) w->hidden = true;
	else
	{
		g_panel->hidden = false; g_tabs->hidden = false;
		sync_controls ();			// (the pin's controls as its mode wants)
		g_tabs->selected = g_tab; on_tab (*g_tabs);	// (the tab's view and buttons)
	}
	Widget *const ide[] = { g_ed, g_con, g_btRun, g_btStep, g_btStop, g_speed, g_lbSpeed, g_ddEx };
	for (Widget *w : ide) w->hidden = !code;
	if (g_root) g_root->invalidate (true);
}

bool LabRoot::onKey (long k)
{
	if (k == KEY_F1 + 4) { do_run (); return true; }			// F5
	if (k == KEY_F1 + 7) { do_step (); return true; }			// F8
	if (g_running)
	{
		if (k == 27) { do_stop (); return true; }			// Esc
		if (g_nkeyQ < 16) g_keyQ[g_nkeyQ++] = k;			// INKEY$
		return true;
	}
	if (g_view == 1) return false;
	if (k == ' ') { toggle_level (); return true; }
	return ((Widget *) g_header)->onKey (k);
}

static void m_quit (void) { save_sketch (); kapi_exit (0); }
static void m_run (void) { do_run (); }
static void m_step (void) { do_step (); }
static void m_stop (void) { do_stop (); }
static void m_open (void)
{
	char path[256];
	if (g_running || !uk_file_open (path, sizeof path, g_fr ? "SD:/basic/examples/fr" : "SD:/basic/examples", TR ("BASIC programs|*.bas|All files|*"))) return;
	if (load_into_editor (path)) { g_view = 1; g_viewSw->selected = 1; g_viewSw->invalidate (true); apply_view (); g_ed->setFocus (); }
}
static void m_save (void)
{
	char path[256];
	if (!uk_file_save (path, sizeof path, "SD:/basic", "gpio.bas", TR ("BASIC programs|*.bas|All files|*"))) return;
	if (fk_save (path, g_ed->text (), (unsigned) g_ed->length ()) != 0) uk_messagebox (TR ("GPIO Lab"), TR ("The program could not be saved."), MB_OK);
	else { char t[300]; snprintf (t, sizeof t, TR ("Saved: %s"), path); g_con->say (t, 2); }
}
static void m_sim (void) { use_sim (); Root::current ()->invalidate (true); }
static void m_release (void) { release_all (); }
static void m_demo (void) { demo (); refresh_pins (); sync_controls (); redraw_all (); }

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);
	uk_lang_init ();				// the words in the system's language (before the widgets)
	g_fr = !strcmp (uk_lang (), "fr");
	bas::setDialect (bas::frenchDialect ());	// BASIC in French too, whatever the language (before the editor's words: wordList)
	for (int i = 0; i < NEX; i++) g_exNames[i] = TR (EXAMPLES[i]);
	char args[128] = "";
	kapi_get_args (args, sizeof args);
	g_hasHw = gk_available () == 1;
	if (strstr (args, "--sim") || gk_available () == 0) gk_sim (1);
	g_demo = strstr (args, "--demo") != 0;
	bool codeArg = strstr (args, "--code") != 0;
	if (strstr (args, "--tab i2c")) g_tab = 1;
	else if (strstr (args, "--tab edges")) g_tab = 2;
	for (int p = 0; p < GK_PINS; p++) { g_freqIx[p] = 3; g_duty[p] = 500; }
	g_t0 = gk_now_us ();

	int sw = 0, sh = 0;
	kapi_screen_size (&sw, &sh);
	if (sw > 0 && sw < W + 40) W = sw - 40 < 940 ? 940 : sw - 40;
	if (sh > 0 && sh < H + 80) H = sh - 80 < 600 ? 600 : sh - 80;
	LabRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	{ FtTextFace *m = new FtTextFace; if (m->open ("DejaVu Sans Mono", 14)) g_mono = m; else delete m; }
	refresh_pins ();
	set_status (gk_available () == 2 ? TR ("The simulator drives the pins: nothing touches the real header.") : TR ("Choose a pin on the header."));

	g_header = new HeaderView (12, BANNER + 6, 412, H - BANNER - 44);
	root.addChild (g_header);
	g_simSw = new ToggleSwitch (12 + 412 - 132, BANNER + 8, 132, 26, TR ("Simulator"), gk_available () == 2, on_sim_switch, root.bg);
	g_simSw->tip = TR ("On: a board in memory -- click an input's dot to drive it, nothing touches the real pins. Off: the Pi's header.");
	root.addChild (g_simSw);
	sync_sim_switch ();

	int rx = 436, rw = W - rx - 12;
	g_panel = new PinPanel (rx, BANNER + 6, rw, 214);
	root.addChild (g_panel);
	int py = BANNER + 6;
	// (the words of the segments, in the language: a SegmentedControl copies them)
	const char *const MODES[6] = { TR ("Free"), TR ("Input"), TR ("Pull-up"), TR ("Pull-down"), TR ("Output"), "PWM" };
	g_modes = new SegmentedControl (rx + 14, py + 68, rw - 28, 28, MODES, 6, 0, on_mode);
	g_modes->tip = TR ("The pin's mode (Free gives it back: an input with no pull)");
	root.addChild (g_modes);
	g_btLevel = new Button (rx + 14, py + 108, 110, 30, TR ("Set High"), on_level);
	g_btLevel->tip = TR ("The output's level (Space)");
	root.addChild (g_btLevel);
	g_cbBlink = new Checkbox (rx + 136, py + 112, 150, 24, TR ("Blink (2 Hz)"), false, on_blink, uk_mix (C_BG, C_FIELD, 140));
	root.addChild (g_cbBlink);
	g_lbFreq = new Label (rx + 14, py + 112, 80, 22, TR ("Frequency"), C_TEXT, uk_mix (C_BG, C_FIELD, 140));
	root.addChild (g_lbFreq);
	g_ddFreq = new Dropdown (rx + 94, py + 108, 150, 30, FREQS, 6, 3, on_freq);
	root.addChild (g_ddFreq);
	// the duty's label as wide as its words ("Rapport cyclique"), the slider after it
	char dt[64]; snprintf (dt, sizeof dt, TR ("Duty  %d.%d %%"), 100, 0);
	int dw = uk_text_w (dt) + 8; if (dw < 90) dw = 90;
	snprintf (dt, sizeof dt, TR ("Duty  %d.%d %%"), 50, 0);
	g_lbDuty = new Label (rx + 260, py + 112, dw, 22, dt, C_TEXT, uk_mix (C_BG, C_FIELD, 140));
	root.addChild (g_lbDuty);
	g_slDuty = new Slider (rx + 260 + dw, py + 108, rw - 274 - dw, 30, 0, 1000, 500, on_duty, uk_mix (C_BG, C_FIELD, 140));
	g_slDuty->tip = TR ("How long the pin is high in each period (a servo: 2.5 % to 12.5 %, 7.5 % the middle)");
	root.addChild (g_slDuty);
	g_lbDrive = new Label (rx + 14, py + 112, 120, 22, TR ("Driven outside"), C_TEXT, uk_mix (C_BG, C_FIELD, 140));
	root.addChild (g_lbDrive);
	const char *const DRIVE[3] = { TR ("Nothing"), TR ("Low"), TR ("High") };
	g_drive = new SegmentedControl (rx + 134, py + 108, 220, 28, DRIVE, 3, 0, on_drive);
	g_drive->tip = TR ("The simulator: what a wire brings to the input (Nothing: it floats, or follows its pull)");
	root.addChild (g_drive);
	g_cbEdges = new Checkbox (rx + 14, py + 150, 184, 24, TR ("Log its edges"), false, on_edges, uk_mix (C_BG, C_FIELD, 140));
	g_cbEdges->tip = TR ("Its rising and falling edges, with their time, in the Edges tab");
	root.addChild (g_cbEdges);
	g_cbWatch = new Checkbox (rx + 200, py + 150, 260, 24, TR ("Show in the timing chart"), true, on_watch, uk_mix (C_BG, C_FIELD, 140));
	root.addChild (g_cbWatch);

	const char *const TABS[3] = { TR ("Timing Chart"), TR ("I2C Bus"), TR ("Edges") };
	int ty = BANNER + 6 + 214 + 12;
	g_tabs = new SegmentedControl (rx, ty, 360, 28, TABS, 3, g_tab, on_tab);
	root.addChild (g_tabs);
	g_btPause = new Button (W - 12 - 90, ty, 90, 28, TR ("Pause"), on_pause);
	root.addChild (g_btPause);
	g_btScan = new Button (W - 12 - 90, ty, 90, 28, TR ("Scan"), on_scan);
	g_btScan->tip = TR ("Open the I2C bus (GPIO 2, 3) and ask every address");
	root.addChild (g_btScan);
	g_btOled = new Button (W - 12 - 90 - 8 - 130, ty, 130, 28, TR ("Test Display"), on_oled);
	g_btOled->tip = TR ("Send a test picture to an SSD1306 display found at 0x3C / 0x3D");
	root.addChild (g_btOled);
	int vy = ty + 38, vh = H - vy - 40;
	g_chart = new ChartView (rx, vy, rw, vh);  root.addChild (g_chart);
	g_i2c = new I2CView (rx, vy, rw, vh);      root.addChild (g_i2c);
	g_edges = new EdgeView (rx, vy, rw, vh);   root.addChild (g_edges);

	// the view: the pins, or the code (the mini IDE)
	const char *const VIEWS[2] = { TR ("Pins"), TR ("Code") };
	g_viewSw = new SegmentedControl (W - 12 - 192, 10, 192, 30, VIEWS, 2, 0, on_view);
	g_viewSw->tip = TR ("Pins: drive them by hand. Code: write a BASIC program and watch it drive them.");
	root.addChild (g_viewSw);
	{
		int cy = BANNER + 6, x = rx, ddx = W - 12 - 190;
		// (Step and the Speed's label as wide as their words -- "Pas à pas", "Vitesse : max" --, the slider in what is left)
		int wStep = uk_text_w (TR ("Step")) + 28; if (wStep < 70) wStep = 70;
		int wSpeed = uk_text_w (TR ("Speed: full")) + 8; if (wSpeed < 86) wSpeed = 86;
		char sp[48]; snprintf (sp, sizeof sp, TR ("Speed: %d"), 6);
		g_btRun = new Button (x, cy, 96, 30, TR ("Run"), cb_run); g_btRun->tip = TR ("Run the program (F5)"); root.addChild (g_btRun); x += 102;
		g_btStep = new Button (x, cy, wStep, 30, TR ("Step"), cb_step); g_btStep->tip = TR ("A line at a time (F8)"); root.addChild (g_btStep); x += wStep + 6;
		g_btStop = new Button (x, cy, 70, 30, TR ("Stop"), cb_stop); g_btStop->tip = TR ("Stop the program (Esc)"); root.addChild (g_btStop); x += 84;
		g_lbSpeed = new Label (x, cy + 4, wSpeed, 22, sp, C_TEXT, root.bg); root.addChild (g_lbSpeed); x += wSpeed + 2;
		int wSlider = ddx - 12 - x; if (wSlider > 120) wSlider = 120; if (wSlider < 60) wSlider = 60;
		g_speed = new Slider (x, cy, wSlider, 30, 1, 10, 6, cb_speed, root.bg);
		g_speed->tip = TR ("How fast the lines run (each lit as it runs); full: as fast as it can"); root.addChild (g_speed);
		g_ddEx = new Dropdown (ddx, cy, 190, 30, g_exNames, NEX, 0, cb_example); root.addChild (g_ddEx);
		int conH = 150, ey = cy + 40, eh = H - 40 - ey - conH - 8;
		g_ed = new CodeEdit (rx, ey, rw, eh);
		g_ed->mono = g_mono; g_ed->ui = ft_uikit_face ();
		g_ed->isKeyword = [] (const char *w, int n) -> bool {
			static char words[8000]; static int len = -1;
			if (len < 0) len = bas::wordList (words, sizeof words);
			for (int i = 0; i < len; )
			{
				int j = i; while (j < len && words[j] != ' ') j++;
				if (j - i == n) { int k = 0; while (k < n && (w[k] >= 'a' && w[k] <= 'z' ? w[k] - 32 : w[k]) == words[i + k]) k++; if (k == n) return true; }
				i = j + 1;
			}
			return false;
		};
		root.addChild (g_ed);
		g_con = new ConsoleView (rx, ey + eh + 8, rw, conH); root.addChild (g_con);
		if (!load_into_editor (SKETCH)) g_ed->setText (first_sketch ());
		set_run_buttons ();
	}

	static Menu menu;
	menu.menu (TR ("File"));
	menu.item (TR ("Open Program..."), "^O", UK_CTRL ('O'), m_open);
	menu.item (TR ("Save Program As..."), "^S", UK_CTRL ('S'), m_save);
	menu.separator ();
	menu.item (TR ("Quit"), "^Q", UK_CTRL ('Q'), m_quit);
	menu.menu (TR ("Program"));
	menu.item (TR ("Run"), "F5", KEY_F1 + 4, m_run);
	menu.item (TR ("Step"), "F8", KEY_F1 + 7, m_step);
	menu.item (TR ("Stop"), "Esc", 0, m_stop);
	menu.menu (TR ("Board"));
	menu.item (TR ("Use the Simulator"), "^M", UK_CTRL ('M'), m_sim);
	menu.item (TR ("Release Every Pin"), "^R", UK_CTRL ('R'), m_release);
	menu.separator ();
	menu.item (TR ("Demonstration Set-up"), "", 0, m_demo);
	menu.publish ();

	if (g_demo) demo ();
	refresh_pins ();
	sync_controls ();
	g_tabs->selected = g_tab;
	on_tab (*g_tabs);
	g_view = codeArg ? 1 : 0; g_viewSw->selected = g_view;
	apply_view ();
	if (g_view == 1) g_ed->setFocus (); else g_header->setFocus ();
	if (strstr (args, "--run")) g_runReq = 1;
	// the loop (Root::run's), with the programs started from it -- never from a callback: their run gives the
	// window its rounds (pump)
	root.attach ();
	while (!g_quit && root.step ())
	{
		if (g_runReq) { int r = g_runReq; g_runReq = 0; run_program (r == 2); continue; }
		kapi_msleep (16);
	}
	save_sketch ();
	gk_release ();
	return 0;
}
