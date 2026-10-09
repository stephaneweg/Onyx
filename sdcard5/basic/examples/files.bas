' files.bas -- write a file, append to it, read it back.
OPEN "SD:/tmp/notes.txt" FOR OUTPUT AS #1
PRINT #1, "First line"
PRINT #1, "Second line"
CLOSE #1
OPEN "SD:/tmp/notes.txt" FOR APPEND AS #1
PRINT #1, "Added at "; TIME$
CLOSE #1
OPEN "SD:/tmp/notes.txt" FOR INPUT AS #1
DO UNTIL EOF(1)
  LINE INPUT #1, l$
  PRINT "> "; l$
LOOP
CLOSE #1
