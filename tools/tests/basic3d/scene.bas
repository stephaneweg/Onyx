' The BASIC 3D test scene: a textured cube, a red sphere, a green cylinder on a grey floor,
' a half-transparent blue pane, then the same from the side (the camera moved).
SCREEN 12
' a texture: drawn with the usual graphics, then grabbed (magenta = transparent)
LINE (0, 0)-(63, 63), RGB(230, 200, 60), BF
LINE (0, 0)-(63, 63), RGB(40, 30, 10), B
LINE (8, 8)-(55, 55), RGB(120, 70, 20), BF
CIRCLE (32, 32), 12, RGB(255, 255, 255)
LINE (0, 0)-(63, 63), RGB(40, 30, 10)
t = GRAB3D(0, 0, 64, 64)
PRINT "texture"; t; "gpu"; GPU3D
FOR shot = 0 TO 1
  IF shot = 0 THEN CAMERA3D 0, 3, 7, 0, 0.5, 0, 50 ELSE CAMERA3D 7, 2, 0, 0, 0.5, 0, 50
  SCENE3D RGB(20, 24, 40)
  LIGHT3D 0.5, 1, 0.7, 25
  COLOR3D RGB(160, 160, 170)
  PLANE3D 12
  PUSH3D
    TRANSLATE3D -2, 0.75, 0: ROTATE3D 0, 30, 0
    COLOR3D RGB(255, 255, 255): TEXTURE3D t, 1
    CUBE3D 1.5
  POP3D
  TEXTURE3D 0
  PUSH3D
    TRANSLATE3D 0.3, 0.8, -0.5: COLOR3D RGB(220, 40, 40)
    SPHERE3D 0.8, 24
  POP3D
  PUSH3D
    TRANSLATE3D 2.2, 0, 0: COLOR3D RGB(40, 200, 80)
    CYLINDER3D 0.5, 1.8, 20
  POP3D
  ' a pane in front, drawn last: blended, not writing depth, both faces
  BLEND3D 1: DEPTH3D 1, 0: CULL3D 0
  COLOR3D RGB(80, 140, 255), 110
  VERTEX3D -1.5, 0, 1.8: VERTEX3D 1.5, 0, 1.8: VERTEX3D 1.5, 1.6, 1.8
  VERTEX3D -1.5, 0, 1.8: VERTEX3D 1.5, 1.6, 1.8: VERTEX3D -1.5, 1.6, 1.8
  RENDER3D
NEXT
