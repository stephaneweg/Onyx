//
// uikit/theme.h -- the desktop's look (docs/gui-redesign/README.md, "a modernised CDE"): the palette
// every widget and every window frame draws with. A theme gives ONE colour per kind of window
// frame (active: the window in front; inactive: the others) and the apps' face; every other shade
// is computed from those (uikit/paint.h: the grey profile). Read once by uikit::init () from
// SD:/etc/theme.txt:
//
//     theme    = Peach        a named theme. Two kinds: CDE's framed title buttons -- Peach, Steel,
//                             Sage, Brick, Slate (the frame of the window in front; the ones
//                             behind: Grey) --, or OS X's coloured beads, the frame melting into
//                             the window (no line between them) -- Milk (soft greys) and Dark
//                             Coffee (black coffee's browns, a caramel accent)
//     style    = cde          the frames' look, if not the theme's: cde or milk
//     active   = 0xF0B07A     or the colours themselves (they override the theme's)
//     inactive = 0xACACB0
//     window   = 0xD0C2BA     the windows' content: the apps' face, their background ("face"
//                             before: still read)
//     button   = 0xD0C2BA     the push buttons, the drop-downs' faces (default: the window's)
//     field    = 0xF6F3F1     the text fields and the lists (default: the window's, nearly white)
//     accent   = 0x4992A7     focus, selection, checks
//     outline  = dark         the frames' 1-px outline: none, dark or black
//     menubar  = 0xD0C2BA     the menu bar (default: the window's)
//     dock     = 0xA4BACE     the dock's face
//
// Without the file (or a key), the defaults below: Peach over CDE's beige, a teal accent; a colour
// left out takes the style's own (uk_style_palette: Milk's greys, its Aqua blue...). Each text
// colour is black or white by the brightness of what it is written on.
//
#ifndef _uikit_theme_h
#define _uikit_theme_h

#include "uikit/global.h"

namespace uikit {

// The palette (0x00RRGGBB), set by uk_theme_load (); the names are the toolkit's since its start.
UIKIT_VAR (unsigned, C_BG);			// an app's background (its face)
UIKIT_VAR (unsigned, C_FACE);			// a control's face (a button, a handle)
UIKIT_VAR (unsigned, C_FACE_HI);		// ... under the pointer
UIKIT_VAR (unsigned, C_FACE_DN);		// ... pressed
UIKIT_VAR (unsigned, C_BORDER);		// a control's outline
UIKIT_VAR (unsigned, C_TEXT);			// text on the face
UIKIT_VAR (unsigned, C_ACCENT);		// focus, selection, checks, carets
UIKIT_VAR (unsigned, C_DIS);			// disabled text
UIKIT_VAR (unsigned, C_FIELD);		// a text field's, a list's background
UIKIT_VAR (unsigned, C_FIELD_TEXT);		// text in a field
UIKIT_VAR (unsigned, C_SEL_TEXT);		// text on the accent (a selected row)
UIKIT_VAR (unsigned, C_FRAME_ACTIVE);		// the window in front's frame
UIKIT_VAR (unsigned, C_FRAME_INACTIVE);	// the other windows' frames
UIKIT_VAR (int, UK_OUTLINE);		// the frames' 1-px outline: 0 none, 1 dark, 2 black
UIKIT_VAR (unsigned, C_DOCK);			// the dock's face (and the drawers' frame)
UIKIT_VAR (unsigned, C_BUTTON);		// a push button's / a drop-down's face (the window's by default)
UIKIT_VAR (unsigned, C_BUTTON_TEXT);		// ... its text
UIKIT_VAR (unsigned, C_MENUBAR);		// the menu bar's face (the window's by default)

// The frames' look: CDE's (framed title buttons) or Milk's (Xfce's Milk theme, as OS X: the title
// buttons coloured beads -- close red, minimise amber, maximise green --, the title's gradient
// down to the window's colour, C_BG, the borders that colour: the frame melts into the window).
enum { UK_STYLE_CDE = 0, UK_STYLE_MILK = 1 };
UIKIT_VAR (int, UK_STYLE);
// A style's own colours: what a theme of it takes when theme.txt does not say.
// (button, field: 0xFF000000 = from the window's, UK_AUTO below; outline: 0 none, 1 dark, 2 black.)
struct UkPalette { unsigned face, accent, inactive, dock, button, field; int outline; };
const UkPalette &uk_style_palette (int style);

// The named themes (the active frame's colour, the frames' style; pal: a theme with colours of its
// own -- Dark Coffee --, 0: its style's).
struct UkNamedTheme { const char *name; unsigned frame; int style; const UkPalette *pal; };
const UkPalette &uk_theme_palette (int theme);	// a named theme's colours (its own, else its style's)
extern const UkNamedTheme uk_themes[];	// ..., { 0, 0, 0 }
static const unsigned UK_GREY = 0x00ACACB0;	// the inactive frames

void uk_theme_load ();			// (uikit::init () calls it; idempotent)
void uk_theme_reload ();		// read SD:/etc/theme.txt again (a new theme applied: the dock)
void uk_theme_face (unsigned face);	// the palette's shades from a face colour (a preview)

// The whole theme as values (the Theme applet edits one, previews it, writes it). UK_AUTO: a
// colour derived from the window's (button, field, menubar).
static const unsigned UK_AUTO = 0xFF000000u;
struct UkTheme
{
	int theme;				// uk_themes[] index of the active frame's colour, -1: `active`
	unsigned active, inactive, window, button, field, accent, menubar, dock;
	int outline;				// 0 none, 1 dark, 2 black
	int style;				// the frames' look: UK_STYLE_CDE / UK_STYLE_MILK
};
// A theme's style changed (a scheme chosen): the colours that are the style's own -- the window,
// the accent, the frames behind, the dock -- become the new style's (what the user set otherwise
// is kept); the buttons, the fields and the menu bar follow the window's.
void uk_theme_take_style (UkTheme &t, int style);
// A named theme chosen (uk_themes[theme]): its frame, its style, and -- when its colours are not
// those of the theme left -- its window, accent, frames behind, dock, buttons, fields, outline.
void uk_theme_take (UkTheme &t, int theme);
void uk_theme_defaults (UkTheme &t);
void uk_theme_parse (const char *text, UkTheme &t);	// theme.txt's keys over t
void uk_theme_get (UkTheme &t);			// the palette in use
void uk_theme_set (const UkTheme &t);		// ... made this one (every shade computed)
int  uk_theme_write (const UkTheme &t, char *out, int cap);	// theme.txt's text -> its length

} // namespace uikit

#endif
