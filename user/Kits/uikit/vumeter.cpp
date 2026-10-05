//
// uikit/vumeter.cpp -- VuMeter (uikit/vumeter.h): levels in 1/100 dB, the segments laid along the scale
// (linear in dB), the falls timed by the ticks between two calls.
//
#include "uikit/vumeter.h"

namespace uikit {

// log2 (1 + i / 16) x 1000 (i = 0..16): the fraction of a level's logarithm.
static const short L2[17] = { 0, 87, 170, 248, 322, 392, 459, 524, 585, 644, 700, 755, 807, 858, 907, 954, 1000 };

int uk_q16_to_cdb (unsigned v)
{
	if (v == 0) return -20000;
	int n = 31; while (!(v >> n)) n--;					// the top bit
	unsigned m = n >= 8 ? (v >> (n - 8)) & 0xFF : (v << (8 - n)) & 0xFF;	// the next 8 bits
	int i = (int) (m >> 4), f = (int) (m & 15);
	int l2 = (n - 16) * 1000 + L2[i] + (L2[i + 1] - L2[i]) * f / 16;	// log2 (v / 65536) x 1000
	int cdb = l2 * 60206 / 100000;						// x 6.0206 dB
	return cdb < -20000 ? -20000 : cdb;
}

static const unsigned VU_GREEN = 0x0040C060, VU_AMBER = 0x00E6AC2E, VU_RED = 0x00E8493C;
enum { VU_PAD = 1, VU_CLIP = 4 };

VuMeter::VuMeter (int l, int t, int w, int h, bool vertical_, bool stereo_)
  : Widget (l, t, w, h), vertical (vertical_), stereo (stereo_), floorDb (-48), topDb (6), amberDb (-12), redDb (-3),
    segPx (3), holdTicks (150), fallDb (26), peakFallDb (14), showPeak (true), showClip (true), m_last (0)
{
	clip[0] = clip[1] = false;
	for (int i = 0; i < 5; i++) m_drawn[i] = -2;
	reset ();
}

void VuMeter::reset ()
{
	for (int c = 0; c < 2; c++) { m_lvl[c] = m_pk[c] = -20000; m_pkT[c] = 0; clip[c] = false; }
	invalidate (true);
}

int VuMeter::len () const
{
	int L = (vertical ? height : width) - 2 * VU_PAD - (showClip ? VU_CLIP + 1 : 0);
	return L < 1 ? 1 : L;
}

int VuMeter::pos (int cdb) const
{
	int lo = floorDb * 100, hi = topDb * 100, L = len ();
	if (hi <= lo) hi = lo + 100;
	if (cdb <= lo) return 0;
	if (cdb >= hi) return L;
	return (int) ((long long) (cdb - lo) * L / (hi - lo));
}

unsigned VuMeter::colourAt (int cdb) const
{ return cdb >= redDb * 100 ? VU_RED : cdb >= amberDb * 100 ? VU_AMBER : VU_GREEN; }

// What the canvas must show: each channel's lit length and its peak's place (px, by segment), the clips.
void VuMeter::state (int *s) const
{
	for (int c = 0; c < 2; c++)
	{
		int p = pos (m_lvl[c]), q = pos (m_pk[c]);
		if (segPx > 1) { p = (p + segPx / 2) / segPx * segPx; q = q / segPx * segPx; }
		s[c] = p;
		s[2 + c] = showPeak && m_pk[c] > floorDb * 100 ? q : -1;
	}
	s[4] = (clip[0] ? 1 : 0) | (clip[1] ? 2 : 0);
}

void VuMeter::setCdb (int l, int r)
{
	unsigned now = kapi_get_ticks ();
	int dt = m_last ? (int) (now - m_last) : 0;
	if (dt < 0) dt = 0;
	if (dt > 1000) dt = 1000;
	m_last = now;
	int in[2] = { l, stereo ? r : l };
	for (int c = 0; c < 2; c++)
	{
		int v = in[c] < -20000 ? -20000 : in[c];
		int fallen = m_lvl[c] - fallDb * dt;			// (dB/s x ticks of 1/100 s = 1/100 dB)
		m_lvl[c] = v > fallen ? v : fallen;
		if (m_lvl[c] < -20000) m_lvl[c] = -20000;
		if (v >= m_pk[c]) { m_pk[c] = v; m_pkT[c] = now; }
		else if ((int) (now - m_pkT[c]) > holdTicks)
		{
			m_pk[c] -= peakFallDb * dt;
			if (m_pk[c] < m_lvl[c]) m_pk[c] = m_lvl[c];
		}
		if (v > 0) clip[c] = true;
	}
	int s[5]; state (s);
	for (int i = 0; i < 5; i++) if (s[i] != m_drawn[i]) { invalidate (true); break; }
}

void VuMeter::setQ16 (int l, int r)
{
	setCdb (uk_q16_to_cdb ((unsigned) (l < 0 ? -l : l)), uk_q16_to_cdb ((unsigned) (r < 0 ? -r : r)));
}

void VuMeter::onDraw ()
{
	unsigned bg = bgColor ();
	bool dark = uk_bright (C_BG) < 110;
	unsigned well = uk_tone (C_BG, dark ? 64 : 50);
	canvas.clear (bg);
	uk_rbox (canvas, 0, 0, width, height, 2, well, well);
	int s[5]; state (s);
	for (int i = 0; i < 5; i++) m_drawn[i] = s[i];
	int L = len (), nch = stereo ? 2 : 1, across = (vertical ? width : height) - 2 * VU_PAD;
	int cw = (across - (nch - 1)) / nch; if (cw < 1) cw = 1;
	int lo = floorDb * 100, span = (topDb - floorDb) * 100; if (span < 100) span = 100;
	int off = showClip ? VU_CLIP + 1 : 0;				// (the clip light at the top / the right end)
	// a run along the scale [o, o + n) of channel c, in colour k
	auto run = [&] (int c, int o, int n, unsigned k) {
		if (n <= 0) return;
		int a = VU_PAD + c * (cw + 1);
		if (vertical) canvas.fillRect (a, VU_PAD + off + L - o - n, cw, n, k);
		else canvas.fillRect (VU_PAD + o, a, n, cw, k);
	};
	for (int c = 0; c < nch; c++)
	{
		int lit = s[c], pk = s[2 + c];
		if (segPx > 1)
			for (int o = 0; o + segPx - 1 <= L; o += segPx)
			{
				unsigned k = colourAt (lo + (int) ((long long) (o + segPx / 2) * span / L));
				bool on = o + segPx <= lit || o == pk;
				run (c, o, segPx - 1, on ? k : uk_mix (well, k, 46));
			}
		else
		{
			int pa = pos (amberDb * 100), pr = pos (redDb * 100);
			run (c, 0, pa, uk_mix (well, VU_GREEN, 46)); run (c, pa, pr - pa, uk_mix (well, VU_AMBER, 46));
			run (c, pr, L - pr, uk_mix (well, VU_RED, 46));
			run (c, 0, lit < pa ? lit : pa, VU_GREEN);
			if (lit > pa) run (c, pa, (lit < pr ? lit : pr) - pa, VU_AMBER);
			if (lit > pr) run (c, pr, lit - pr, VU_RED);
			if (pk >= 0) run (c, pk > L - 2 ? L - 2 : pk, 2, colourAt (m_pk[c]));
		}
		if (showClip)
		{
			int a = VU_PAD + c * (cw + 1);
			unsigned k = clip[c] ? VU_RED : uk_mix (well, VU_RED, 40);
			if (vertical) canvas.fillRect (a, VU_PAD, cw, VU_CLIP, k);
			else canvas.fillRect (width - VU_PAD - VU_CLIP, a, VU_CLIP, cw, k);
		}
	}
}

bool VuMeter::onMouse (int mx, int my, int bl, int, int, int)
{
	if (mx < 0 || my < 0 || mx >= width || my >= height) { pressed = false; return false; }
	if (bl && !pressed) { pressed = true; if (clip[0] || clip[1]) { clip[0] = clip[1] = false; invalidate (true); } }
	else if (!bl) pressed = false;
	return true;
}

} // namespace uikit
