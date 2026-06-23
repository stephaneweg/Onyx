//
// skin.cpp -- 9-slice bitmap skin (ported from SimpleOS gui/skin.bas).
//
#include <kern/gui/skin.h>

CSkin *g_pButtonSkin = 0;
CSkin *g_pCloseSkin  = 0;
CSkin *g_pWindowSkin = 0;
CSkin *g_pWindowSkinInactive = 0;

CSkin::CSkin (void)
:	m_nSkinW (0), m_nSkinH (0), m_nCount (1),
	m_nLeft (0), m_nRight (0), m_nTop (0), m_nBottom (0)
{
}

boolean CSkin::LoadFromBuffer (const u8 *pData, unsigned nSize, unsigned nCount,
			       int nLeft, int nRight, int nTop, int nBottom)
{
	if (nCount == 0 || !m_Image.LoadBMP (pData, nSize))
	{
		return FALSE;
	}

	m_nCount  = nCount;
	m_nSkinW  = m_Image.Width ();
	m_nSkinH  = m_Image.Height () / (int) nCount;	// one state
	m_nLeft   = nLeft;
	m_nRight  = nRight;
	m_nTop    = nTop;
	m_nBottom = nBottom;
	return m_nSkinH > 0;
}

// Bake a colour into the (grayscale) skin once, so draws need no per-pixel tint.
// Transparent (magenta) pixels are left untouched to preserve the transparency key.
void CSkin::Colorize (u32 nTint)
{
	if (!m_Image.IsValid () || (nTint & 0xFFFFFF) == 0x00FFFFFF)
	{
		return;
	}
	unsigned tr = (nTint >> 16) & 0xFF, tg = (nTint >> 8) & 0xFF, tb = nTint & 0xFF;
	u32 *p = m_Image.Buffer ();
	int n = m_Image.Width () * m_Image.Height ();
	for (int i = 0; i < n; i++)
	{
		u32 c = p[i];
		if ((c & 0xFFFFFF) == GIMAGE_TRANSPARENT) continue;
		unsigned r = (((c >> 16) & 0xFF) * tr) / 255;
		unsigned g = (((c >> 8)  & 0xFF) * tg) / 255;
		unsigned b = ((c & 0xFF) * tb) / 255;
		p[i] = (r << 16) | (g << 8) | b;
	}
}

void CSkin::DrawOn (GImage *pTarget, unsigned nNum, int x, int y, int w, int h,
		    boolean bTransparent)
{
	if (pTarget == 0 || !IsValid ())
	{
		return;
	}

	int sw = m_nSkinW, sh = m_nSkinH;
	int lw = m_nLeft, rw = m_nRight, th = m_nTop, bh = m_nBottom;
	int nMidW = sw - lw - rw;		// tileable middle of the skin
	int nMidH = sh - th - bh;
	int nOutW = w - lw - rw;		// area to fill between the fixed margins
	int nOutH = h - th - bh;
	int sy = sh * (int) (nNum % m_nCount);	// source Y of this state

	// Exact size: a single straight blit.
	if (w == sw && h == sh)
	{
		pTarget->PutOtherPart (&m_Image, x, y, 0, sy, sw, sh, bTransparent);
		return;
	}

	// Center fill (sample the skin's center pixel for this state -- already tinted
	// if Colorize() baked a colour in).
	if (nOutH > 0 && nOutW > 0)
	{
		u32 nFill = m_Image.GetPixel (sw / 2, sy + sh / 2);
		pTarget->FillRectangle (x + lw, y + th, x + w - rw - 1, y + h - bh - 1, nFill);
	}

	// Top + bottom edges (and center band), tiled horizontally. The last column is
	// cropped to the fill width so the middle never bleeds past the right margin.
	if (nMidW > 0 && nOutW > 0)
	{
		int nFillR = x + w - rw;			// exclusive right edge of the fill
		for (int tx = x + lw; tx < nFillR; tx += nMidW)
		{
			int tw = nMidW;
			if (tx + tw > nFillR) tw = nFillR - tx;	// crop the last tile
			pTarget->PutOtherPart (&m_Image, tx, y, lw, sy, tw, th, bTransparent);
			if (nMidH > 0)
			{
				int ch = nMidH > nOutH ? nOutH : nMidH;	// crop to the fill height
				pTarget->PutOtherPart (&m_Image, tx, y + th, lw, sy + th,
						       tw, ch, bTransparent);
			}
			pTarget->PutOtherPart (&m_Image, tx, y + h - bh, lw, sy + sh - bh,
					       tw, bh, bTransparent);
		}
	}

	// Left + right edges, tiled vertically (last row cropped to the fill height).
	if (nMidH > 0 && nOutH > 0)
	{
		int nFillB = y + h - bh;			// exclusive bottom edge of the fill
		for (int ty = y + th; ty < nFillB; ty += nMidH)
		{
			int tht = nMidH;
			if (ty + tht > nFillB) tht = nFillB - ty;
			pTarget->PutOtherPart (&m_Image, x, ty, 0, sy + th, lw, tht, bTransparent);
			pTarget->PutOtherPart (&m_Image, x + w - rw, ty, sw - rw, sy + th,
					       rw, tht, bTransparent);
		}
	}

	// Four fixed corners (drawn last so they cover any tiling overflow).
	if (lw > 0 && th > 0)
		pTarget->PutOtherPart (&m_Image, x, y, 0, sy, lw, th, bTransparent);
	if (rw > 0 && th > 0)
		pTarget->PutOtherPart (&m_Image, x + w - rw, y, sw - rw, sy, rw, th, bTransparent);
	if (lw > 0 && bh > 0)
		pTarget->PutOtherPart (&m_Image, x, y + h - bh, 0, sy + sh - bh, lw, bh, bTransparent);
	if (rw > 0 && bh > 0)
		pTarget->PutOtherPart (&m_Image, x + w - rw, y + h - bh, sw - rw, sy + sh - bh,
				       rw, bh, bTransparent);
}
