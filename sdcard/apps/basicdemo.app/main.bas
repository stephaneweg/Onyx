' BASIC Demo -- an Onyx app written in BASIC (SD:/apps/basicdemo.app/main.bas).
' The kernel sees main.bas instead of main and runs it with SD:/bin/basic.
' Open it in QBasic to read or change it.

DIM SHARED colorBox, sizeBar, fillBox, bar

WINDOW "BASIC Demo", 480, 360
COLOR 15, 1: CLS
DRAWTEXT 16, 12, "Hello from Onyx BASIC!", 14

r = LABEL(16, 46, 90, 22, "Your name:")
nameBox = TEXTBOX(110, 44, 200, 24, "")
greet = BUTTON(320, 42, 144, 28, "Say hello")

r = LABEL(16, 80, 90, 22, "Colour:")
colorBox = DROPDOWN(110, 78, 200, 24, "Yellow|Cyan|Green|Red|White")
counter = BUTTON(320, 76, 144, 28, "Clicks: 0")

r = LABEL(16, 114, 90, 22, "Size:")
sizeBar = SLIDER(110, 116, 200, 20, 90)
SETVALUE sizeBar, 60
fillBox = CHECKBOX(320, 112, 144, 24, "Filled", 1)

bar = PROGRESS(16, 330, 330, 16)
quit = BUTTON(364, 324, 100, 28, "Quit")

DrawArt
clicks = 0
DO
  e = WAITEVENT
  SELECT CASE e
    CASE -1, quit
      END
    CASE greet
      n$ = GETTEXT$(nameBox)
      IF n$ = "" THEN n$ = "stranger"
      NOTIFY "BASIC Demo", "Hello, " + n$ + "!"
    CASE counter
      clicks = clicks + 1
      SETTEXT counter, "Clicks:" + STR$(clicks)
      SETVALUE bar, (clicks * 10) MOD 110
    CASE colorBox, sizeBar, fillBox
      DrawArt
  END SELECT
LOOP

' The picture: concentric circles in the chosen colour and size.
SUB DrawArt
  LINE (16, 150)-(464, 316), 0, BF
  LINE (16, 150)-(464, 316), 7, B
  SELECT CASE VALUE(colorBox)
    CASE 0: c = 14
    CASE 1: c = 11
    CASE 2: c = 10
    CASE 3: c = 12
    CASE ELSE: c = 15
  END SELECT
  maxR = 4 + VALUE(sizeBar) * 72 \ 90
  FOR rad = maxR TO 4 STEP -6
    IF ((maxR - rad) \ 6) MOD 2 = 0 THEN k = c ELSE k = 1
    IF VALUE(fillBox) THEN
      CIRCLE (240, 233), rad, k, F
    ELSE
      CIRCLE (240, 233), rad, k
    END IF
  NEXT
  DRAWTEXT 24, 158, "radius" + STR$(maxR), 7
END SUB
