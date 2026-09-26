' graphics.bas -- SCREEN 12 (640 x 480, 16 colours): lines, boxes, circles.
SCREEN 12
FOR i = 0 TO 15
  LINE (i * 40, 0)-(i * 40 + 39, 30), i, BF
NEXT
FOR r = 10 TO 200 STEP 10
  CIRCLE (320, 260), r, 1 + (r \ 10) MOD 15
NEXT
FOR x = 0 TO 639 STEP 16
  LINE (x, 479)-(320, 260), 8
NEXT
LOCATE 29, 2: PRINT "Press a key";
SLEEP
