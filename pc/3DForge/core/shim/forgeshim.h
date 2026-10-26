//
// pc/3DForge/core/shim/forgeshim.h -- what 3DForge's view (user/Apps/3dforge: frender.h, ficons.h, fview.h, fdraw.h)
// takes from Onyx, for Windows: the few calls of AppKit it makes (the clock, the modifiers, the GPU -- none here: the
// frame is drawn by the processor or by OpenGL, forgecore.cpp), UIKit's canvas and painter (uikit/vpaint.cpp itself, the
// rest here: rounded boxes, tones, a few glyphs) and its text (GDI+, Segoe UI). The headers appkit/appkit.h,
// uikit/uikit.h and uikit/toolbar.h of this folder come first on the include path and lead here.
//
// MIT licence (Onyx).
//
#ifndef _forge_shim_h
#define _forge_shim_h

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kern/kapi_abi.h"		// (the GPU's vertex, batch and frame: frender.h builds its frames with them)
// (its kapi_gpu_render -- forgecore.cpp's, the processor's cores -- takes any frame: frender.h's limits are the Pi's)
#define FORGE_GPU_MAX_TARGET	16384
#define FORGE_GPU_MAX_VERTS	0x7FFFFFFF
#define FORGE_GPU_MAX_BATCHES	0x7FFFFFFF
// (the view's frame drawn by OpenGL at its own size, anti-aliased by the graphics card: core/glview.cpp; false: no
//  OpenGL here, the processor draws it)
bool forge_gl_show (const struct kapi_gpu_vertex3 *v, unsigned nv, const struct kapi_gpu_batch *b, unsigned nb, int w, int h, unsigned clear, unsigned *dst, int stride);
#define FORGE_HOST_SHOW forge_gl_show

// ---- AppKit ---------------------------------------------------------------------------------------------------------------
#undef MOD_CTRL				// (windows.h's imm.h has others)
#undef MOD_SHIFT
#undef MOD_ALT
#define MOD_CTRL		1
#define MOD_SHIFT		2
#define MOD_ALT			4
unsigned kapi_get_ticks ();			// hundredths of a second
int kapi_get_modifiers ();			// MOD_* held now (the host says: fc_view_mouse)
int kapi_gpu_info (char *out, int cap);		// 1: a GPU (forgecore.cpp's OpenGL)
int kapi_gpu_render (const struct kapi_gpu_frame *f, const struct kapi_gpu_vertex3 *v, unsigned nv, const struct kapi_gpu_batch *b, unsigned nb);

// ---- the words in the user's language (uikit/lang.h): the catalogues are Onyx's own files ("English<TAB>French"),
// loaded by forge_lang_load (forgecore.cpp's fc_lang: Windows' language chosen by the host) -------------------------------------
const char *uk_tr (const char *en);
const char *uk_trc (const char *ctx, const char *en);
#define TR(s)		uk_tr (s)
#define TRC(c, s)	uk_trc (c, s)
#define TRN(s)		(s)
bool forge_lang_load (const char *path);		// one more catalogue (false: it cannot be read)

