' USB gamepads: PAD, STICK, STRIG (the test host: pad 0 holds A + right on every other read)
b = PAD(0)
PRINT "pad"; b; (b AND 16) <> 0; (b AND 8) <> 0
PRINT "pad again"; PAD
PRINT "pad 1"; PAD(1)
PRINT "stick"; STICK(0); STICK(1); STICK(2); STICK(3)
PRINT "strig"; STRIG(1); STRIG(0); STRIG(0); STRIG(0); STRIG(5)
ON ERROR GOTO bad
PRINT STICK(4)
END
bad:
PRINT "error"; ERR
RESUME NEXT
