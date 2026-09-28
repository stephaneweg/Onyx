//
// wtk/theme.h -- the desktop's look (docs/gui-redesign/README.md, "a modernised CDE"): the palette
// every widget and every window frame draws with. A theme gives ONE colour per kind of window
// frame (active: the window in front; inactive: the others) and the apps' face; every other shade
// is computed from those (wtk/paint.h: the grey profile). Read once by wtk::init () from
// SD:/etc/theme.txt:
//
//     theme    = Peach        a named colour theme: Peach, Steel, Sage, Brick, Slate (the frame
//                             of the window in front; the ones behind: Grey)
//     active   = 0xF0B07A     or the colours themselves (they override the theme's)
//     inactive = 0xACACB0
//     face     = 0xD0C2BA     the apps' face: their background, the buttons
//     accent   = 0x4992A7     focus, selection, checks
//     outline  = dark         the frames' 1-px outline: none, dark or black
//
// Without the file (or a key), the defaults below: Peach over CDE's beige, a teal accent.
//
#ifndef _wtk_theme_h
#define _wtk_theme_h

namespace wtk {

// The palette (0x00RRGGBB), set by wk_theme_load (); the names are the toolkit's since its start.
extern unsigned C_BG;			// an app's background (its face)
extern unsigned C_FACE;			// a control's face (a button, a handle)
extern unsigned C_FACE_HI;		// ... under the pointer
extern unsigned C_FACE_DN;		// ... pressed
extern unsigned C_BORDER;		// a control's outline
extern unsigned C_TEXT;			// text on the face
extern unsigned C_ACCENT;		// focus, selection, checks, carets
extern unsigned C_DIS;			// disabled text
extern unsigned C_FIELD;		// a text field's, a list's background
extern unsigned C_FIELD_TEXT;		// text in a field
extern unsigned C_SEL_TEXT;		// text on the accent (a selected row)
extern unsigned C_FRAME_ACTIVE;		// the window in front's frame
extern unsigned C_FRAME_INACTIVE;	// the other windows' frames
extern int	WK_OUTLINE;		// the frames' 1-px outline: 0 none, 1 dark, 2 black

// The named colour themes (the active frame's colour).
struct WkNamedTheme { const char *name; unsigned frame; };
extern const WkNamedTheme wk_themes[];	// ..., { 0, 0 }
static const unsigned WK_GREY = 0x00ACACB0;	// the inactive frames

void wk_theme_load ();			// (wtk::init () calls it; idempotent)
void wk_theme_face (unsigned face);	// the palette's shades from a face colour (a preview)

} // namespace wtk

#endif