// ---- UIKit ----------------------------------------------------------------------------------------------------------------
namespace uikit {

#define UK_TRANSPARENT_KEY 0x00FF00FFu
#define UK_AUTO 0xFFFFFFFEu

class Canvas
{
public:
	unsigned *px; int w, h, stride; bool owns;
	Canvas () : px (0), w (0), h (0), stride (0), owns (false) {}
	~Canvas () { release (); }
	void release () { if (owns) delete[] px; px = 0; w = h = stride = 0; owns = false; }
	bool alloc (int W, int H) { release (); px = new unsigned[(size_t) W * H]; w = W; h = H; stride = W; owns = true; return true; }
	void adopt (unsigned *p, int W, int H, int Stride) { release (); px = p; w = W; h = H; stride = Stride; }
	void clear (unsigned c);
	void pixel (int x, int y, unsigned c) { if (x >= 0 && y >= 0 && x < w && y < h) px[(size_t) y * stride + x] = c; }
	void fillRect (int x, int y, int rw, int rh, unsigned c);
	void frameRect (int x, int y, int rw, int rh, unsigned c);
};

// the theme's colours (the Windows version's own: a light face, a blue accent)
extern unsigned C_ACCENT, C_TEXT, C_BG, C_FIELD, C_FIELD_TEXT, C_DIS, C_BUTTON, C_BUTTON_TEXT, C_FACE;

static inline int V (int px) { return px * 16; }
int uk_sin (int deg);
static inline int uk_cos (int deg) { return uk_sin (deg + 90); }

class VPath
{
public:
	VPath ();
	~VPath ();
	void clear ();
	void poly (const int *xy, int n);
	void rect (int x, int y, int w, int h);
	void rrect (int x, int y, int w, int h, int r);
	void circle (int cx, int cy, int r);
	void hole (int cx, int cy, int r);
	void ellipse (int cx, int cy, int rx, int ry);
	void line (int x0, int y0, int x1, int y1, int w);
	void polyline (const int *xy, int n, int w, bool closed = false);
	void arc (int cx, int cy, int r, int a0, int a1, int w);
	void arrowHead (int x, int y, int deg, int len, int half);
	void fill (Canvas &cv, unsigned c, int alpha = 255, int dx = 0, int dy = 0);
private:
	int *m_x, *m_y, m_n, m_cap;
	int *m_cs, m_nc, m_ccap;
	void add (int x, int y);
	void begin ();
	void end (bool asHole = false);
};

void uk_paint_alpha (bool on);
void uk_blend_px (Canvas &cv, int x, int y, unsigned c, int a);
unsigned uk_tone (unsigned c, int level);
unsigned uk_mix (unsigned a, unsigned b, int t);
enum { UK_TL = 1, UK_TR = 2, UK_BL = 4, UK_BR = 8, UK_ALL = 15 };
void uk_rbox (Canvas &cv, int x, int y, int w, int h, int r, unsigned top, unsigned bottom, int alpha = 255, int corners = UK_ALL);
void uk_rline (Canvas &cv, int x, int y, int w, int h, int r, unsigned c, int alpha = 255, int corners = UK_ALL);
void uk_sunken (Canvas &cv, int x, int y, int w, int h, int r, unsigned bg, bool focus = false);
void uk_etch_h (Canvas &cv, int x, int y, int w, unsigned face);
void uk_etch_v (Canvas &cv, int x, int y, int h, unsigned face);
enum { WKG_CHECK, WKG_UP, WKG_DOWN, WKG_LEFT, WKG_RIGHT, WKG_CLOSE, WKG_MIN, WKG_MAX, WKG_MENU,
       WKG_DOT, WKG_PLUS, WKG_MINUS, WKG_RESTORE, WKG_CHEV_UP, WKG_CHEV_DOWN, WKG_CHEV_LEFT, WKG_CHEV_RIGHT };
void uk_glyph (Canvas &cv, int kind, int cx, int cy, int size, unsigned c);
enum { WKT_NEW, WKT_OPEN, WKT_SAVE, WKT_UNDO, WKT_REDO };
void uk_tool_glyph (Canvas &cv, int kind, int x, int y, int size, unsigned ink);

// ---- text: a face (a size of Segoe UI); the one in use is set by UkFaceScope ------------------------------------------------
class TextFace
{
public:
	int px;					// its size, pixels
	explicit TextFace (int size) : px (size) {}
	int height () const;
};
extern TextFace *uk_face_;			// (0: the default, 13 px)
struct UkFaceScope
{
	TextFace *was;
	explicit UkFaceScope (TextFace *f) : was (uk_face_) { if (f) uk_face_ = f; }
	~UkFaceScope () { uk_face_ = was; }
};
int  uk_fh ();
int  uk_tw (const char *s, int style = 0);		// style 2: bold
void uk_text (Canvas &cv, int x, int y, const char *s, unsigned c, int style = 0);
int  uk_text_fit (const char *s, int w, char *out, int cap, int style = 0);

// ---- a widget: what the view is (fview.h's View): its size, its canvas, whether it must be drawn again ---------------------
class Widget
{
public:
	int left, top, width, height;
	Canvas canvas;
	bool catchOutside, dirty, hidden;
	Widget *parent;
	Widget (int l, int t, int w, int h) : left (l), top (t), width (w), height (h), catchOutside (false), dirty (true), hidden (false), parent (0) {}
	virtual ~Widget () {}
	virtual void onDraw () {}
	virtual bool onMouse (int, int, int, int, int, int) { return false; }
	void invalidate (bool = true) { dirty = true; }
};

} // namespace uikit

// (UIKit's picture reader, ui::icon_load -- fview.h's picture_load --, is shim.cpp's: GDI+ reads PNG, JPEG, BMP, GIF, TIFF.)

#endif
