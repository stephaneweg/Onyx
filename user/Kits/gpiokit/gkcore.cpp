//
// gkcore.cpp -- GPIOKit (SD:/lib/gpiokit.so; gpiokit/gpiokit.h): the header's pins, PWM, edges, I2C and
// SPI over the kernel's gpio_ctl (kapi v92, KAPI_GPIO_*), and a simulated board for a PC or an Onyx
// without the hardware.
//
// The one kit besides AppKit that reads the kernel's table itself (the user's exception: docs/03 §5.10):
// gpio_ctl is a single entry and GPIOKit is shipped with the kernel (the package onyx), so a kernel
// that moves the entry ships the GPIOKit that follows it. Everything else (time, sleeping) is AppKit's.
// Built freestanding for Onyx; compiled as it is on a PC (the simulator only, against the stand-in kernel:
// tools/tests/desktop_sim).
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
#include "gpiokit/gpiokit.h"
#if defined (__aarch64__) && !defined (GK_STANDALONE)
#include "appkit/appkit.h"
#include "kern/kapi_abi.h"
#include "lib.h"
#elif !defined (GK_STANDALONE)
#include "appkit/appkit.h"			// (a PC: the stand-in kernel's -- its clock, which a script may drive)
#endif
#ifdef GK_STANDALONE				// (a PC test with no stand-in kernel: a clock of its own, moved by the waits)
static unsigned long long s_nVirtUs;
#endif

#define GK_KAPI_VERSION	92			// gpio_ctl's
// gpio_ctl's operations (kern/kapi_abi.h KAPI_GPIO_*: checked below on Onyx; a PC build has no kernel)
enum { OP_INFO, OP_MODE, OP_WRITE, OP_READ, OP_READ_ALL, OP_PWM, OP_EDGES, OP_EVENTS, OP_I2C_OPEN, OP_I2C_XFER,
       OP_I2C_SCAN, OP_SPI_OPEN, OP_SPI_XFER, OP_CLOSE, OP_RELEASE, OP_NOW };
enum { BUS_I2C = 1, BUS_SPI = 2 };
struct gk_i2c_xfer { unsigned addr, wlen, rlen; const void *wr; void *rd; };	// struct kapi_gpio_i2c
struct gk_spi_xfer { unsigned cs, len; const void *tx; void *rx; };		// struct kapi_gpio_spi
#if defined (__aarch64__) && !defined (GK_STANDALONE)
static_assert (OP_NOW == KAPI_GPIO_NOW && OP_RELEASE == KAPI_GPIO_RELEASE && OP_I2C_XFER == KAPI_GPIO_I2C_XFER
	       && OP_SPI_XFER == KAPI_GPIO_SPI_XFER && BUS_SPI == KAPI_GPIO_BUS_SPI, "gpio_ctl's numbers");
static_assert (sizeof (gk_i2c_xfer) == sizeof (struct kapi_gpio_i2c) && sizeof (gk_spi_xfer) == sizeof (struct kapi_gpio_spi), "transfers");
static_assert (sizeof (gk_pin_info) == sizeof (struct kapi_gpio_pin) && sizeof (gk_event) == sizeof (struct kapi_gpio_event), "layouts");
#endif

// ---- small helpers (freestanding: no C library) ---------------------------------------------------
static void zero (void *p, int n) { volatile unsigned char *b = (volatile unsigned char *) p; while (n-- > 0) *b++ = 0; }
static void copy (void *d, const void *s, int n) { unsigned char *a = (unsigned char *) d; const unsigned char *b = (const unsigned char *) s; while (n-- > 0) *a++ = *b++; }
static void scopy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

extern "C" unsigned long long gk_now_us (void);

// ---- the kernel's entry ----------------------------------------------------------------------------
static long hw (int op, long a0 = 0, long a1 = 0, long a2 = 0)
{
#if defined (__aarch64__) && !defined (GK_STANDALONE)
	const struct TKApiTable *t = (const struct TKApiTable *) KAPI_TABLE_VA;
	if (t->version < GK_KAPI_VERSION || t->gpio_ctl == 0) return GK_ENODEV;
	return t->gpio_ctl (op, a0, a1, a2);
#else
	(void) op; (void) a0; (void) a1; (void) a2;
	return GK_ENODEV;
#endif
}
static bool hw_present (void)
{
#if defined (__aarch64__) && !defined (GK_STANDALONE)
	const struct TKApiTable *t = (const struct TKApiTable *) KAPI_TABLE_VA;
	return t->version >= GK_KAPI_VERSION && t->gpio_ctl != 0;
#else
	return false;
#endif
}

