//
// uikit/flat.h -- UIKit for a program that is not C++: a window and its widgets as handles, plain C
// functions on them. It is what Onyx BASIC calls (#import uikit: UIKit.window, UIKit.button ...; the code
// QBStudio makes of a form), and what a C program may call.
//
//   void *win = uk_window ("Hello", 320, 120, 0);
//   uk_label (win, 12, 12, 200, 20, "Your name:");
//   void *name = uk_textbox (win, 12, 36, 200, 24, "", 0);
//   uk_button (win, 220, 36, 80, 24, "OK", on_ok);          // void on_ok (void *button)
//   uk_window_run (win);                                     // until the window is closed
//
// A handle is a pointer the program keeps and gives back; 0 is "none" (a function given 0 does nothing).
// A program has one window. The widgets belong to their window and end with it. A text is Latin-1. A
// callback is called from uk_window_run (or uk_window_step), on the program's own thread.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _uikit_flat_h
#define _uikit_flat_h

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*uk_event) (void *widget);		// a widget's event: the widget it comes from
typedef void (*uk_resized) (void *window, int w, int h);
typedef void (*uk_chosen) (void);			// a menu item chosen

// ---- the window ---------------------------------------------------------------------------------------
enum { UK_FLAT_RESIZABLE = 1 };
void *uk_window (const char *title, int w, int h, int flags);	// the program's window (w x h: its inside); 0: none
void uk_window_run (void *window);			// the events, until the window is closed (its box, uk_window_close)
int  uk_window_step (void *window);			// ... one round of them, for a program with its own loop: 0 once closed
int  uk_window_wait (void *window);			// ... one round and the wait before the next (16 ms): WHILE uk_window_wait (w) ... WEND
void uk_window_close (void *window);			// uk_window_run returns, uk_window_step gives 0
int  uk_window_width (void *window);
int  uk_window_height (void *window);
void uk_window_min_size (void *window, int w, int h);	// the smallest a resizable window goes
void uk_window_on_resize (void *window, uk_resized fn);	// after the user resized it: place the widgets again

// ---- the widgets (x, y, w, h: their place in the window) ---------------------------------------------------
void *uk_label (void *window, int x, int y, int w, int h, const char *text);
void *uk_button (void *window, int x, int y, int w, int h, const char *text, uk_event on_click);
void *uk_textbox (void *window, int x, int y, int w, int h, const char *text, uk_event on_change);
void *uk_checkbox (void *window, int x, int y, int w, int h, const char *text, int checked, uk_event on_click);
void *uk_listbox (void *window, int x, int y, int w, int h, const char *items, uk_event on_change);	// items: "one|two|three"
void *uk_dropdown (void *window, int x, int y, int w, int h, const char *items, uk_event on_change);
void *uk_slider (void *window, int x, int y, int w, int h, int max, int value, uk_event on_change);	// from 0 to max
void *uk_progress (void *window, int x, int y, int w, int h, int max, int value);
void uk_set_range (void *widget, int min, int max);	// a slider, a progress bar: its ends

// A widget's text: a label's, a button's, a checkbox's, what a text box holds, the chosen item of a list
// or a drop-down. uk_get_text's result is good until the next call of UIKit.
void uk_set_text (void *widget, const char *text);
const char *uk_get_text (void *widget);
// Its number: a checkbox's state (1 / 0), the chosen item's place in a list or a drop-down (from 0; -1:
// none), a slider's or a progress bar's value.
void uk_set_value (void *widget, int value);
int  uk_get_value (void *widget);
void uk_add_item (void *widget, const char *text);	// a list: one more item
void uk_clear_items (void *widget);			// a list: emptied
int  uk_item_count (void *widget);			// a list, a drop-down
void uk_move (void *widget, int x, int y, int w, int h);
void uk_show (void *widget, int on);
void uk_enable (void *widget, int on);
int  uk_shown (void *widget);
int  uk_enabled (void *widget);
void uk_focus (void *widget);

// ---- the menus (in the screen's menu bar while the window is the active one) --------------------------------
// One more item in the menu `title` (made at its first item); item "-": a separator; key: "Ctrl+S" or "".
void uk_menu_item (void *window, const char *title, const char *item, const char *key, uk_chosen on_choose);

// ---- the dialogs -------------------------------------------------------------------------------------------
int uk_message (const char *title, const char *text, int buttons);	// buttons 0 OK, 1 OK Cancel, 2 Yes No, 3 Yes No Cancel -> 1 OK / Yes, 0 Cancel / No (3: 2 for No)
const char *uk_ask_open (const char *folder);		// the file chosen ("": none); good until the next call
const char *uk_ask_save (const char *folder, const char *name);

#ifdef __cplusplus
}
#endif

#endif
