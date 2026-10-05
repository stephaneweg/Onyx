' uikit.bas -- a desktop app made with UIKit's widgets, called by name.
'
' #import UIKit gives the program the system's toolkit: a window, its widgets, its menus. A widget
' is a handle (a number); what happens to it calls a SUB of the program (ADDRESSOF). The reference:
' the document 11 (UIKit), "uikit/flat.h". QBStudio draws such a window and writes this code for you.
'
#import UIKit

DECLARE SUB Greet (widget)
DECLARE SUB Moved (widget)
DECLARE SUB Sized (wnd, w, h)
DECLARE SUB About ()
DECLARE SUB Quit ()

DIM SHARED win, nameBox, greetBtn, greeting, list, level, gauge

win = UIKit.window("UIKit from BASIC", 360, 300, 1)	' 1: the window can be resized
IF win = 0 THEN END
UIKit.window_min_size win, 300, 240

greeting = UIKit.label(win, 12, 12, 336, 20, "Your name:")
nameBox = UIKit.textbox(win, 12, 38, 236, 26, "", 0)
greetBtn = UIKit.button(win, 256, 38, 92, 26, "Greet", ADDRESSOF(Greet))
list = UIKit.listbox(win, 12, 74, 336, 140, "", 0)
level = UIKit.slider(win, 12, 226, 336, 22, 100, 30, ADDRESSOF(Moved))
gauge = UIKit.progress(win, 12, 262, 336, 16, 100, 30)

UIKit.menu_item win, "App", "About...", "", ADDRESSOF(About)
UIKit.menu_item win, "App", "-", "", 0
UIKit.menu_item win, "App", "Quit", "Ctrl+Q", ADDRESSOF(Quit)
UIKit.window_on_resize win, ADDRESSOF(Sized)
UIKit.focus nameBox

' the events, until the window is closed: each round calls the SUBs of what happened
DO WHILE UIKit.window_wait(win)
LOOP
END

SUB Greet (widget)
	n$ = UIKit.get_text(nameBox)
	IF n$ = "" THEN n$ = "you"
	UIKit.set_text greeting, "Hello, " + n$ + "!"
	UIKit.add_item list, "Greeted " + n$ + " at " + TIME$
	UIKit.set_text nameBox, ""
	UIKit.focus nameBox
END SUB

SUB Moved (widget)
	UIKit.set_value gauge, UIKit.get_value(widget)
END SUB

' the window was resized: the widgets follow its width, the list its height
SUB Sized (wnd, w, h)
	UIKit.move greeting, 12, 12, w - 24, 20
	UIKit.move nameBox, 12, 38, w - 124, 26
	UIKit.move greetBtn, w - 104, 38, 92, 26
	UIKit.move list, 12, 74, w - 24, h - 160
	UIKit.move level, 12, h - 74, w - 24, 22
	UIKit.move gauge, 12, h - 38, w - 24, 16
END SUB

SUB About ()
	r = UIKit.message("About", "A BASIC program on UIKit's widgets.", 0)
END SUB

SUB Quit ()
	UIKit.window_close win
END SUB
