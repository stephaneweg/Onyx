//
// uikit/sidepanel.h -- SidePanel: a side panel that says what it is -- a NAVIGATION list at the left (the places of a
// library, the mailboxes, the modules) or an INSPECTOR at the right (a layers' list, the properties) -- so that each
// UIKit shows it as its mode wants (docs/POCKETUI-TECH-STUDY.md section 6.8, phase P6; docs/03 "The adaptive widgets"):
//
//   presentation    the desktop       pocket, landscape            pocket, portrait          console
//   navigation      UK_SP_FULL        UK_SP_RAIL (icons; the       UK_SP_DRAWER (opened by   UK_SP_COLUMN (big rows,
//                   (today's sidebar)  labels on hover / focus)      its button or its tab)    L1 / R1 the sections)
//   inspector       UK_SP_FULL        UK_SP_SLIDEOVER (over the    UK_SP_SHEET (a bottom     UK_SP_SLIDEOVER (opened
//                                     content from the right)      sheet, half or whole)     on demand)
//   (a wide pocket screen -- the window at least four times the panel's width -- keeps UK_SP_FULL; a navigation panel
//    of free content only, without items, goes from UK_SP_FULL to a drawer: a rail could not show it)
//
// Its content: STRUCTURED ITEMS (what a rail, a drawer, a console column can all draw) -- headings (foldable), items
// with an icon, an indent, a badge (a count, a dot), a short value at the right, a picture (a layer, a page), a toggle
// (a layer's eye) -- and FREE CONTENT as the fallback: pages (an inspector's tabs: any widget each), a content under
// the items (a tree, a month), a footer (the chosen item's controls, buttons). The app lays its own content out with
// reservedWidth () (what the panel takes now at its side: the rail's width, 0 for an overlay) and onPresentation.
//
//   SidePanel *sp = new SidePanel (0, 52, 208, h, UK_SP_LEFT, UK_SP_NAVIGATION);
//   sp->setIconFn (my_icon);                         // the app's icons (else: icon = a WKG_* glyph)
//   sp->addItem (1, "Home", I_HOME);
//   sp->addHeading ("LIBRARY");  sp->addItem (2, "Artists", I_PERSON);  ...
//   sp->onSelect = go;  sp->select (1);
//   sp->place (0, 52, 208, h);  content->left = sp->reservedWidth ();  (again in onPresentation and the window's resize)
//
// On the desktop it is today's sidebar (rows of 28 px, the chosen one in the accent; the headings small, bold, dim);
// its width is the app's (setWidths gives the range a splitter may use).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef _uikit_sidepanel_h
#define _uikit_sidepanel_h

#include "uikit/widget.h"
#include "uikit/text.h"

namespace uikit {

enum { UK_SP_LEFT = 0, UK_SP_RIGHT = 1 };				// the side
enum { UK_SP_NAVIGATION = 0, UK_SP_INSPECTOR = 1 };			// the role
enum { UK_SP_FULL = 0, UK_SP_RAIL, UK_SP_DRAWER, UK_SP_SLIDEOVER, UK_SP_SHEET, UK_SP_COLUMN, UK_SP_HIDDEN };	// presentation ()
enum { UK_SPI_HEADING = 1, UK_SPI_FOLDABLE = 2, UK_SPI_DISABLED = 4, UK_SPI_SEPARATOR = 8, UK_SPI_FOLDED = 16 };	// an item's flags
enum { UK_SP_TOGGLE_ON = -2, UK_SP_TOGGLE_OFF = -3 };		// the icon ids an icon function gets for a toggle

// The app's icons: icon `id` drawn in the size x size box at (x, y) in ink (selected: on the accent).
typedef void (*SpIconFn) (Canvas &cv, int id, int x, int y, int size, unsigned ink, bool selected);

class SidePanel : public Widget
{
public:
	SidePanel (int l, int t, int w, int h, int side = UK_SP_LEFT, int role = UK_SP_NAVIGATION);
	~SidePanel () override;

