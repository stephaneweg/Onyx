' control flow
FOR i = 1 TO 3: PRINT i;: NEXT i: PRINT
FOR i = 10 TO 1 STEP -3: PRINT i;: NEXT: PRINT
FOR i = 1 TO 3
  FOR j = 1 TO 2
    PRINT i * 10 + j;
  NEXT j, i
PRINT
n = 0
WHILE n < 3: n = n + 1: WEND: PRINT "while"; n
DO: n = n - 1: LOOP UNTIL n = 0: PRINT "do until"; n
DO WHILE n < 5
  n = n + 2
  IF n = 4 THEN EXIT DO
LOOP
PRINT "exit do"; n
IF n = 4 THEN PRINT "single then" ELSE PRINT "single else"
IF n = 5 THEN PRINT "no" ELSE PRINT "else branch"
IF n > 10 THEN
  PRINT "big"
ELSEIF n > 3 THEN
  PRINT "medium"
ELSE
  PRINT "small"
END IF
FOR k = 1 TO 6
  SELECT CASE k
    CASE 1: PRINT "one";
    CASE 2, 3: PRINT "two-three";
    CASE 4 TO 5: PRINT "four-five";
    CASE ELSE: PRINT "other";
  END SELECT
  PRINT " ";
NEXT
PRINT
s$ = "b"
SELECT CASE s$
  CASE IS < "b": PRINT "less"
  CASE "b": PRINT "is b"
END SELECT
GOSUB sub1
PRINT "back"
c = 0
10 c = c + 1
IF c < 3 THEN GOTO 10
PRINT "goto loop"; c
ON 2 GOTO la, lb
la: PRINT "la"
lb: PRINT "lb"
FOR q = 1 TO 100
  IF q = 4 THEN EXIT FOR
NEXT
PRINT "exit for"; q
END
sub1:
  PRINT "in gosub"
  RETURN
