' subs.bas -- SUBs and FUNCTIONs (open it in QBasic: View > SUBs...).
PRINT "Fibonacci:"
FOR i = 1 TO 12: PRINT Fib(i);: NEXT
PRINT
a = 3: b = 7
PRINT "Before:"; a; b
SwapThem a, b
PRINT "After: "; a; b
PRINT Reverse$("Onyx BASIC")

FUNCTION Fib (n)
  IF n <= 2 THEN Fib = 1 ELSE Fib = Fib(n - 1) + Fib(n - 2)
END FUNCTION

SUB SwapThem (x, y)
  ' arguments are passed by reference: the caller's variables change
  t = x: x = y: y = t
END SUB

FUNCTION Reverse$ (s$)
  r$ = ""
  FOR i = LEN(s$) TO 1 STEP -1: r$ = r$ + MID$(s$, i, 1): NEXT
  Reverse$ = r$
END FUNCTION
