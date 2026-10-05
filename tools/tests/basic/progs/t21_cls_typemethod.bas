' compile error: a TYPE only holds data
TYPE Pt
  x AS SINGLE
END TYPE
SUB Pt.Move (dx)
  this.x = this.x + dx
END SUB
