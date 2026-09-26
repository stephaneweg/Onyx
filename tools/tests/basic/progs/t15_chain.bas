' CHAIN with COMMON, RUN label, CLEAR, TRON / TROFF, FRE
COMMON SHARED nom$, score, t()
DIM t(2)
IF runs = 0 THEN PRINT "first run" ELSE PRINT "run"; runs
runs = runs + 1
IF runs = 1 THEN RUN Again
Again:
PRINT "runs after RUN label:"; runs
x = 5: CLEAR: PRINT "after CLEAR x ="; x
PRINT FRE("") > 0; FRE(-1) > 0
TRON
a = 1
b = 2
TROFF
PRINT
nom$ = "Ada": score = 42: t(1) = 7
OPEN "chain_t.txt" FOR OUTPUT AS #1
PRINT #1, "kept open"
CHAIN "chain/part2"
PRINT "not reached"
