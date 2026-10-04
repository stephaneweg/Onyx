' classes.bas -- objects in Onyx BASIC: classes, inheritance, virtual methods, an interface.
' Shapes bounce in SCREEN 12; each one draws itself its own way. A key stops.

INTERFACE Scorable                ' what a class promises to have
  FUNCTION Points% ()
END INTERFACE

CLASS Sprite                      ' the parent: a place, a speed, a colour
  x AS SINGLE
  y AS SINGLE
  dx AS SINGLE
  dy AS SINGLE
  size AS SINGLE
  col AS INTEGER
END CLASS
SUB Sprite.new (size)
  this.size = size
  this.x = size + RND * (639 - 2 * size): this.y = 40 + size + RND * (439 - 2 * size)
  this.dx = RND * 6 - 3: this.dy = RND * 6 - 3
  this.col = 9 + INT(RND * 7)
END SUB
SUB Sprite.Move ()                ' the same for every sprite
  this.Draw 0
  this.x = this.x + this.dx: this.y = this.y + this.dy
  IF this.x < this.size OR this.x > 639 - this.size THEN this.dx = -this.dx: this.Bounce
  IF this.y < 40 + this.size OR this.y > 479 - this.size THEN this.dy = -this.dy: this.Bounce
  this.Draw this.col
END SUB
ABSTRACT SUB Sprite.Draw (c AS INTEGER)       ' each child draws itself
VIRTUAL SUB Sprite.Bounce ()                  ' a child may react to a wall
END SUB
VIRTUAL FUNCTION Sprite.Name$ ()
  RETURN "sprite"
END FUNCTION

CLASS Ball EXTENDS Sprite IMPLEMENTS Scorable
  hits AS INTEGER
END CLASS
OVERRIDE SUB Ball.Draw (c AS INTEGER)
  CIRCLE (this.x, this.y), this.size, c
END SUB
OVERRIDE SUB Ball.Bounce ()
  this.hits = this.hits + 1
END SUB
OVERRIDE FUNCTION Ball.Name$ ()
  RETURN "ball"
END FUNCTION
FUNCTION Ball.Points% ()
  RETURN this.hits
END FUNCTION

CLASS Box EXTENDS Sprite
END CLASS
OVERRIDE SUB Box.Draw (c AS INTEGER)
  LINE (this.x - this.size, this.y - this.size)-(this.x + this.size, this.y + this.size), c, B
END SUB
OVERRIDE FUNCTION Box.Name$ ()
  RETURN "box"
END FUNCTION

CLASS Star EXTENDS Box IMPLEMENTS Scorable    ' a box that draws its diagonals too
END CLASS
SUB Star.new (size)
  BASE.new size                   ' the parent's constructor
  this.col = 14
END SUB
OVERRIDE SUB Star.Draw (c AS INTEGER)
  BASE.Draw c                     ' the box, then the cross
  LINE (this.x - this.size, this.y - this.size)-(this.x + this.size, this.y + this.size), c
  LINE (this.x - this.size, this.y + this.size)-(this.x + this.size, this.y - this.size), c
END SUB
OVERRIDE FUNCTION Star.Name$ ()
  RETURN "star"
END FUNCTION
FUNCTION Star.Points% ()
  RETURN 5
END FUNCTION

CONST N = 12
DIM s(N - 1) AS Sprite            ' variables of the parent's type hold any child
SCREEN 12
RANDOMIZE TIMER
FOR i = 0 TO N - 1
  SELECT CASE i MOD 3
    CASE 0: s(i) = NEW Ball(8 + RND * 14)
    CASE 1: s(i) = NEW Box(8 + RND * 14)
    CASE 2: s(i) = NEW Star(10 + RND * 10)
  END SELECT
NEXT

DIM sc AS Scorable
DO
  total = 0: balls = 0
  FOR i = 0 TO N - 1
    s(i).Move                     ' Move calls the object's own Draw
    IF s(i) IS Ball THEN balls = balls + 1
    IF s(i) IS Scorable THEN      ' through the interface, whatever the class
      sc = s(i)
      total = total + sc.Points%
    END IF
  NEXT
  LOCATE 1, 2: PRINT N; "sprites,"; balls; "balls; points:"; total; "  (a key stops)   ";
  PAUSE 16
LOOP WHILE INKEY$ = ""
SCREEN 0
FOR i = 0 TO N - 1: PRINT s(i).Name$; " "; : NEXT
PRINT
