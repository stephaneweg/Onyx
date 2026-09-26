' ON TIMER / ON KEY: the test host's clock runs with the sleeps, t14_events.keys types keys
ON TIMER(1) GOSUB Tick
TIMER ON
ON KEY(11) GOSUB Up
KEY(11) ON
KEY 15, CHR$(0) + CHR$(30)
ON KEY(15) GOSUB KeyA
KEY(15) ON
FOR i = 1 TO 40
  PAUSE 100
NEXT
PRINT "nt"; nt; "ups"; ups; "a"; na; "left: "; INKEY$
TIMER OFF
END
Tick: nt = nt + 1: RETURN
Up: ups = ups + 1: RETURN
KeyA: na = na + 1: RETURN
