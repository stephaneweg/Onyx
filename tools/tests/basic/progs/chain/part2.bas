COMMON SHARED who$, pts, arr()
PRINT "part2:"; who$; pts; arr(1)
PRINT #1, "from part2"
CLOSE #1
OPEN "chain_t.txt" FOR INPUT AS #2
DO UNTIL EOF(2): LINE INPUT #2, l$: PRINT l$: LOOP
CLOSE #2
KILL "chain_t.txt"
