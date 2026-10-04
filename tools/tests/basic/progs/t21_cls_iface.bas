' compile error: an interface's method is missing
INTERFACE Drawable
  SUB Draw ()
  FUNCTION Area () AS SINGLE
END INTERFACE
CLASS Box IMPLEMENTS Drawable
END CLASS
SUB Box.Draw ()
END SUB
