' kits: #import, a kit's functions called by their name, variables filled (BYREF), strings, callbacks, memory
#import TestKit

DECLARE FUNCTION Twice (i, user)
DECLARE SUB Heard (t$, n)
DECLARE SUB Structures ()

PRINT "add"; TestKit.add(2, 40)
PRINT "c name"; testkit.tk_add(1, 1)
PRINT "len"; TestKit.len("hello")
PRINT "null string"; TestKit.len(0)
PRINT "name ["; TestKit.name; "] ["; TestKit.name(); "]"
PRINT "half"; TestKit.half(5)
PRINT "scale"; TestKit.scale(1.5, 3)
PRINT "mix"; TestKit.mix(1, 2.5, 3, .25, "four")
PRINT "big"; TestKit.big; "neg"; TestKit.neg; "byte"; TestKit.byte; "null"; TestKit.null

' variables the function fills
i = 21: d# = 0: q# = 0: f = 0
TestKit.fill BYREF i, BYREF d#, BYREF q#, BYREF f
PRINT "fill"; i; d#; q#; f
DIM a(3)
a(2) = 5
TestKit.fill(BYREF a(2), BYREF d#, BYREF q#, BYREF f)
c# = ALLOC(4): POKEL c#, 7
TestKit.fill c#, BYREF d#, BYREF q#, BYREF f
PRINT "by pointer"; PEEKL(c#); d#
DEALLOC c#
PRINT "element"; a(2); d#

' a FUNCTION and a SUB the kit calls
total = 0
PRINT "each"; TestKit.each(4, ADDRESSOF(Twice), 100)
PRINT "total"; total
TestKit.say ADDRESSOF(Heard)

' memory: a text the kit made (ours to free), a buffer of ours
p# = TestKit.dup("abc")
PRINT "dup "; CSTR$(p#); " "; CSTR$(p#, 2); PEEKB(p# + 1)
TestKit.free p#
m# = ALLOC(32)
POKEB m#, 65: POKEW m# + 2, -2: POKEL m# + 4, 123456: POKEQ m# + 8, 9876543210#
POKEF m# + 16, 1.5: POKED m# + 24, 2.25
PRINT "peek"; PEEKB(m#); PEEKW(m# + 2); PEEKL(m# + 4); PEEKQ(m# + 8); PEEKF(m# + 16); PEEKD(m# + 24)
POKES m#, "text"
PRINT "pokes "; CSTR$(m#); TestKit.len(m#)
DEALLOC m#

IF TestKit.add(1, 2) = 3 THEN TestKit.say(ADDRESSOF(Heard)) ELSE PRINT "no"
Structures
ON ERROR GOTO failed
PRINT "each2"; TestKit.each(3, ADDRESSOF(Twice), -1)
PRINT "after"
END

failed:
PRINT "error"; ERR; "caught"
RESUME NEXT

FUNCTION Twice (i, user)
	SHARED total
	IF user < 0 AND i = 2 THEN ERROR 5
	total = total + i
	Twice = i * 2 + user
END FUNCTION

SUB Heard (t$, n)
	PRINT "heard "; t$; n; TestKit.add(n, n)
END SUB

' structures: a kit's are TYPEs of the program; a variable goes where the function takes a pointer
SUB Structures
	DIM it AS TestKit.item, pt AS TestKit.tk_point
	it.name = "abc": it.id = 5: it.weight = 1.25: it.at.x = 10: it.at.y = 20: it.flag = 1: it.level = 2
	PRINT "struct"; LEN(it); TestKit.item_next(it)
	PRINT "after ["; it.name; "]"; it.id; it.weight; it.at.x; it.at.y; it.flag; it.level; it.ratio
	it.name = "a name that is far too long"
	r = TestKit.item_next(it)
	PRINT "cut"; r; "["; it.name; "]"; it.id
	pt.x = 3: pt.y = 4
	PRINT "point"; TestKit.point_sum(pt); TestKit.point_sum(it.at); TestKit.point_sum(0)
	DIM many(2) AS TestKit.point
	many(1).x = 7: many(1).y = 8
	PRINT "element"; TestKit.point_sum(many(1))
	PRINT "array"; TestKit.points(many(), 3); many(0).x; many(1).y; many(2).x; TestKit.points(many, 3)
	' a structure the kit keeps: read from its address, written back
	k# = TestKit.item_kept
	DIM kept AS TestKit.item
	PEEKT k#, kept
	PRINT "kept ["; kept.name; "]"; kept.id; kept.weight; kept.at.x; kept.at.y; kept.ratio
	kept.name = "ours": kept.at.y = 40
	POKET k#, kept
	PRINT "poked "; CSTR$(k#); PEEKL(k# + 36)
	DIM other AS TestKit.item
	other = it: other.id = 1
	PRINT "copy"; it.id > 1
END SUB
