//
// clipboard.h -- system clipboard helpers (ABI v40 kapi_clipboard_set/get). The kernel
// keeps ONE typed blob, so a copy survives the app that made it and every app shares it:
//   clip_set_text ("hello");            char b[256]; clip_get_text (b, sizeof b);
//   clip_set_files ("SD:/a.txt", cut);  int cut; clip_get_file (b, sizeof b, &cut);
//
#ifndef _clipboard_h
#define _clipboard_h
#include "kapi.h"

static inline int clip_len_ (const char *s) { int n = 0; while (s && s[n]) n++; return n; }

// Copy text (NUL-terminated) to the clipboard.
static inline void clip_set_text (const char *s) { kapi_clipboard_set (CLIP_TEXT, s, (unsigned) clip_len_ (s)); }
static inline void clip_set_text_n (const char *s, int n) { kapi_clipboard_set (CLIP_TEXT, s, (unsigned) (n < 0 ? 0 : n)); }

// The clipboard text into buf (NUL-terminated, truncated to cap-1). Returns its length,
// 0 if the clipboard is empty or holds something else than text.
static inline int clip_get_text (char *buf, int cap)
{
	int type = 0;
	int n = kapi_clipboard_get (&type, buf, (unsigned) (cap - 1), 0);
	if (type != CLIP_TEXT) { buf[0] = '\0'; return 0; }
	if (n > cap - 1) n = cap - 1;
	buf[n] = '\0';
	return n;
}

// A file/folder path (copy, or cut when `cut` != 0 -- the paste then moves it).
static inline void clip_set_files (const char *path, int cut)
{ kapi_clipboard_set (cut ? CLIP_FILES_CUT : CLIP_FILES, path, (unsigned) clip_len_ (path)); }

// The first clipboard path into buf; *cut = 1 if it was cut. 0 if no path.
static inline int clip_get_file (char *buf, int cap, int *cut)
{
	int type = 0;
	int n = kapi_clipboard_get (&type, buf, (unsigned) (cap - 1), 0);
	if (type != CLIP_FILES && type != CLIP_FILES_CUT) { buf[0] = '\0'; return 0; }
	if (n > cap - 1) n = cap - 1;
	buf[n] = '\0';
	for (int i = 0; i < n; i++) if (buf[i] == '\n') { buf[i] = '\0'; n = i; break; }
	if (cut) *cut = type == CLIP_FILES_CUT;
	return n;
}

// Empty the clipboard (after a cut+paste moved the file).
static inline void clip_clear (void) { kapi_clipboard_set (0, 0, 0); }

#endif
