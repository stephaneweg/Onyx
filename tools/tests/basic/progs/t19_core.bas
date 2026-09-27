' CORE ON / OFF: on a host without app cores the program simply goes on where it is
PRINT "before"; CORE
CORE ON
s = 0
FOR i = 1 TO 1000: s = s + i: NEXT
PRINT "sum"; s; "core"; CORE
CORE OFF
PRINT "after"; CORE