	// ---- the structured items -----------------------------------------------------------------------------------
	int  addHeading (const char *label, unsigned flags = 0);	// -> its id (UK_SPI_FOLDABLE: a click folds it)
	int  addSeparator ();					// -> its id
	// An item (id: the app's, >= 0) with an icon (-1: none), an indent level, under a heading (-1: the last one added).
	int  addItem (int id, const char *label, int icon = -1, int level = 0, int under = -1);
	void setLabel (int id, const char *label);		// (labels are copied, 63 bytes at most)
	void setIcon (int id, int icon);
	void setBadge (int id, int count);			// 0: none; > 0 a pill ("3"); -1 a dot
	void setTrailing (int id, const char *text);		// a short value at the right ("80 %", "12")
	// A line of help under the label (copied, 95 bytes at most): shown when the panel is whole, 240 px wide or more
	// and every item fits at two lines (a settings list on a big screen); else the label alone.
	void setSubtitle (int id, const char *text);
	void setThumb (int id, const unsigned *px, int w, int h);	// a picture (copied; 0: none) -- the rows grow for it
	void setToggle (int id, int on);			// -1: none; 0 / 1: a toggle (a layer's eye) before the label
	void setFlags (int id, unsigned flags);
	unsigned flags (int id);
	void moveItem (int id, int beforeId);			// (-1: to the end)
	void remove (int id);
	void clear ();						// every item (the pages, the content and the footer stay)
	void select (int id, bool fire = false);		// (-1: none)
	int  selected ();
	int  itemAt (int x, int y);				// the item under (x, y) of the panel, -1
	bool dropAt (int x, int y, int type, const char *data, int len);	// a drop the app's Root got over the panel -> onDrop

	void (*onSelect) (SidePanel &, int id);			// an item chosen (a click, Enter, L1 / R1)
	void (*onToggle) (SidePanel &, int id, int on);		// its toggle clicked (on: the new state)
	void (*onItemMenu) (SidePanel &, int id, int x, int y);	// a right click (x, y: the window's)
	bool (*onDrop) (SidePanel &, int id, int type, const char *data, int len);	// a file dropped on a place (dropAt)
	void (*onReorder) (SidePanel &, int id, int beforeId);	// an item dragged (0: the items do not move)
	void (*onPresentation) (SidePanel &, int presentation);	// the presentation changed: lay the content out again

	// ---- the free content -------------------------------------------------------------------------------------------
	int  addPage (const char *label, int icon, Widget *content);	// an inspector's page -> its number (2 and more: tabs)
	void setPage (int page);
	int  page ();
	void setContent (Widget *w);			// a navigation panel's content under its items (a tree, a month)
	void setFooter (Widget *w, int h);		// under the rest: h px (the chosen item's controls, a button)

	// ---- the look, the geometry, the state ------------------------------------------------------------------------
	void setIconFn (SpIconFn fn);
	void setFaces (TextFace *item, TextFace *heading);	// (0: the installed face; the headings bold)
	void setColors (unsigned bg, unsigned edge);		// UK_AUTO: the sidebar's (the window's face towards the fields')
	// The panel's rectangle in its parent as the app lays it out (its resize code): what the presentation shows of it
	// follows (a rail narrows it, a closed drawer is a tab at its edge). Use it rather than left / top / resizeTo,
	// which are taken as what the parent's anchors did to the panel shown.
	void place (int l, int t, int w, int h);
	void setWidths (int minW, int prefW, int maxW);		// the desktop: a splitter's range; prefW the panel's width
	// false: never a rail -- in pocket's landscape a navigation panel whose labels matter (a settings list) stays whole
	void setRail (bool allowed);
	int  presentation ();				// UK_SP_* now (the desktop: UK_SP_FULL)
	int  reservedWidth ();				// what the app leaves for it at its side now (a rail: its width; 0: overlaid)
	void open (bool on);				// a drawer, a slide-over, a sheet opened / closed (the rail: expanded)
	bool isOpen ();
	// A button for the app's own bar that opens and closes it (portrait's "menu" of a navigation drawer, an
	// inspector's): made once, the app places it (hidden on the desktop and whenever the panel is shown whole);
	// asked, the panel draws no tab of its own at its edge.
	Widget *toggleButton ();

	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
	void layout () override;
	void resizeTo (int w, int h) override;		// (the app's size kept: the presentation decides the panel's)
	unsigned bgColor () override;

	unsigned long sp_reserved_[4] = { 0, 0, 0, 0 };	// (uikit/abi.h rule 2: room for later; the state is behind ext)
	virtual void sp_reserved0 () {}			// (rule 3: room for later virtuals)
	virtual void sp_reserved1 () {}
private:
	int m_side, m_role;
};

} // namespace uikit

#endif
