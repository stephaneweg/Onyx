' Graphics statements (the test host logs the drawing calls)
SCREEN 13
PSET (10, 10), 4
PSET STEP (5, 5), 2
LINE -(30, 30), 1
LINE (0, 0)-STEP(10, 5), 3, B
LINE (1, 1)-(5, 5), 7, , &HF0F0
PAINT (20, 20), 5, 1
DRAW "C3 BM2,2 R4 D2 NL3 U1"
CIRCLE (32, 32), 10, 2, 0, 1.5708
VIEW (10, 10)-(49, 49), 0, 15
PSET (0, 0), 9
WINDOW (0, 0)-(100, 100)
PSET (50, 50), 9
PRINT PMAP(50, 0); PMAP(50, 1); PMAP(20, 2); POINT(2)
WINDOW
VIEW
' GET / PUT through the host's 64 x 64 pixel store
PSET (1, 1), 12: PSET (2, 1), 13: PSET (1, 2), 14
DIM spr(10)
GET (1, 1)-(2, 2), spr
PRINT spr(0); spr(1)
PUT (40, 40), spr, PSET
PRINT POINT(40, 40); POINT(41, 40); POINT(40, 41); POINT(41, 41)
PUT (40, 40), spr, XOR
PRINT POINT(40, 40); POINT(41, 40)
PALETTE 1, 63: PALETTE
PCOPY 1, 0
SCREEN , , 1, 0
