//
// uikit/vumeter.h -- VuMeter: a level meter (stereo or mono, vertical or horizontal) as a studio's:
// segments on a dark well, green, then amber, then red toward the top of the scale; a peak held a
// moment then falling; a clip light (a level over 0 dBFS; a click clears it). Levels are linear
// (1.0 = 0 dBFS, may go over), mapped to dB inside; the bars fall back at `fallDb` dB a second.
//
//   VuMeter *m = new VuMeter (x, y, 14, 120);            (vertical, stereo)
//   ... at the UI's rate (Root::onTick):  m->setQ16 (peakL, peakR);   (65536 = 1.0)
//   ... or, in an FP-enabled translation unit:  m->set (0.5f, 0.25f);
//
// Call it regularly, silence included (set (0, 0)): the falls and the peaks' hold follow the clock
// (kapi_get_ticks) at each call. Cheap: it repaints only when a lit segment or a peak moves.
//
#ifndef _uikit_vumeter_h
#define _uikit_vumeter_h

#include "uikit/widget.h"

namespace uikit {

// A linear level (65536 = 1.0 = 0 dBFS) -> 1/100 dB (0 -> -20000).
int uk_q16_to_cdb (unsigned q16);

class VuMeter : public Widget
{
public:
	bool	 vertical, stereo;
	int	 floorDb, topDb;		// the scale, dB (-48 .. +6)
	int	 amberDb, redDb;		// where the segments turn amber, then red (-12, -3)
	int	 segPx;				// a segment's length with its 1-px gap (3; 0: a continuous bar)
	int	 holdTicks;			// the peak's hold (150 = 1.5 s at 100 Hz)
	int	 fallDb, peakFallDb;		// the bar's, the peak's fall after its hold, dB a second (26, 14)
	bool	 showPeak, showClip;
	bool	 clip[2];			// a level went over 0 dBFS (a click on the meter clears them)

	VuMeter (int l, int t, int w, int h, bool vertical = true, bool stereo = true);
	void setQ16 (int l, int r);		// linear levels, 65536 = 0 dBFS (mono: l)
	void setCdb (int l, int r);		// levels in 1/100 dB
	void reset ();				// all down, the peaks and clips cleared
	int  levelCdb (int ch) const { return m_lvl[ch & 1]; }	// shown, 1/100 dB
	int  peakCdb (int ch) const { return m_pk[ch & 1]; }
#if defined (__ARM_FP) || !defined (__aarch64__)	// (an FP-enabled unit: uikit itself is integer-only)
	void set (float l, float r)
	{
		if (l < 0) l = -l;
		if (r < 0) r = -r;
		setQ16 (l > 16.f ? 1 << 20 : (int) (l * 65536.f), r > 16.f ? 1 << 20 : (int) (r * 65536.f));
	}
#endif

	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
private:
	int	 m_lvl[2], m_pk[2];		// shown level, peak (1/100 dB)
	unsigned m_pkT[2], m_last;		// when each peak was set; the last call (ticks)
	int	 m_drawn[5];			// what the canvas shows (lit lengths, peak places, clips)
	int	 len () const;			// the scale's length, px
	int	 pos (int cdb) const;		// a level's place along it, px
	unsigned colourAt (int cdb) const;
	void	 state (int *s) const;
};

} // namespace uikit

#endif
