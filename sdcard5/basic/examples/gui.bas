' gui.bas -- a small window with controls (WINDOW, BUTTON, TEXTBOX, WAITEVENT).
WINDOW "Converter", 320, 150
r = LABEL(12, 16, 80, 22, "Celsius:")
c = TEXTBOX(100, 14, 120, 24, "20")
go = BUTTON(230, 12, 78, 28, "Convert")
res = LABEL(12, 60, 296, 22, "")
DO
  e = WAITEVENT
  IF e = -1 THEN END
  IF e = go OR e = c THEN
    f = VAL(GETTEXT$(c)) * 9 / 5 + 32
    SETTEXT res, GETTEXT$(c) + " C =" + STR$(f) + " F"
  END IF
LOOP
