' ON ERROR GOTO / RESUME / RESUME NEXT / ERR / ERL / ERROR
ON ERROR GOTO Handler
PRINT "a"
x = 1 / 0
PRINT "after 1/0"
ERROR 53
PRINT "after ERROR 53"
y = SQR(-1)
PRINT "after SQR"
CALL Deep
PRINT "after Deep"
PRINT "half:"; 1 + Half(0)
tries = 0
Again:
tries = tries + 1
IF tries < 3 THEN ERROR 200
PRINT "tries"; tries
ON ERROR GOTO 0
PRINT "done"
END
Handler:
PRINT "error"; ERR; "at"; ERL
IF ERR = 200 THEN RESUME Again
RESUME NEXT
SUB Deep
  DIM a(3)
  a(10) = 1
  PRINT "resumed in Deep"
END SUB
FUNCTION Half (v)
  Half = 100 / v
  PRINT "resumed in Half"
END FUNCTION
