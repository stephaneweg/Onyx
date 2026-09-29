//
// wtk/lcd.h -- LcdDisplay: a label drawn as a studio's time / position display: a sunken well (dark
// in a dark theme, the accent's pale tint in a light one), large digits in the accent (a face given
// to it, else the bitmap font scaled up), and at its right a small caption over a second line
// ("BAR.BEAT.16" over "0:14.83").
//
//   LcdDisplay *pos = new LcdDisplay (x, y, 190, 40, "6.3.2", "BAR.BEAT.16");
//   pos->setSub ("0:14.83");  ...  pos->setText ("6.3.3");    (repainted only when it changed)
//   pos->face = bigFace;      (e.g. an FtTextFace at 24 px: ft/wtkface.h)
//
#ifndef _wtk_lcd_h
#define _wtk_lcd_h

#include "wtk/widget.h"

namespace wtk {

class LcdDisplay : public Widget
{
public:
	TextFace *face;				// the digits' face (0: the bitmap font, scaled)
	TextFace *smallFace;			// the caption's and the second line's (0: wtk's)
	int	 scale;				// the bitmap digits' scale (0: from the height, as large as fits)
	unsigned ink;				// the digits' colour (WK_AUTO: the accent, for the well)
	bool	 centred;			// the digits centred in the room left (else from the left)

	LcdDisplay (int l, int t, int w, int h, const char *text = "", const char *caption = 0);
	void setText (const char *s);
	void setCaption (const char *s);
	void setSub (const char *s);
	const char *text () const { return m_text; }

	void onDraw () override;
private:
	char	 m_text[32], m_cap[32], m_sub[32];
};

} // namespace wtk

#endif
