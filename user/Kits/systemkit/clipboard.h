//
// clipboard.h -- the clipboard, shared by every app (docs/clipboard/README.md): the service `clipd`
// keeps the last CLIP_RING copies (clipproto.h), its widget (the dock's clipboard button) shows
// them and moves the cursor -- the item Ctrl+V pastes. Header-only, freestanding (the kapi, new[]).
//
//   clip_set_text ("hello");            char b[256]; clip_get_text (b, sizeof b);
//   clip_set_files ("SD:/a.txt", cut);  int cut; clip_get_file (b, sizeof b, &cut);
//   clip_set_image (px, w, h);          int w, h; unsigned *px = clip_get_image (&w, &h); delete[] px;
//   clip_put (fmts, datas, lens, n);    several formats of one copy (Letters: "rtf" and "text")
//   clip_get (fmts, nf, got, cap, &data, &len)   the first format of fmts the item has (data: new[])
//
// A copy is also kept by the kernel's clipboard (one text or one path: v40), which is what is
// pasted when clipd cannot be reached (an older card, the PC's desktop simulator).
//
#ifndef _clipboard_h
#define _clipboard_h
#include "appkit/appkit.h"
#include "sk_api.h"
#include "clipproto.h"
// ---- the service -------------------------------------------------------------------------------------
// clipd's pid; launched when it is not running (0: not there)
SK_API int clip_service_ (void);
// ---- copying -----------------------------------------------------------------------------------------
// One copy, n representations (a format and its bytes each). False: clipd could not be reached.
SK_API bool clip_put (const char *const *fmt, const void *const *data, const unsigned *len, int n);
// ---- pasting -----------------------------------------------------------------------------------------
// The item under the cursor in the first of fmt[] it has (else the newest item that has one of them):
// its format into got (cap), its bytes into *data (new[]: delete[] it), *len. False: nothing suits,
// or clipd could not be reached.
SK_API bool clip_get (const char *const *fmt, int nf, char *got, int cap, unsigned char **data, unsigned *len);
// ---- text and paths (the calls every app had) ----------------------------------------------------------------
SK_API void clip_set_text_n (const char *s, int n);
SK_API void clip_set_text (const char *s);
// The clipboard's text into buf (NUL-terminated, truncated to cap-1) -> its length; 0: no text.
SK_API int clip_get_text (char *buf, int cap);
// File / folder paths ('\n'-separated): copied, or cut when `cut` != 0 (the paste moves them).
SK_API void clip_set_files (const char *paths, int cut);
// The first path into buf; *cut = 1 if it was cut. 0 if no path.
SK_API int clip_get_file (char *buf, int cap, int *cut);
// After a cut + paste moved the files: that item goes (clipd), and the kernel's copy.
SK_API void clip_clear (void);
// ---- images ---------------------------------------------------------------------------------------
// w x h pixels 0x00RRGGBB (a row after the other)
SK_API bool clip_set_image (const unsigned *px, int w, int h);
// -> new[] pixels (delete[] them) and the size, or 0: no image
SK_API unsigned *clip_get_image (int *w, int *h);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "clipboard.inc"
#endif

#endif
