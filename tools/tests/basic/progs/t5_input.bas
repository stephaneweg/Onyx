INPUT "Your name"; n$
INPUT "Two numbers", a, b
LINE INPUT "Say something: "; s$
PRINT "Hi "; n$; a + b; s$
WINDOW "Demo", 300, 200
b = BUTTON(10, 10, 80, 24, "OK")
t = TEXTBOX(10, 40, 100, 24, "")
SETTEXT t, "x"
e = WAITEVENT
PRINT "event"; e
PSET (1, 2), 3
LINE (0, 0)-(10, 10), 4, BF
LINE -(20, 5)
CIRCLE (50, 50), 10, 2
NOTIFY "Title", "Message"
PRINT COMMAND$
