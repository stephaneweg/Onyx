//
// wtk/menu.cpp -- see menu.h. The spec format is the kapi_set_menu one (kapi_abi.h):
// "M<title>" / "I<id>\t<label>\t<shortcut>" / "-", one per line; item ids are indices
// into m_cb.
//
#include "wtk/menu.h"
#include "wtk/root.h"

namespace wtk {

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

void Menu::publish ()
{
	s_current = this;
	kapi_set_menu (m_spec, handler);
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
	if (key == WK_CTRL ('Q')) { kapi_menu_command (MENU_QUIT); return true; }	// Quit
	if (key <= 0) return false;
	for (int i = 0; i < m_count; i++)
		if (m_key[i] == key && m_cb[i]) { m_cb[i] (); if (Root::current ()) Root::current ()->invalidate (true); return true; }
	return false;
}

void Menu::handler (unsigned long, int ev, long v)
{
	Menu *m = s_current;
	if (m == 0 || ev != GUI_EVENT_MENU || v < 0 || v >= m->m_count || m->m_cb[v] == 0) return;
	if (modal_open ()) return;
	m->m_cb[v] ();
	if (Root::current ()) Root::current ()->invalidate (true);
}

} // namespace wtk
