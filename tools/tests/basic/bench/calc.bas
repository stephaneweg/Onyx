' calc.bas -- pure computing, for basic -p and the native back end: a Mandelbrot set (floating point,
' loops), a sieve (integers, an array), a string loop, SUB calls.
DEFLNG I-N
t0 = TIMER
total = 0
FOR py = 0 TO 119
  FOR px = 0 TO 159
    x0 = px / 160 * 3.5 - 2.5: y0 = py / 120 * 2 - 1
    x = 0: y = 0: it = 0
    DO WHILE x * x + y * y <= 4 AND it < 100
      xt = x * x - y * y + x0
      y = 2 * x * y + y0
      x = xt
      it = it + 1
    LOOP
    total = total + it
  NEXT
NEXT
PRINT "mandelbrot"; total
DIM flags(20000) AS INTEGER
count = 0
FOR pass = 1 TO 10
  count = 0
  FOR i = 2 TO 20000: flags(i) = 1: NEXT
  FOR i = 2 TO 20000
    IF flags(i) THEN
      count = count + 1
      FOR k = i + i TO 20000 STEP i: flags(k) = 0: NEXT
    END IF
  NEXT
NEXT
PRINT "primes"; count
s$ = ""
FOR i = 1 TO 20000
  s$ = s$ + CHR$(65 + i MOD 26)
  IF LEN(s$) > 60 THEN s$ = MID$(s$, 30)
NEXT
PRINT "string "; LEN(s$)
FUNCTION Fib& (n AS LONG)
  IF n < 2 THEN Fib& = n ELSE Fib& = Fib&(n - 1) + Fib&(n - 2)
END FUNCTION
PRINT "fib"; Fib&(24)
