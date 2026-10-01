//
// wtk/theme.h -- the desktop's look (docs/gui-redesign/README.md, "a modernised CDE"): the palette
// every widget and every window frame draws with. A theme gives ONE colour per kind of window
// frame (active: the window in front; inactive: the others) and the apps' face; every other shade
// is computed from those (wtk/paint.h: the grey profile). Read once by wtk::init () from
// SD:/etc/theme.txt:
//
//     theme    = Peach        a named theme: Peach, Steel, Sage, Brick, Slate (the frame of the
//                             window in front; the ones behind: Grey) -- or Milk (soft greys and
//                             OS X's coloured beads for the title buttons, a lighter gradient)
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
// left out takes the style's own (wk_style_palette: Milk's greys, its Aqua blue...). Each text
// colour is black or white by the brightness of what it is written on.
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
extern unsigned C_DOCK;			// the dock's face (and the drawers' frame)
extern unsigned C_BUTTON;		// a push button's / a drop-down's face (the window's by default)
extern unsigned C_BUTTON_TEXT;		// ... its text
extern unsigned C_MENUBAR;		// the menu bar's face (the window's by default)

// The frames' look: CDE's (framed title buttons) or Milk's (Xfce's Milk theme, as OS X: the title
// buttons coloured beads -- close red, minimise amber, maximise green --, a lighter gradient).
enum { WK_STYLE_CDE = 0, WK_STYLE_MILK = 1 };
extern int	WK_STYLE;
// A style's own colours: what a theme of it takes when theme.txt does not say.
struct WkPalette { unsigned face, accent, inactive, dock; };
const WkPalette &wk_style_palette (int style);

// The named themes (the active frame's colour, the frames' style).
struct WkNamedTheme { const char *name; unsigned frame; int style; };
extern const WkNamedTheme wk_themes[];	// ..., { 0, 0, 0 }
static const unsigned WK_GREY = 0x00ACACB0;	// the inactive frames

void wk_theme_load ();			// (wtk::init () calls it; idempotent)
void wk_theme_reload ();		// read SD:/etc/theme.txt again (a new theme applied: the dock)
void wk_theme_face (unsigned face);	// the palette's shades from a face colour (a preview)

// The whole theme as values (the Theme applet edits one, previews it, writes it). WK_AUTO: a
// colour derived from the window's (button, field, menubar).
static const unsigned WK_AUTO = 0xFF000000u;
struct WkTheme
{
	int theme;				// wk_themes[] index of the active frame's colour, -1: `active`
	unsigned active, inactive, window, button, field, accent, menubar, dock;
	int outline;				// 0 none, 1 dark, 2 black
	int style;				// the frames' look: WK_STYLE_CDE / WK_STYLE_MILK
};
// A theme's style changed (a scheme chosen): the colours that are the style's own -- the window,
// the accent, the frames behind, the dock -- become the new style's (what the user set otherwise
// is kept); the buttons, the fields and the menu bar follow the window's.
void wk_theme_take_style (WkTheme &t, int style);
void wk_theme_defaults (WkTheme &t);
void wk_theme_parse (const char *text, WkTheme &t);	// theme.txt's keys over t
void wk_theme_get (WkTheme &t);			// the palette in use
void wk_theme_set (const WkTheme &t);		// ... made this one (every shade computed)
int  wk_theme_write (const WkTheme &t, char *out, int cap);	// theme.txt's text -> its length

} // namespace wtk

#endif
