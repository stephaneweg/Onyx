' RANDOM / BINARY files, GET / PUT / SEEK / LOC / LOF / EOF, FIELD / LSET / RSET, MKx$ / CVx,
' INPUT$, ENVIRON, FILES
TYPE Rec
  id AS INTEGER
  nom AS STRING * 6
  prix AS DOUBLE
END TYPE
DIM r AS Rec
OPEN "t12.dat" FOR RANDOM AS #1 LEN = LEN(r)
FOR i = 1 TO 3
  r.id = i * 10: r.nom = "item" + CHR$(48 + i): r.prix = i * 1.25
  PUT #1, i, r
NEXT
PRINT "LOF"; LOF(1); "LOC"; LOC(1)
GET #1, 2, r
PRINT r.id; "["; r.nom; "]"; r.prix
SEEK #1, 3: GET #1, , r: PRINT r.id; SEEK(1)
CLOSE #1
' FIELD on the same file
OPEN "t12.dat" FOR RANDOM AS #2 LEN = 16
FIELD #2, 2 AS id$, 6 AS nom$, 8 AS prix$
GET #2, 1
PRINT CVI(id$); "["; nom$; "]"; CVD(prix$)
LSET nom$ = "NEW": RSET id$ = MKI$(77)
PUT #2, 1
GET #2, 1: PRINT CVI(id$); "["; nom$; "]"
CLOSE #2
' BINARY
OPEN "t12.bin" FOR BINARY AS #3
a% = 513: b& = -2: s$ = "Onyx"
PUT #3, , a%: PUT #3, , b&: PUT #3, , s$
PRINT "pos"; LOC(3); "len"; LOF(3)
t$ = SPACE$(4): GET #3, 1, a%: GET #3, , b&: GET #3, , t$
PRINT a%; b&; t$
SEEK #3, 1: PRINT ASC(INPUT$(1, #3)); ASC(INPUT$(1, #3))
CLOSE
PRINT LEN(MKI$(1)); LEN(MKL$(1)); LEN(MKS$(1)); LEN(MKD$(1)); CVS(MKS$(1.5)); CVL(MKL$(-70000))
ENVIRON "GREETING=Hello"
PRINT ENVIRON$("greeting"); "|"; ENVIRON$(1)
FILES "*.BAS"
KILL "t12.dat": KILL "t12.bin"
