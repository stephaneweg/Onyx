//
// wtk/menu.h -- an app's menus for the SYSTEM menu bar (top of the screen, the
// `menubar` app). Build them once after the Root window exists, then publish():
//
//   static Menu menu;
//   menu.menu ("File");
//   menu.item ("Open...", "^O", WK_CTRL ('O'), onOpen);
//   menu.separator ();
//   menu.item ("Save",    "^S", WK_CTRL ('S'), onSave);
//   menu.publish ();
//
// The menu bar shows them while this app is the active one and sends the chosen item
// back (GUI_EVENT_MENU); Menu runs the item's callback. Menu also handles the items'
// keyboard shortcuts (Root routes every key through Menu::shortcut first) and ^Q =
// Quit. The bar adds the app-name menu with "Quit" itself.
//
#ifndef _wtk_menu_h
#define _wtk_menu_h
#include "kapi.h"
namespace wtk {

#define WK_CTRL(c)	((long) ((c) & 0x1F))	// Ctrl+letter key code (^A = 1 ... ^Z = 26)

typedef void (*MenuAction) ();

class Menu
{
public:
	Menu ();
	void menu (const char *title);				// start a new menu
	void item (const char *label, const char *shortcutText, long key, MenuAction cb);
	void separator ();
	void publish ();					// hand the menus to the menu bar
	bool shortcut (long key);				// run the item bound to `key`
	static Menu *current ();				// the published menu (or 0)
private:
	enum { MAXITEMS = 64 };
	char	   m_spec[WIN_MENU_MAX_USER];
	int	   m_len;
	MenuAction m_cb[MAXITEMS];
	long	   m_key[MAXITEMS];
	int	   m_count;
	void put (const char *s);
	static void handler (unsigned long, int ev, long v);
};

} // namespace wtk
#endif
