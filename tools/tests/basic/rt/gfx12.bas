' SCREEN 12, VIEW / WINDOW, LINE styles, PSET STEP, VIEW PRINT
SCREEN 12
VIEW (20, 20)-(619, 459), 1, 15
WINDOW (-10, -1.5)-(10, 1.5)
LINE (-10, 0)-(10, 0), 7, , &HAAAA
LINE (0, -1.5)-(0, 1.5), 7, , &HAAAA
PSET (-10, SIN(-10)), 14
FOR x = -10 TO 10 STEP .05: LINE -(x, SIN(x) * COS(x / 3)), 14: NEXT
WINDOW: VIEW
VIEW PRINT 1 TO 3
PRINT "line 1": PRINT "line 2": PRINT "line 3": PRINT "line 4 (scrolled in the view)"
