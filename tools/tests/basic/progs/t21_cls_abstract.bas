' compile error: a class with an abstract method cannot be created
CLASS Shape
  x AS SINGLE
END CLASS
ABSTRACT SUB Shape.Draw ()
CLASS Box EXTENDS Shape
END CLASS
DIM s AS Shape
s = NEW Box
