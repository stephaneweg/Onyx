' TYPE records, numeric types, DEF FN, MID$ statement, LSET / RSET, LEN
TYPE Point
  x AS INTEGER
  y AS INTEGER
END TYPE
TYPE Player
  nom AS STRING * 8
  pos AS Point
  score AS LONG
END TYPE
DIM p AS Player
p.nom = "Ada"
p.pos.x = 3: p.pos.y = 4
p.score = 100000
PRINT "["; p.nom; "]"; p.pos.x; p.pos.y; p.score; LEN(p); LEN(p.pos)
DIM q AS Player
q = p
q.pos.x = 99
PRINT p.pos.x; q.pos.x
DIM t(3) AS Point
FOR i = 0 TO 3: t(i).x = i * 10: t(i).y = -i: NEXT
PRINT t(2).x; t(3).y
Move p.pos, 5
PRINT p.pos.x; p.pos.y
SUB Move (pt AS Point, d)
  pt.x = pt.x + d
  pt.y = pt.y + d
END SUB
' integer types
a% = 7 / 2: b% = 5 / 2: c& = 100000
PRINT a%; b%; c&; 7 \ 2; CINT(-2.5)
DEFINT I-J
i = 3.7: j = 2.5
PRINT i; j
x# = 1 / 3#
PRINT x#; 1 / 3
PRINT 1234567.89#
' DEF FN
DEF FNsq (v) = v * v
DEF FNmax (a, b)
  IF a > b THEN FNmax = a ELSE FNmax = b
END DEF
PRINT FNsq(7); FNmax(3, 9)
' MID$ statement, LSET / RSET, fixed strings
s$ = "Hello World"
MID$(s$, 7, 5) = "Onyx!"
PRINT s$
DIM f AS STRING * 6
f = "abcdefgh": PRINT "["; f; "]"
w$ = SPACE$(6): LSET w$ = "ab": PRINT "["; w$; "]"
RSET w$ = "cd": PRINT "["; w$; "]"
PRINT LEN(a%); LEN(c&); LEN(x#); LEN(f)
