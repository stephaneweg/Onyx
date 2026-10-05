' Classes: references, inheritance, virtual methods, interfaces, IS, NOTHING, destructors
INTERFACE Drawable
  SUB Draw ()
  FUNCTION Area () AS SINGLE
END INTERFACE
INTERFACE Named
  FUNCTION Title$ ()
END INTERFACE

CLASS Shape IMPLEMENTS Named
  x AS SINGLE
  y AS SINGLE
  label AS STRING
END CLASS
SUB Shape.new (x, y)
  this.x = x: this.y = y: this.label = "shape"
END SUB
VIRTUAL SUB Shape.Describe ()
  PRINT "a shape at"; this.x; this.y
END SUB
VIRTUAL FUNCTION Shape.Title$ ()
  Shape.Title$ = this.label
END FUNCTION
ABSTRACT FUNCTION Shape.Sides% ()
SUB Shape.Move (dx, dy)
  this.x = this.x + dx: this.y = this.y + dy
END SUB
SUB Shape.Show ()                 ' not virtual, calls virtual ones
  PRINT this.Title$; ":"; this.Sides%; "sides, ";
  this.Describe
END SUB

CLASS Circle EXTENDS Shape IMPLEMENTS Drawable
  r AS SINGLE
END CLASS
SUB Circle.new (x, y, r)
  BASE.new x, y
  this.r = r: this.label = "circle"
END SUB
OVERRIDE SUB Circle.Describe ()
  PRINT "a circle of radius"; this.r; "- ";
  BASE.Describe
END SUB
OVERRIDE FUNCTION Circle.Sides% ()
  RETURN 0
END FUNCTION
SUB Circle.Draw ()
  PRINT "draw circle"; this.r
END SUB
FUNCTION Circle.Area () AS SINGLE
  RETURN INT(3.14159 * this.r * this.r)
END FUNCTION

CLASS Square EXTENDS Shape IMPLEMENTS Drawable
  side AS SINGLE
END CLASS
SUB Square.new (s)
  BASE.new (0, 0)
  this.side = s: this.label = "square"
END SUB
OVERRIDE FUNCTION Square.Sides% ()
  Square.Sides% = 4
END FUNCTION
VIRTUAL SUB Square.Draw ()
  PRINT "draw square"; this.side
END SUB
FUNCTION Square.Area () AS SINGLE
  RETURN this.side * this.side
END FUNCTION

CLASS BigSquare EXTENDS Square
END CLASS
OVERRIDE SUB BigSquare.Draw ()
  PRINT "BIG ";
  BASE.Draw
END SUB
OVERRIDE FUNCTION BigSquare.Title$ ()
  RETURN "big " + BASE.Title$()
END FUNCTION

' --- polymorphism through a parent's variable
DIM s AS Shape
PRINT s IS NOTHING
s = NEW Circle(1, 2, 3)
s.Show
s.Move 10, 10
s.Describe
PRINT s IS Circle; s IS Shape; s IS Square; s IS Drawable; s IS Named; s IS NOTHING

' --- references: two variables, one object
DIM c AS Circle
c = s                             ' checked when it runs
c.r = 7
s.Describe
DIM s2 AS Shape
s2 = s
PRINT s2 IS s; s2 IS c
s2 = NEW Square(5)
PRINT s2 IS s; NOT s2 IS NOTHING

' --- an array of objects, interfaces
DIM list(3) AS Shape
list(0) = c
list(1) = s2
list(2) = NEW BigSquare(9)
FOR i = 0 TO 3
  IF list(i) IS NOTHING THEN
    PRINT i; "nothing"
  ELSE
    list(i).Show
    DIM d AS Drawable
    d = list(i)
    d.Draw
    PRINT "area"; d.Area()
  END IF
NEXT
DIM n AS Named
n = list(2)
PRINT n.Title$

' --- objects as arguments and results
SUB Twice (d AS Drawable)
  d.Draw: d.Draw
END SUB
FUNCTION Biggest (a AS Shape, b AS Shape) AS Shape
  DIM da AS Drawable, db AS Drawable
  da = a: db = b
  IF da.Area() > db.Area() THEN RETURN a
  Biggest = b
END FUNCTION
Twice c
Twice NEW Square(2)
PRINT Biggest(list(0), list(2)).Title$
DIM big AS Shape
big = Biggest(list(0), list(1))
PRINT big.Title$
SUB Forget (p AS Shape)
  p = NOTHING                     ' the parameter only
END SUB
Forget big
PRINT big IS NOTHING

' --- a class's fields that are objects: a linked list
CLASS Node
  value AS INTEGER
  nxt AS Node
END CLASS
SUB Node.new (v AS INTEGER, n AS Node)
  this.value = v: this.nxt = n
END SUB
DIM head AS Node
FOR i = 1 TO 4: head = NEW Node(i * i, head): NEXT
DIM p AS Node
p = head
DO UNTIL p IS NOTHING
  PRINT p.value;
  p = p.nxt
LOOP
PRINT
PRINT head.nxt.nxt.value
head.nxt.nxt.value = 99
PRINT head.nxt.nxt.value

' --- a TYPE (a value) holding an object: the copy shares the object
TYPE Slot
  id AS INTEGER
  item AS Shape
END TYPE
DIM a AS Slot, b AS Slot
a.id = 1: a.item = c
b = a
b.id = 2: b.item.x = 500
PRINT a.id; b.id; a.item.x; c.x

' --- constructors: a parent's without parameters is called first; DIM v AS Class (args)
CLASS Animal
  legs AS INTEGER
  sound AS STRING
END CLASS
SUB Animal.new ()
  this.legs = 4: this.sound = "..."
  PRINT "(animal made)"
END SUB
SUB Animal.delete ()
  PRINT "(animal gone: "; this.sound; ")"
END SUB
CLASS Dog EXTENDS Animal
END CLASS
SUB Dog.new ()
  this.sound = "woof"
END SUB
SUB Dog.delete ()
  PRINT "(dog gone)"
END SUB
CLASS Puppy EXTENDS Dog
END CLASS
DIM dg AS Dog()
PRINT dg.legs; dg.sound
DIM pup AS Animal
pup = NEW Puppy                   ' Dog's constructor (inherited), then Animal's first
PRINT pup.sound; pup IS Dog

' --- destructors: when the last reference goes
PRINT "drop dog"
dg = NOTHING
PRINT "dropped"
DIM other AS Animal
other = pup
pup = NOTHING
PRINT "still one reference"
other = NEW Animal
PRINT "end of destructors"
SUB Scope ()
  DIM local AS Dog()
  PRINT "in scope"
END SUB
Scope
PRINT "after scope"

' --- errors when it runs
ON ERROR GOTO oops
DIM sq AS Square
sq = list(0)                      ' a Circle is not a Square
PRINT "not reached"
after1:
DIM none AS Shape
none.Describe
after2:
PRINT none.x
after3:
PRINT "done"
END
oops:
PRINT "error: "; ERR
k = k + 1
IF k = 1 THEN RESUME after1
IF k = 2 THEN RESUME after2
RESUME after3
