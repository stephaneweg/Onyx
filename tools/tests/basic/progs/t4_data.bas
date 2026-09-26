DIM names$(3), scores(3)
FOR i = 1 TO 3: READ names$(i), scores(i): NEXT
FOR i = 1 TO 3: PRINT names$(i); scores(i): NEXT
RESTORE more
READ a$, b
PRINT a$; b
DATA "Alice", 90, Bob, 85
DATA "Carol, jr", 77
more:
DATA hello, 42
DIM m(2, 3)
m(2, 3) = 9: PRINT m(2, 3); m(0, 0)
DIM r(1 TO 3) AS STRING
r(3) = "three": PRINT r(3)
CONST PI = 3.14159, NAME$ = "circle"
PRINT NAME$; PI * 2
SWAP names$(1), names$(2): PRINT names$(1); names$(2)
p = 1: q = 2: SWAP p, q: PRINT p; q
OPEN "test_out.txt" FOR OUTPUT AS #1
PRINT #1, "line one"
PRINT #1, 1; 2; 3
WRITE #1, "w", 5
CLOSE #1
OPEN "test_out.txt" FOR APPEND AS #2
PRINT #2, "appended"
CLOSE
OPEN "test_out.txt" FOR INPUT AS #1
DO UNTIL EOF(1)
  LINE INPUT #1, l$
  PRINT "> "; l$
LOOP
CLOSE #1
OPEN "test_out.txt" FOR INPUT AS #1
LINE INPUT #1, skip$: INPUT #1, n1, n2
PRINT "fields"; n1; n2
CLOSE
KILL "test_out.txt"
PRINT FILEEXISTS("test_out.txt")
PRINT DATE$; " "; TIME$; TIMER
