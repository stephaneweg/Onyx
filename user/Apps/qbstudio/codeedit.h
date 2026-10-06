//
// codeedit.h -- QBStudio's code editor: UIKit's CodeEdit (uikit/codeedit.h: BASIC's colours, the line numbers, the
// indentation's guides, the problems marked, undo / redo, the clipboard, the completion) set up for QBStudio --
// its faces, BASIC's words (bas::wordList), the .form text's flags, and the completion after a control's name and a
// dot (g_complete). The editor itself was this file until 2026-10-06, when it moved into the kit.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _qbstudio_codeedit_h
#define _qbstudio_codeedit_h

#include "uikit/uikit.h"
#include "systemkit/systemkit.h"
#include "form.h"

namespace qs {
using namespace uikit;

enum { LANG_BASIC = CODE_BASIC, LANG_FORM = CODE_FORM };
static TextFace *g_mono = 0;				// the code's face (DejaVu Sans Mono), 0: the UI's
static TextFace *g_ui = 0;				// the UI's (the completion's list)
static bool (*g_isKeyword) (const char *w, int n);	// a word of BASIC? (the app: bas::wordList)

// A completion: what may come here
struct Compl { char name[40]; char kind; char detail[100]; };	// kind: 'p' property, 'm' method, 'c' control, 's' a SUB, 'k' a word, 'f' a kit's function, 't' its structure, 'K' a kit
typedef void (*ComplFn) (const char *object, Vec<Compl> &out);	// object: the name before the dot ("" = none)
static ComplFn g_complete;

static inline char upc (char c) { return c >= 'a' && c <= 'z' ? (char) (c - 32) : c; }
static inline bool name_ch (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

static bool qs_is_flag (const char *s, int n) { char w[24]; if (n > 23) return false; memcpy (w, s, n); w[n] = 0; return is_flag (w); }
static void qs_complete (CodeEdit &ed, const char *object)
{
	if (!g_complete) return;
	Vec<Compl> all; g_complete (object, all);
	for (int i = 0; i < all.n; i++) ed.addCompletion (all[i].name, all[i].kind, all[i].detail);
}
// A new editor of QBStudio's (lang: LANG_BASIC / LANG_FORM)
static CodeEdit *qs_editor (int lang)
{
	CodeEdit *e = new CodeEdit (0, 0, 100, 100);
	e->lang = lang; e->mono = g_mono; e->ui = g_ui;
	e->isKeyword = g_isKeyword; e->isFlag = qs_is_flag; e->complete = qs_complete;
	return e;
}

} // namespace qs

#endif
