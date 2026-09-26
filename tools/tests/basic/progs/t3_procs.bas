DECLARE SUB Swapper (a, b)
DECLARE FUNCTION Fact (n)
DIM SHARED total
x = 1: y = 2
Swapper x, y
PRINT "swapped"; x; y
CALL Swapper(x, (y))
PRINT "byval"; x; y
PRINT "fact"; Fact(10)
PRINT Greet$("Onyx")
AddTo 5: AddTo 7
PRINT "total"; total
DIM arr(5)
FOR i = 0 TO 5: arr(i) = i * i: NEXT
PRINT "sum"; SumArr(arr())
Counter: Counter
IF Seven = 7 THEN PRINT "seven"; Seven + 1
END

SUB Swapper (a, b)
  t = a: a = b: b = t
END SUB

FUNCTION Fact (n)
  IF n <= 1 THEN Fact = 1 ELSE Fact = n * Fact(n - 1)
END FUNCTION

FUNCTION Greet$ (who$)
  Greet$ = "Hello, " + who$ + "!"
END FUNCTION

SUB AddTo (v)
  total = total + v
END SUB

FUNCTION SumArr (a())
  s = 0
  FOR i = LBOUND(a) TO UBOUND(a): s = s + a(i): NEXT
  SumArr = s
END FUNCTION

SUB Counter
  STATIC calls
  calls = calls + 1
  PRINT "counter"; calls
END SUB
' a FUNCTION without parameters, called in an expression
FUNCTION Seven
  Seven = 7
END FUNCTION