static void nap_ms (int ms)
{
#ifdef GK_STANDALONE
	s_nVirtUs += (unsigned long long) ms * 1000u;
#else
	kapi_msleep ((unsigned) ms);
#endif
}

// ---- the simulated board ---------------------------------------------------------------------------
// A program's own: its pins, an outside level for each input, the PWM's waves computed from the time,
// an edge queue, an I2C bus with an SSD1306 (0x3C) and a BME280 (0x76), SPI looped back (MISO = MOSI).
static int s_nSim = -1;					// -1: not decided yet (the hardware if there is one)

struct SimPin { unsigned char mode, out, edges, last; signed char ext; unsigned freq, duty; };
static SimPin s_Pin[GK_PINS];
static gk_event s_Q[256];
static unsigned s_nQHead, s_nQTail;
static bool s_bQLost;
static bool s_bI2C, s_bSPI;

// the SSD1306: its memory, the addressing (horizontal), a command's pending arguments
static unsigned char s_Oled[1024];
static int s_nCol, s_nPage, s_nColLo, s_nColHi = 127, s_nPageLo, s_nPageHi = 7;
static int s_nOledCmd, s_nOledArgs, s_nOledArg0;
// the BME280: its registers, the one being read / written
static unsigned char s_Bme[256];
static int s_nBmeReg;

static void sim_reset (void);
static bool sim_on (void)
{
	if (s_nSim < 0) { s_nSim = hw_present () ? 0 : 1; if (s_nSim == 1) sim_reset (); }
	return s_nSim == 1;
}

