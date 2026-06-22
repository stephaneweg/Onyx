//
// gimage.h
//
// Software-rendered 32-bit image buffer -- the rendering core, ported to C++ from
// SimpleOS's FreeBASIC GImage (graphics/gimage.bas). Pixels are u32 0x00RRGGBB
// (matching Circle's COLOR32 with DEPTH=32). A GImage can own its buffer (windows,
// off-screen images) or wrap an existing one (the framebuffer back buffer).
//
#ifndef _kern_gui_gimage_h
#define _kern_gui_gimage_h

#include <circle/types.h>

// Magenta is the transparency key for blits, as in SimpleOS.
#define GIMAGE_TRANSPARENT	0xFF00FF

class GImage
{
public:
	GImage (void);				// empty; call SetSize() to allocate
	GImage (u32 *pBuffer, int nWidth, int nHeight);	// wrap an existing buffer (no ownership)
	~GImage (void);

	// (Re)point this image at an existing buffer without taking ownership. Used
	// to re-wrap the framebuffer back buffer, which may change after a present.
	void Wrap (u32 *pBuffer, int nWidth, int nHeight);

	// Allocate an owned buffer of the given size (grows only when needed).
	void SetSize (int nWidth, int nHeight);

	boolean IsValid (void) const	{ return m_pBuffer != 0; }
	int Width (void) const		{ return m_nWidth; }
	int Height (void) const		{ return m_nHeight; }
	u32 *Buffer (void) const	{ return m_pBuffer; }

	void Clear (u32 nColor);
	void SetPixel (int x, int y, u32 nColor);
	u32  GetPixel (int x, int y) const;

	void DrawLine (int x1, int y1, int x2, int y2, u32 nColor);
	void DrawRectangle (int x1, int y1, int x2, int y2, u32 nColor);
	void FillRectangle (int x1, int y1, int x2, int y2, u32 nColor);

	// Text (Circle's built-in bitmap font; transparent background -- only the glyph
	// pixels are drawn). Top-left at (x,y).
	void DrawChar (int x, int y, char chAscii, u32 nColor);
	void DrawText (int x, int y, const char *pText, u32 nColor);
	static int FontWidth (void);				// fixed glyph cell width
	static int FontHeight (void);
	static int TextWidth (const char *pText);		// pixel width of a string

	// Blit another image at (x,y). With bTransparent, pixels equal to
	// GIMAGE_TRANSPARENT are skipped. Clipped to this image's bounds.
	void PutOther (const GImage *pSrc, int x, int y, boolean bTransparent);
	// Blit a raw source buffer (opaque), clipped.
	void PutOtherRaw (const u32 *pSrc, int nSrcW, int nSrcH, int x, int y);

private:
	void CreateBuffer (void);

private:
	int	m_nWidth;
	int	m_nHeight;
	u32    *m_pBuffer;
	unsigned m_nBufferSize;		// bytes, for the grow-only owned buffer
	boolean	m_bOwnsBuffer;
};

#endif
