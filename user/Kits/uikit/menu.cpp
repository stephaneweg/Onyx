//
// uikit/menu.cpp -- see menu.h. The spec format is the uk_win_menu_set one (uikit/win.h):
// "M<title>" / "I<id>\t<label>\t<shortcut>" / "-", one per line; item ids are indices
// into m_cb.
//
#include "uikit/menu.h"
#include "uikit/root.h"
#include "uikit/win.h"		// uk_win_menu_set, uk_win_menu_command
#include "uikit/internal/adapt_int.h"

namespace uikit {

static Menu *s_current = 0;

Menu::Menu () : m_len (0), m_count (0) { m_spec[0] = '\0'; }

void Menu::put (const char *s)
{
	for (int i = 0; s[i] && m_len < (int) sizeof m_spec - 1; i++) m_spec[m_len++] = s[i];
	m_spec[m_len] = '\0';
}

void Menu::menu (const char *title) { put ("M"); put (title); put ("\n"); }

void Menu::separator () { put ("-\n"); }

void Menu::item (const char *label, const char *shortcutText, long key, MenuAction cb)
{
	if (m_count >= MAXITEMS) return;
	char id[8]; int n = 0, v = m_count;
	char t[8]; int k = 0;
	if (v == 0) t[k++] = '0';
	while (v) { t[k++] = (char) ('0' + v % 10); v /= 10; }
	while (k) id[n++] = t[--k];
	id[n] = '\0';
	put ("I"); put (id); put ("\t"); put (label); put ("\t"); put (shortcutText ? shortcutText : ""); put ("\n");
	m_cb[m_count] = cb;
	m_key[m_count] = key;
	m_count++;
}

// The window's drawers (uikit/internal/adapt_int.h menu_extra): their lines go before the menus' -- a menu bar that
// does not know them skips them (a line is a menu "M", an item "I" or a separator "-").
static char s_extra[96];
static char s_pub[WIN_MENU_MAX_USER];
void Menu::publish ()
{
	s_current = this;
	if (s_extra[0] == 0) { uk_win_menu_set (m_spec, handler); return; }
	int n = 0;
	for (int i = 0; s_extra[i] && n < (int) sizeof s_pub - 1; i++) s_pub[n++] = s_extra[i];
	for (int i = 0; m_spec[i] && n < (int) sizeof s_pub - 1; i++) s_pub[n++] = m_spec[i];
	s_pub[n] = 0;
	uk_win_menu_set (s_pub, handler);
}
namespace internal {
void menu_extra (const char *lines)
{
	int i = 0; bool same = true;
	for (; lines[i] && i < (int) sizeof s_extra - 1; i++) { if (s_extra[i] != lines[i]) same = false; }
	if (same && s_extra[i] == 0) return;
	for (i = 0; lines[i] && i < (int) sizeof s_extra - 1; i++) s_extra[i] = lines[i];
	s_extra[i] = 0;
	if (s_current) s_current->publish ();
}
}

Menu *Menu::current () { return s_current; }

// A modal dialog (file dialog, message box, input box) owns the input: no menu command
// or shortcut runs underneath it (e.g. Del in a rename box must not delete the file).
static bool modal_open ()
{
	Root *r = Root::current ();
	if (r == 0) return false;
	for (Widget *n = r->lastChild; n; n = n->prevSib) if (n->modal) return true;
	return false;
}

bool Menu::shortcut (long key)
{
	if (modal_open ()) return false;
	if (key == UK_CTRL ('Q')) { uk_win_menu_command (MENU_QUIT); return true; }	// Quit
	if (key <= 0) return false;
	// ^H, ^I and ^M share their codes with Backspace, Tab and Enter: without Ctrl held, the
	// key is the editing key and goes to the focused widget, not to the menu.
	if ((key == KEY_BACKSPACE || key == KEY_TAB || key == KEY_ENTER) && !(kapi_get_modifiers () & MOD_CTRL))
		return false;
	for (int i = 0; i < m_count; i++)
		if (m_key[i] == key && m_cb[i]) { m_cb[i] (); if (Root::current ()) Root::current ()->invalidate (true); return true; }
	return false;
}

void Menu::handler (unsigned long, int ev, gui_value v)
{
	Menu *m = s_current;
	if (m == 0 || ev != GUI_EVENT_MENU || v < 0 || v >= m->m_count || m->m_cb[v] == 0) return;
	if (modal_open ()) return;
	m->m_cb[v] ();
	if (Root::current ()) Root::current ()->invalidate (true);
}

} // namespace uikit
