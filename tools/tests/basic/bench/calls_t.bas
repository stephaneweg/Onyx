' calls_t.bas -- calls.bas with the time of each part (TIMER: hundredths of a second)
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
SUB Nop
END SUB
t0# = TIMER
PRINT "fib"; Fib&(27);
PRINT INT((TIMER - t0#) * 1000); "ms": t0# = TIMER
DIM t AS DOUBLE, v AS DOUBLE
FOR i = 1 TO 300000
  v = i
  Accum t, v
NEXT
PRINT "accum"; t;
PRINT INT((TIMER - t0#) * 1000); "ms": t0# = TIMER
FOR i = 1 TO 300000
  Nop
NEXT
PRINT "nop";
PRINT INT((TIMER - t0#) * 1000); "ms": t0# = TIMER
s# = 0
FOR i = 1 TO 200000
  x# = i / 1000
  s# = s# + SIN(x#) * COS(x#) + ABS(x# - 50) + INT(x#) + SGN(x# - 100) + MIN(x#, 3)
NEXT
PRINT "math"; INT(s#);
PRINT INT((TIMER - t0#) * 1000); "ms": t0# = TIMER
FOR i = 1 TO 200000
  x# = i / 1000
  s# = s# + Dist#(x#, 2)
NEXT
PRINT "dist"; INT(s#);
PRINT INT((TIMER - t0#) * 1000); "ms"
