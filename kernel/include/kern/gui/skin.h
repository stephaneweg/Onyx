//
// skin.h
//
// 9-slice bitmap skin, ported from SimpleOS (gui/skin.bas). The source BMP holds
// `count` states stacked vertically (e.g. a button: normal / hover / pressed).
// The margins (left/right/top/bottom) mark fixed corners + edges; the middle is
// tiled to fill an arbitrary widget size. Pixels equal to GIMAGE_TRANSPARENT
// (magenta) are treated as transparent.
//
#ifndef _kern_gui_skin_h
#define _kern_gui_skin_h

#include <kern/gui/gimage.h>
#include <circle/types.h>

class CSkin
{
public:
	CSkin (void);

	// Decode a BMP (nCount states stacked vertically) + set the 9-slice margins.
	boolean LoadFromBuffer (const u8 *pData, unsigned nSize, unsigned nCount,
				int nLeft, int nRight, int nTop, int nBottom);

	boolean IsValid (void) const	{ return m_Image.IsValid (); }
	int StateWidth (void) const	{ return m_nSkinW; }
	int StateHeight (void) const	{ return m_nSkinH; }

	// Draw state nNum at (x,y) sized w x h onto pTarget (9-slice).
	void DrawOn (GImage *pTarget, unsigned nNum, int x, int y, int w, int h,
		     boolean bTransparent);

private:
	GImage	 m_Image;
	int	 m_nSkinW;		// one state's width  (= image width)
	int	 m_nSkinH;		// one state's height (= image height / count)
	unsigned m_nCount;
	int	 m_nLeft, m_nRight, m_nTop, m_nBottom;
};

// Skins loaded at boot from SD:/skins (0 if unavailable -> flat fallback drawing).
extern CSkin *g_pButtonSkin;
extern CSkin *g_pCloseSkin;
extern CSkin *g_pWindowSkin;

#endif // _kern_gui_skin_h
