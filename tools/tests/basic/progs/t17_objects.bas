' Methods and constructors in TYPEs (FreeBASIC-like): this, RETURN, DIM ... AS T(args), NEW
TYPE MonObj
  x AS INTEGER
  y AS INTEGER
  test AS SUB (a AS INTEGER, b AS INTEGER)
  test2 AS FUNCTION (a AS INTEGER) AS INTEGER
END TYPE

TYPE Pt
  x AS SINGLE
  y AS SINGLE
END TYPE
TYPE Shape
  name AS STRING
  pos AS Pt
END TYPE

SUB MonObj.new (v AS INTEGER)
  this.x = 0
  this.y = v
END SUB
SUB MonObj.Test (a AS INTEGER, b AS INTEGER)
  this.x = a + b
END SUB
FUNCTION MonObj.Test2 (a AS INTEGER) AS INTEGER
  RETURN this.x + a
END FUNCTION
FUNCTION MonObj.Sum% ()
  MonObj.Sum% = this.x + this.y + this.Test2(0)
END FUNCTION

SUB Pt.Move (dx, dy)
  this.x = this.x + dx: this.y = this.y + dy
END SUB
FUNCTION Pt.Text$ ()
  Pt.Text$ = "(" + LTRIM$(STR$(this.x)) + "," + LTRIM$(STR$(this.y)) + ")"
END FUNCTION
SUB Shape.new (n$, x, y)
  this.name = n$: this.pos.x = x: this.pos.y = y
END SUB
SUB Shape.Show ()
  PRINT this.name; " at "; this.pos.Text$
END SUB

DIM a AS MonObj(7)            ' a constructor
PRINT a.x; a.y
a.Test 2, 3
PRINT a.x, a.Test2(10)
DIM b AS MonObj               ' no parentheses: no constructor
PRINT b.x; b.y
b = NEW MonObj(42)
PRINT b.y; b.Sum%
CALL b.Test(1, 1)
PRINT b.x

DIM s AS Shape("box", 1, 2)
s.Show
s.pos.Move 10, 20
s.Show
DIM list(2) AS Pt
list(1).Move 5, 6
PRINT list(1).Text$; " "; list(0).Text$
FOR i = 0 TO 2: list(i).Move i, i: NEXT
PRINT list(2).Text$
Bump b
PRINT b.x
SUB Bump (o AS MonObj)
  o.Test 4, 4
END SUB
