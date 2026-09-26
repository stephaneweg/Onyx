' basics: arithmetic, strings, PRINT formatting
PRINT "Hello, Onyx!"
a = 5: b = 3
PRINT a + b, a - b, a * b, a / b
PRINT 7 \ 2; 7 MOD 3; 2 ^ 10; -2 ^ 2
PRINT 1 / 3; 0.5; 123456789; 1E+20; .001
x$ = "abc" + "DEF"
PRINT x$; LEN(x$); UCASE$(x$); LCASE$(x$)
PRINT LEFT$(x$, 2); RIGHT$(x$, 2); MID$(x$, 2, 3); MID$(x$, 4)
PRINT INSTR(x$, "cD"); INSTR(3, "hello world", "o")
PRINT STR$(42); STR$(-3.5); VAL("  12.5xyz"); VAL("&HFF")
PRINT CHR$(65); ASC("a"); SPACE$(3); "|"; STRING$(4, "*"); STRING$(3, 45)
PRINT LTRIM$("  x  "); "|"; RTRIM$("  x  "); "|"
PRINT HEX$(255); " "; OCT$(8); " "; HEX$(-1)
PRINT INT(-2.5); FIX(-2.5); CINT(2.5); ABS(-4); SGN(-9); SQR(16)
PRINT 1 < 2; 1 > 2; "a" < "b"; NOT 0; 5 AND 3; 5 OR 3; 5 XOR 3
PRINT "tab"; TAB(10); "x"; SPC(3); "y"
PRINT "zone1", "zone2", "z3"
PRINT "no newline";
PRINT " -- continued"
? "question mark print"
