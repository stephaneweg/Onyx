PRINT SIN(1); COS(1); TAN(1); ATN(1) * 4
PRINT EXP(1); LOG(10); SQR(2); 2 ^ 0.5; 10 ^ -2
PRINT SIN(100); COS(-7.5); ATN(-20); EXP(-3); LOG(0.001)
' sieve
DIM p(10000)
c = 0
FOR i = 2 TO 10000
  IF p(i) = 0 THEN
    c = c + 1
    FOR j = i * i TO 10000 STEP i: p(j) = 1: NEXT
  END IF
NEXT
PRINT "primes"; c
s$ = ""
FOR i = 1 TO 1000: s$ = s$ + CHR$(65 + i MOD 26): NEXT
PRINT LEN(s$); MID$(s$, 500, 5)
RANDOMIZE 1: PRINT INT(RND * 100); INT(RND * 100)