static const char *const s_pReserved[GK_PINS] =
{
	"HAT ID EEPROM", "HAT ID EEPROM", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	"serial console", "serial console", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static int pwm_channel (int pin) { return pin == 12 || pin == 18 ? 0 : pin == 13 || pin == 19 ? 1 : -1; }

static int sim_level (int p)
{
	const SimPin &s = s_Pin[p];
	switch (s.mode)
	{
	case GK_OUT: return s.out;
	case GK_PWM:
	{
		if (s.duty == 0) return 0;
		if (s.duty >= 10000) return 1;
		unsigned long long per = 1000000ull / (s.freq ? s.freq : 1);
		if (per == 0) per = 1;
		return (gk_now_us () % per) < per * s.duty / 10000 ? 1 : 0;
	}
	case GK_I2C: case GK_SPI: return 1;			// (idle: pulled up / high)
	case GK_IN_PULLUP:   return s.ext >= 0 ? s.ext : 1;
	case GK_IN_PULLDOWN: return s.ext >= 0 ? s.ext : 0;
	default:
		if (s_pReserved[p] != 0) return 1;		// (the UART's idle line, the EEPROM's pull-ups)
		return s.ext >= 0 ? s.ext : 0;
	}
}

static void sim_edge_check (int p)
{
	SimPin &s = s_Pin[p];
	int lv = sim_level (p);
	if (lv == s.last) return;
	s.last = (unsigned char) lv;
	int edge = lv ? GK_RISING : GK_FALLING;
	if (!(s.edges & edge)) return;
	unsigned nNext = (s_nQHead + 1) & 255;
	if (nNext == s_nQTail) { s_bQLost = true; return; }
	gk_event &e = s_Q[s_nQHead];
	e.pin = (unsigned char) p; e.edge = (unsigned char) edge; e.level = (unsigned char) lv;
	e.lost = s_bQLost ? 1 : 0; e.reserved = 0; e.us = gk_now_us ();
	s_bQLost = false;
	s_nQHead = nNext;
}

static void sim_free (int p)
{
	SimPin &s = s_Pin[p];
	s.mode = GK_FREE; s.out = 0; s.edges = 0; s.freq = s.duty = 0;
	s.last = (unsigned char) sim_level (p);
}

// The board as at power-up: every pin free, nothing driving the inputs.
static void sim_reset (void)
{
	for (int p = 0; p < GK_PINS; p++) { s_Pin[p].ext = -1; sim_free (p); }
	s_bI2C = s_bSPI = false;
	s_nQHead = s_nQTail = 0;
}

static void bme_init (void)
{
	if (s_Bme[0xD0] == 0x60) return;
	s_Bme[0xD0] = 0x60;					// chip id
	// the datasheet's example calibration (BST-BME280-DS002, 8.1 / 4.2.3)
	static const unsigned short T[3] = { 27504, 26435, (unsigned short) -1000 };
	static const unsigned short P[9] = { 36477, (unsigned short) -10685, 3024, 2855, 140, (unsigned short) -7, 15500, (unsigned short) -14600, 6000 };
	for (int i = 0; i < 3; i++) { s_Bme[0x88 + 2 * i] = (unsigned char) T[i]; s_Bme[0x89 + 2 * i] = (unsigned char) (T[i] >> 8); }
	for (int i = 0; i < 9; i++) { s_Bme[0x8E + 2 * i] = (unsigned char) P[i]; s_Bme[0x8F + 2 * i] = (unsigned char) (P[i] >> 8); }
	s_Bme[0xA1] = 75;					// H1
	s_Bme[0xE1] = 362 & 0xFF; s_Bme[0xE2] = 362 >> 8;	// H2
	s_Bme[0xE3] = 0;					// H3
	s_Bme[0xE4] = 313 >> 4; s_Bme[0xE5] = (313 & 0xF) | ((50 & 0xF) << 4); s_Bme[0xE6] = 50 >> 4;	// H4, H5
	s_Bme[0xE7] = 30;					// H6
}
static void bme_sample (void)
{
	// about 25 degrees (519888), 1006.5 hPa (415148), a humidity near 45 %: the temperature drifts
	// a little with the time (the readings change)
	unsigned long long s = gk_now_us () / 1000000ull;
	unsigned t = 519888u + (unsigned) ((s % 40) < 20 ? (s % 20) * 80 : (20 - s % 20) * 80);
	unsigned p = 415148u, h = 27500u;
	s_Bme[0xF7] = (unsigned char) (p >> 12); s_Bme[0xF8] = (unsigned char) (p >> 4); s_Bme[0xF9] = (unsigned char) ((p & 15) << 4);
	s_Bme[0xFA] = (unsigned char) (t >> 12); s_Bme[0xFB] = (unsigned char) (t >> 4); s_Bme[0xFC] = (unsigned char) ((t & 15) << 4);
	s_Bme[0xFD] = (unsigned char) (h >> 8); s_Bme[0xFE] = (unsigned char) h;
}

static void oled_data (unsigned char b)
{
	if (s_nPage >= 0 && s_nPage < 8 && s_nCol >= 0 && s_nCol < 128) s_Oled[s_nPage * 128 + s_nCol] = b;
	if (++s_nCol > s_nColHi) { s_nCol = s_nColLo; if (++s_nPage > s_nPageHi) s_nPage = s_nPageLo; }
}
static void oled_cmd (unsigned char c)
{
	if (s_nOledArgs > 0)					// an argument of the command before
	{
		if (s_nOledCmd == 0x21) { if (s_nOledArgs == 2) s_nOledArg0 = c; else { s_nColLo = s_nOledArg0 & 127; s_nColHi = c & 127; s_nCol = s_nColLo; } }
		if (s_nOledCmd == 0x22) { if (s_nOledArgs == 2) s_nOledArg0 = c; else { s_nPageLo = s_nOledArg0 & 7; s_nPageHi = c & 7; s_nPage = s_nPageLo; } }
		s_nOledArgs--;
		return;
	}
	s_nOledCmd = c;
	if (c == 0x21 || c == 0x22) s_nOledArgs = 2;
	else if (c == 0x20 || c == 0x81 || c == 0x8D || c == 0xA8 || c == 0xD3 || c == 0xD5 || c == 0xD9 || c == 0xDA || c == 0xDB) s_nOledArgs = 1;
	else if (c >= 0xB0 && c <= 0xB7) s_nPage = c - 0xB0;
	else if (c <= 0x0F) s_nCol = (s_nCol & 0xF0) | c;
	else if (c >= 0x10 && c <= 0x1F) s_nCol = (s_nCol & 0x0F) | ((c & 15) << 4);
}

static int sim_i2c (int addr, const unsigned char *wr, int wn, unsigned char *rd, int rn)
{
	if (addr == 0x3C)
	{
		if (rn > 0) { zero (rd, rn); return wn > 0 && rn == 0 ? wn : rn; }	// (its status: 0)
		if (wn < 1) return 0;
		bool data = (wr[0] & 0x40) != 0;			// the control byte: Co / D/C#
		for (int i = 1; i < wn; i++) { if (data) oled_data (wr[i]); else oled_cmd (wr[i]); }
		return wn;
	}
	if (addr == 0x76)
	{
		bme_init ();
		if (wn >= 1)
		{
			s_nBmeReg = wr[0];
			for (int i = 1; i < wn; i++) s_Bme[(s_nBmeReg + i - 1) & 255] = wr[i];	// (ctrl_meas, config...)
		}
		if (rn > 0)
		{
			bme_sample ();
			for (int i = 0; i < rn; i++) rd[i] = s_Bme[(s_nBmeReg + i) & 255];
			return rn;
		}
		return wn;
	}
	return GK_EIO;
}

// ---- the kit ---------------------------------------------------------------------------------------

extern "C" {

unsigned long long gk_now_us (void)
{
	if (!sim_on ())
	{
		long r = hw (OP_NOW);
		if (r >= 0) return (unsigned long long) r;
	}
#if defined (__aarch64__) && !defined (GK_STANDALONE)
	unsigned long c, f;
	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0\n\tmrs %1, cntfrq_el0" : "=r" (c), "=r" (f));
	return (unsigned long long) ((__uint128_t) c * 1000000u / f);
#elif defined (GK_STANDALONE)
	return s_nVirtUs += 100;
#else
	return (unsigned long long) kapi_get_ticks () * 10000ull;	// (the stand-in's 100 Hz ticks)
#endif
}

int gk_available (void)
{
	return sim_on () ? 2 : hw_present () ? 1 : 0;
}

int gk_sim (int on)
{
	if (on) { if (s_nSim != 1) { s_nSim = 1; sim_reset (); } }
	else s_nSim = hw_present () ? 0 : 1;
	return gk_available ();
}

const char *gk_error (int code)
{
	switch (code)
	{
	case 0:		return "done";
	case GK_EPERM:	return "the system uses this pin, or it is not this program's";
	case GK_EIO:	return "no answer on the bus";
	case GK_ENOMEM:	return "out of memory";
	case GK_EFAULT:	return "a bad buffer";
	case GK_EBUSY:	return "another program has it";
	case GK_ENODEV:	return "no GPIO on this system (update it, or use the simulator)";
	case GK_EINVAL:	return "not possible on this pin, or a value out of range";
	default:	return code < 0 ? "failed" : "done";
	}
}

int gk_release (void)
{
	if (!sim_on ()) return (int) hw (OP_RELEASE);
	for (int p = 0; p < GK_PINS; p++) sim_free (p);
	s_bI2C = s_bSPI = false;
	s_nQHead = s_nQTail = 0;
	return 0;
}

// ---- pins ----

int gk_mode (int pin, int mode)
{
	if (!sim_on ()) return (int) hw (OP_MODE, pin, mode);
	if (pin < 0 || pin >= GK_PINS || mode < 0 || mode > GK_ALT0 + 5 || mode == GK_PWM || mode == GK_I2C || mode == GK_SPI) return GK_EINVAL;
	if (s_pReserved[pin] != 0) return GK_EPERM;
	if (s_Pin[pin].mode == GK_I2C || s_Pin[pin].mode == GK_SPI) return GK_EBUSY;
	sim_free (pin);
	s_Pin[pin].mode = (unsigned char) mode;
	s_Pin[pin].last = (unsigned char) sim_level (pin);
	return 0;
}

int gk_write (int pin, int level)
{
	if (!sim_on ()) return (int) hw (OP_WRITE, pin, level ? 1 : 0);
	if (pin < 0 || pin >= GK_PINS) return GK_EINVAL;
	if (s_Pin[pin].mode == GK_FREE || s_pReserved[pin] != 0) return GK_EPERM;
	if (s_Pin[pin].mode != GK_OUT) return GK_EINVAL;
	s_Pin[pin].out = level ? 1 : 0;
	return 0;
}

int gk_read (int pin)
{
	if (!sim_on ()) return (int) hw (OP_READ, pin);
	if (pin < 0 || pin >= GK_PINS) return GK_EINVAL;
	return sim_level (pin);
}

int gk_toggle (int pin)
{
	int v = gk_read (pin);
	if (v < 0) return v;
	int r = gk_write (pin, !v);
	return r < 0 ? r : !v;
}

unsigned gk_read_all (void)
{
	if (!sim_on ()) { long r = hw (OP_READ_ALL); return r < 0 ? 0 : (unsigned) r; }
	unsigned m = 0;
	for (int p = 0; p < GK_PINS; p++) if (sim_level (p)) m |= 1u << p;
	return m;
}

int gk_info (gk_pin_info *out, int max)
{
	if (out == 0 || max <= 0) return GK_PINS;
	if (!sim_on ()) return (int) hw (OP_INFO, (long) out, max);
	if (max > GK_PINS) max = GK_PINS;
	for (int p = 0; p < max; p++)
	{
		gk_pin_info &i = out[p];
		zero (&i, sizeof i);
		i.pin = (unsigned char) p; i.mode = s_Pin[p].mode; i.level = (unsigned char) sim_level (p);
		i.owner = s_Pin[p].mode != GK_FREE ? 1 : 0;
		if (s_pReserved[p] != 0) { i.flags |= GK_F_RESERVED; scopy (i.reason, s_pReserved[p], sizeof i.reason); }
		if (pwm_channel (p) >= 0) i.flags |= GK_F_PWM;
		if (s_Pin[p].edges) i.flags |= GK_F_EDGES;
		if (s_Pin[p].mode == GK_PWM) { i.pwm_freq = s_Pin[p].freq; i.pwm_duty = s_Pin[p].duty; }
	}
	return max;
}

const char *gk_mode_name (int mode)
{
	static const char *const n[] = { "free", "input", "pull-up", "pull-down", "output", "PWM", "I2C", "SPI",
					 "ALT0", "ALT1", "ALT2", "ALT3", "ALT4", "ALT5" };
	return mode >= 0 && mode <= GK_ALT0 + 5 ? n[mode] : "?";
}

// ---- PWM ----

int gk_pwm (int pin, int freq_hz, int duty)
{
	if (!sim_on ()) return (int) hw (OP_PWM, pin, freq_hz, duty);
	int c = pwm_channel (pin);
	if (c < 0 || freq_hz < 1 || freq_hz > 1000000 || duty < 0 || duty > 10000) return GK_EINVAL;
	int other = c == 0 ? (pin == 12 ? 18 : 12) : (pin == 13 ? 19 : 13);
	if (s_Pin[other].mode == GK_PWM) return GK_EBUSY;
	if (s_Pin[pin].mode != GK_PWM) sim_free (pin);
	s_Pin[pin].mode = GK_PWM; s_Pin[pin].freq = (unsigned) freq_hz; s_Pin[pin].duty = (unsigned) duty;
	return 0;
}

int gk_servo (int pin, int pulse_us)
{
	if (pulse_us < 500) pulse_us = 500;
	if (pulse_us > 2500) pulse_us = 2500;
	return gk_pwm (pin, 50, pulse_us / 2);			// (20 ms a period: pulse / 20000 * 10000)
}

// ---- edges ----

int gk_edges (int pin, int edges)
{
	if (!sim_on ()) return (int) hw (OP_EDGES, pin, edges);
	if (pin < 0 || pin >= GK_PINS || edges < 0 || edges > 3) return GK_EINVAL;
	int m = s_Pin[pin].mode;
	if (m != GK_IN && m != GK_IN_PULLUP && m != GK_IN_PULLDOWN) return m == GK_FREE ? GK_EPERM : GK_EINVAL;
	s_Pin[pin].edges = (unsigned char) edges;
	s_Pin[pin].last = (unsigned char) sim_level (pin);
	return 0;
}

int gk_events (gk_event *out, int max, int wait_ms)
{
	if (out == 0 || max <= 0) return GK_EINVAL;
	if (wait_ms < 0) wait_ms = 0;
	if (wait_ms > 1000) wait_ms = 1000;
	if (!sim_on ()) return (int) hw (OP_EVENTS, (long) out, max, wait_ms);
	for (int waited = 0; s_nQHead == s_nQTail && waited < wait_ms; waited += 5) nap_ms (5);
	int n = 0;
	while (n < max && s_nQTail != s_nQHead) { out[n++] = s_Q[s_nQTail]; s_nQTail = (s_nQTail + 1) & 255; }
	return n;
}

// ---- I2C ----

int gk_i2c_open (int clock_hz)
{
	if (!sim_on ()) return (int) hw (OP_I2C_OPEN, clock_hz);
	if (s_bI2C) return 0;
	if (s_Pin[2].mode != GK_FREE || s_Pin[3].mode != GK_FREE) return GK_EBUSY;
	s_Pin[2].mode = s_Pin[3].mode = GK_I2C;
	s_bI2C = true;
	return 0;
}

int gk_i2c_close (void)
{
	if (!sim_on ()) return (int) hw (OP_CLOSE, BUS_I2C);
	if (s_bI2C) { sim_free (2); sim_free (3); s_bI2C = false; }
	return 0;
}

int gk_i2c_write_read (int addr, const void *wr, int wn, void *rd, int rn)
{
	if (addr < 0 || addr > 0x7F || wn < 0 || rn < 0 || wn > 4096 || rn > 4096 || (wn == 0 && rn == 0)) return GK_EINVAL;
	if (!sim_on ())
	{
		gk_i2c_xfer x = { (unsigned) addr, (unsigned) wn, (unsigned) rn, wr, rd };
		return (int) hw (OP_I2C_XFER, (long) &x);
	}
	if (!s_bI2C) return GK_EPERM;
	return sim_i2c (addr, (const unsigned char *) wr, wn, (unsigned char *) rd, rn);
}
int gk_i2c_write (int addr, const void *data, int n) { return gk_i2c_write_read (addr, data, n, 0, 0); }
int gk_i2c_read (int addr, void *data, int n) { return gk_i2c_write_read (addr, 0, 0, data, n); }

int gk_i2c_reg_read (int addr, int reg)
{
	unsigned char r = (unsigned char) reg, v = 0;
	int n = gk_i2c_write_read (addr, &r, 1, &v, 1);
	return n < 0 ? n : v;
}

int gk_i2c_reg_write (int addr, int reg, int value)
{
	unsigned char b[2] = { (unsigned char) reg, (unsigned char) value };
	int n = gk_i2c_write (addr, b, 2);
	return n < 0 ? n : 0;
}

int gk_i2c_scan (unsigned char map[16])
{
	if (map == 0) return GK_EINVAL;
	if (!sim_on ()) return (int) hw (OP_I2C_SCAN, (long) map);
	if (!s_bI2C) return GK_EPERM;
	zero (map, 16);
	map[0x3C / 8] |= (unsigned char) (1 << (0x3C % 8));
	map[0x76 / 8] |= (unsigned char) (1 << (0x76 % 8));
	return 2;
}

const char *gk_i2c_guess (int addr)
{
	switch (addr)
	{
	case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: return "MCP23017 / PCF8574 port expander";
	case 0x3C: case 0x3D: return "SSD1306 / SH1106 OLED display";
	case 0x40: return "INA219 / HTU21D / PCA9685";
	case 0x48: case 0x49: case 0x4A: case 0x4B: return "ADS1115 ADC / TMP102";
	case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: return "AT24C EEPROM";
	case 0x5C: return "AM2320 / BH1750";
	case 0x68: return "DS3231 / DS1307 clock, MPU-6050";
	case 0x69: return "MPU-6050 (AD0 high)";
	case 0x76: case 0x77: return "BME280 / BMP280 sensor";
	default: return "";
	}
}

// ---- SPI ----

int gk_spi_open (int clock_hz, int mode)
{
	if (!sim_on ()) return (int) hw (OP_SPI_OPEN, clock_hz, mode);
	if (mode < 0 || mode > 3) return GK_EINVAL;
	if (s_bSPI) return 0;
	for (int p = 7; p <= 11; p++) if (s_Pin[p].mode != GK_FREE) return GK_EBUSY;
	for (int p = 7; p <= 11; p++) s_Pin[p].mode = GK_SPI;
	s_bSPI = true;
	return 0;
}

int gk_spi_transfer (int cs, const void *tx, void *rx, int n)
{
	if (cs < 0 || cs > 1 || n <= 0 || n > 4096) return GK_EINVAL;
	if (!sim_on ())
	{
		gk_spi_xfer x = { (unsigned) cs, (unsigned) n, tx, rx };
		return (int) hw (OP_SPI_XFER, (long) &x);
	}
	if (!s_bSPI) return GK_EPERM;
	if (rx != 0) { if (tx != 0) copy (rx, tx, n); else zero (rx, n); }	// (MISO wired to MOSI)
	return n;
}

int gk_spi_close (void)
{
	if (!sim_on ()) return (int) hw (OP_CLOSE, BUS_SPI);
	if (s_bSPI) { for (int p = 7; p <= 11; p++) sim_free (p); s_bSPI = false; }
	return 0;
}

// ---- the header ----

static const signed char s_Header[41] =			// header pin -> GPIO (-1 power, -2 ground)
{
	0,
	-1, -1,  2, -1,  3, -2,  4, 14, -2, 15,
	17, 18, 27, -2, 22, 23, -1, 24, 10, -2,
	 9, 25, 11,  8, -2,  7,  0,  1,  5, -2,
	 6, 12, 13, -2, 19, 16, 26, 20, -2, 21,
};

int gk_header_gpio (int header_pin)
{
	if (header_pin < 1 || header_pin > 40) return -1;
	return s_Header[header_pin] >= 0 ? s_Header[header_pin] : -1;
}

int gk_header_pin (int gpio)
{
	for (int i = 1; i <= 40; i++) if (s_Header[i] == gpio) return i;
	return -1;
}

const char *gk_header_label (int header_pin)
{
	static const char *const gpio[GK_PINS] = { "GPIO0", "GPIO1", "GPIO2", "GPIO3", "GPIO4", "GPIO5", "GPIO6", "GPIO7",
		"GPIO8", "GPIO9", "GPIO10", "GPIO11", "GPIO12", "GPIO13", "GPIO14", "GPIO15", "GPIO16", "GPIO17", "GPIO18",
		"GPIO19", "GPIO20", "GPIO21", "GPIO22", "GPIO23", "GPIO24", "GPIO25", "GPIO26", "GPIO27" };
	if (header_pin < 1 || header_pin > 40) return "";
	int g = s_Header[header_pin];
	if (g >= 0) return gpio[g];
	if (g == -2) return "GND";
	return header_pin == 1 || header_pin == 17 ? "3V3" : "5V";
}

const char *gk_gpio_function (int gpio)
{
	switch (gpio)
	{
	case 0: return "ID_SD";		case 1: return "ID_SC";
	case 2: return "SDA1";		case 3: return "SCL1";
	case 4: return "GPCLK0";
	case 7: return "SPI0 CE1";	case 8: return "SPI0 CE0";
	case 9: return "SPI0 MISO";	case 10: return "SPI0 MOSI";	case 11: return "SPI0 SCLK";
	case 12: return "PWM0";		case 13: return "PWM1";
	case 14: return "TXD0";		case 15: return "RXD0";
	case 18: return "PWM0";		case 19: return "PWM1";
	default: return "";
	}
}

// ---- the simulator ----

int gk_sim_input (int pin, int level)
{
	if (pin < 0 || pin >= GK_PINS) return GK_EINVAL;
	s_Pin[pin].ext = (signed char) (level < 0 ? -1 : level ? 1 : 0);
	if (sim_on ()) sim_edge_check (pin);
	return 0;
}

int gk_sim_display (unsigned char out[1024])
{
	if (!sim_on () || out == 0) return 0;
	copy (out, s_Oled, 1024);
	return 1;
}

} // extern "C"

// (the library's table: its init -- the library runtime's, Runtime/librt.cpp)
#if defined (__aarch64__) && !defined (GK_STANDALONE)
extern "C" int onyx_lib_init (const TLibImports *imp);
extern "C" int gk_lib_init (const TLibImports *imp)
{
	return onyx_lib_init (imp) < 0 ? -1 : 0;
}
#endif
