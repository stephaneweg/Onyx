' compile error: redefining a virtual method needs OVERRIDE
CLASS A
END CLASS
VIRTUAL SUB A.Go ()
END SUB
CLASS B EXTENDS A
END CLASS
SUB B.Go ()
END SUB
