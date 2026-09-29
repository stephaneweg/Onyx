//
// app.h -- what Cardfile's views share: the document, the record shown, the records shown and their order,
// and the app's functions they call (main.cpp).
//
#ifndef _cardfile_app_h
#define _cardfile_app_h

#include "widgets.h"

namespace cf {

enum { V_FORM, V_LIST, V_DESIGN };

static Doc      g_doc;
static char     g_path[200];			// "" : not saved yet
static int      g_cur = -1;			// the record shown (its index in g_doc.r), -1: none
static int     *g_ord, g_nord, g_ordCap;	// the records shown (their indexes), in the views' order
static char     g_search[64];			// the search's words ("" : every record)
static int      g_pin = -1;			// a record just made: shown whatever the search
static unsigned g_fieldsVer = 1;		// the fields' version (a view built for an older one: rebuilt)
static int      g_fsel;				// the field chosen in the design view
static int      g_view = V_FORM;

// (main.cpp)
static int  cur_pos ();				// the record shown's place in g_ord, -1
static void goto_pos (int pos);			// show the record at that place (the edits kept first)
static void undo_mark (int key);		// before a change: the document kept for Undo (key >= 0: the
						// same edit going on -- one step for all of it)
static void doc_changed (bool fields);		// after a change: the order and the views follow
static void select_record (int rec);		// another record chosen (in the list)
static void undo_break ();			// the next change a step of its own
static void status (const char *msg);		// a word in the status bar for a few seconds
static void cmd_new_record ();
static void cmd_clear_search ();
static void cmd_del_record ();
static void show_view (int v);
static int  ask (const char *title, const char *text, int buttons, int icon = 1);

} // namespace cf

#endif
