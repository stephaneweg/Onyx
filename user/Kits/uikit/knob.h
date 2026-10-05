//
// uikit/knob.h -- Knob: a rotary control (a studio's gain, pan, rate...). A 270-degree track (open at
// the bottom), the value's arc on it in the accent (or `arcColor`) -- from the start, or from the
// value 0 for a bipolar one (a pan: the top) --, a round cap with a pointer; an optional label and
// the value's text under it. Integer values in [vmin, vmax] (the apps are integer-only: a gain of
// -60.0 .. +6.0 dB is -600 .. 60, shown by a `format` callback).
//
//   Knob *k = new Knob (x, y, 48, 64, -600, 60, 0, onGain);
//   k->setLabel ("Gain"); k->format = fmt_db; k->setDefault (0);
//
// Mouse: drag up / down (the whole range in 200 px; Shift held: 1000 px, fine), the wheel (a step),
// a double click: back to the default (setDefault). Keys when focused: Up / Right, Down / Left (a
// step), Page Up / Down (ten), Home / End (the ends), Delete (the default). onChange fires at each
// change of `value`. Sizes: the dial is the widget's width, less the captions' lines under it
// (about 24 .. 64 px across).
//
#ifndef _uikit_knob_h
#define _uikit_knob_h

#include "uikit/widget.h"

namespace uikit {

class Knob : public Widget
{
public:
	int	 value, vmin, vmax;
	int	 step;				// the wheel's / the keys' step (default: a 100th of the range)
	int	 def; bool hasDef;		// the default (a double click, Delete); setDefault
	bool	 bipolar;			// the arc from 0 (a pan, a detune: the top), not from the start
	bool	 showValue;			// the value's text under the dial (and the label)
	unsigned arcColor;			// the value's arc (UK_AUTO: the accent)
	TextFace *face;				// the captions' face (0: uikit's -- the installed face or the bitmap font)
	void	 (*format) (int value, char *out, int cap);	// the value's text (0: the number)
	Action	 onChange;

	Knob (int l, int t, int w, int h, int lo, int hi, int val, Action cb = 0);
	void setValue (int v, bool fire = false);	// clamped; repainted; onChange if fire and it changed
	void setRange (int lo, int hi);
	void setDefault (int v) { def = v; hasDef = true; }
	void setLabel (const char *s);
	const char *label () const { return m_label; }
	void valueText (char *out, int cap) const;	// what is shown under the dial

	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
private:
	char	 m_label[24];
	bool	 m_drag, m_fine;
	int	 m_y0, m_v0;			// the drag's start: its y, the value then
	unsigned m_lastClick;			// ticks (a double click)
	int	 lines () const { return (m_label[0] ? 1 : 0) + (showValue ? 1 : 0); }
};

} // namespace uikit

#endif
