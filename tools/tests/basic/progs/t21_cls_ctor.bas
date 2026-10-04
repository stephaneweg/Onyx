' compile error: the parent's constructor has parameters
CLASS A
  n AS INTEGER
END CLASS
SUB A.new (n AS INTEGER)
  this.n = n
END SUB
CLASS B EXTENDS A
END CLASS
SUB B.new ()
  this.n = 2
END SUB
