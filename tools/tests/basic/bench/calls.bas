' calls.bas -- SUB / FUNCTION calls and the numeric functions, for basic -p and the machine code:
' a recursive FUNCTION, a SUB with by-reference arguments, the math functions in a loop.
DEFLNG I-N
FUNCTION Fib& (n AS LONG)
  IF n < 2 THEN Fib& = n ELSE Fib& = Fib&(n - 1) + Fib&(n - 2)
END FUNCTION
SUB Accum (total AS DOUBLE, v AS DOUBLE)
  total = total + v
END SUB
FUNCTION Dist# (x AS DOUBLE, y AS DOUBLE)
  Dist# = SQR(x * x + y * y)
END FUNCTION
PRINT "fib"; Fib&(27)
DIM t AS DOUBLE, v AS DOUBLE
FOR i = 1 TO 300000
  v = i
  Accum t, v
NEXT
PRINT "accum"; t
s# = 0
FOR i = 1 TO 200000
  x# = i / 1000
  s# = s# + SIN(x#) * COS(x#) + ABS(x# - 50) + INT(x#) + SGN(x# - 100) + MIN(x#, 3) + Dist#(x#, 2)
NEXT
PRINT "math"; INT(s#)
