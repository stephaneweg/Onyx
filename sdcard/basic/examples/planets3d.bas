' planets3d.bas -- Onyx BASIC in 3D: a little solar system drawn by the GPU.
'
' The sun, four planets on their orbits, a moon (turning around its planet: PUSH3D / POP3D
' keep the planet's place), Saturn-like rings (a transparent disc: BLEND3D), a starry sky.
' The planets' textures are drawn with LINE / CIRCLE / PSET, then taken with GRAB3D.
'
'   Arrows: turn the camera    + / -: nearer / farther    Space: pause
'   F: full screen on / off    Esc: quit
'
' Shows: SCENE3D, CAMERA3D, LIGHT3D, TRANSLATE3D / ROTATE3D, PUSH3D / POP3D,
' COLOR3D, TEXTURE3D, BLEND3D, DEPTH3D, CULL3D, SPHERE3D, VERTEX3D (with texture), GRAB3D,
' RENDER3D, GPU3D.

SCREEN 12
RANDOMIZE TIMER

' ---- the textures (drawn in a corner of the screen, then grabbed) ----------------------------
' an Earth-like planet: sea, lands, ice caps
LINE (0, 0)-(127, 63), RGB(30, 70, 170), BF
FOR i = 1 TO 14
  CIRCLE (RND * 128, 12 + RND * 40), 4 + RND * 9, RGB(60, 150, 60)
  PAINT (0, 0), RGB(30, 70, 170), RGB(30, 70, 170)
NEXT
FOR i = 1 TO 14
  x = RND * 128: y = 12 + RND * 40: r = 3 + RND * 8
  FOR k = 0 TO r: CIRCLE (x, y), k, RGB(50 + RND * 40, 140 + RND * 40, 50): NEXT
NEXT
LINE (0, 0)-(127, 5), RGB(240, 245, 255), BF
LINE (0, 58)-(127, 63), RGB(240, 245, 255), BF
earth = GRAB3D(0, 0, 128, 64)

' a gas giant: bands
FOR y = 0 TO 63
  c = 150 + 60 * SIN(y / 3.5) + 20 * SIN(y * 1.7)
  LINE (0, y)-(127, y), RGB(c, c * 0.75, c * 0.45)
NEXT
CIRCLE (80, 40), 6, RGB(200, 90, 60): PAINT (80, 40), RGB(200, 90, 60), RGB(200, 90, 60)
giant = GRAB3D(0, 0, 128, 64)

' a rocky planet: craters
LINE (0, 0)-(63, 31), RGB(150, 110, 90), BF
FOR i = 1 TO 40: CIRCLE (RND * 64, RND * 32), 1 + RND * 3, RGB(110, 80, 65): NEXT
rock = GRAB3D(0, 0, 64, 32)

' the rings: bands of a transparent disc (magenta = the holes)
LINE (0, 0)-(63, 7), RGB(255, 0, 255), BF
FOR x = 0 TO 63
  IF (x MOD 9) > 2 THEN LINE (x, 0)-(x, 7), RGB(210 + (x MOD 5) * 8, 190, 150)
NEXT
rings = GRAB3D(0, 0, 64, 8)

' the stars
DIM sx(150), sy(150), sz(150)
FOR i = 1 TO 150
  a = RND * 6.2832: b = (RND - .5) * 3
  sx(i) = 60 * COS(a) * COS(b): sy(i) = 60 * SIN(b): sz(i) = 60 * SIN(a) * COS(b)
NEXT

' the ring's circle, once
DIM rc(48), rs(48)
FOR i = 0 TO 48: rc(i) = COS(i * 6.2832 / 48): rs(i) = SIN(i * 6.2832 / 48): NEXT

yaw = 20: pitch = 25: dist = 16: t = 0: paused = 0: fs = 0
t0 = TIMER: frames = 0: fps = 0

DO
  k$ = UCASE$(INKEY$)
  IF k$ = CHR$(27) THEN EXIT DO
  IF k$ = " " THEN paused = NOT paused
  IF k$ = "F" THEN fs = NOT fs: IF fs THEN FULLSCREEN ON ELSE FULLSCREEN OFF
  IF k$ = "+" OR k$ = "=" THEN dist = dist - 1: IF dist < 6 THEN dist = 6
  IF k$ = "-" THEN dist = dist + 1: IF dist > 40 THEN dist = 40
  IF KEYDOWN("LEFT") THEN yaw = yaw - 2
  IF KEYDOWN("RIGHT") THEN yaw = yaw + 2
  IF KEYDOWN("UP") THEN pitch = pitch + 1: IF pitch > 85 THEN pitch = 85
  IF KEYDOWN("DOWN") THEN pitch = pitch - 1: IF pitch < -85 THEN pitch = -85
  IF NOT paused THEN t = t + 1

  ' the camera on a sphere around the sun
  cy = dist * SIN(pitch * .01745): cr = dist * COS(pitch * .01745)
  CAMERA3D cr * SIN(yaw * .01745), cy, cr * COS(yaw * .01745), 0, 0, 0, 55
  SCENE3D RGB(4, 6, 16)

  ' the stars: tiny unlit triangles, no depth test
  LIGHT3D 0, 0, 0: DEPTH3D 0: CULL3D 0
  COLOR3D RGB(255, 255, 255)
  FOR i = 1 TO 150
    VERTEX3D sx(i), sy(i), sz(i): VERTEX3D sx(i) + .25, sy(i), sz(i): VERTEX3D sx(i), sy(i) + .25, sz(i)
  NEXT
  DEPTH3D 1: CULL3D 1

  ' the sun: unlit, bright
  COLOR3D RGB(255, 210, 90)
  PUSH3D: ROTATE3D 0, t * .3, 0: SPHERE3D 1.6, 32: POP3D

  ' the light comes from the sun... roughly: from above, it is a demo
  LIGHT3D .3, 1, .4, 18

  ' a rocky planet
  PUSH3D
    ROTATE3D 0, t * 1.6, 0: TRANSLATE3D 3.2, 0, 0: ROTATE3D 0, t * 2, 0
    COLOR3D RGB(255, 255, 255): TEXTURE3D rock, 1
    SPHERE3D .45, 16
  POP3D

  ' the Earth and its moon
  PUSH3D
    ROTATE3D 0, t * .9, 0: TRANSLATE3D 5.5, 0, 0
    PUSH3D
      ROTATE3D 23, t * 3, 0: TEXTURE3D earth, 1
      SPHERE3D .8, 32
    POP3D
    ROTATE3D 0, t * 4, 0: TRANSLATE3D 1.4, .2, 0
    TEXTURE3D 0: COLOR3D RGB(190, 190, 200)
    SPHERE3D .25, 12
  POP3D

  ' the gas giant, with its rings
  PUSH3D
    ROTATE3D 0, t * .4, 0: TRANSLATE3D -9, 0, 0: ROTATE3D 20, 0, 10
    PUSH3D: ROTATE3D 0, t * 1.5, 0: COLOR3D RGB(255, 255, 255): TEXTURE3D giant, 1: SPHERE3D 1.4, 32: POP3D
    ' the rings: a flat annulus of triangles (VERTEX3D x, y, z, s, t: s = across the
    ' bands of the texture, from the inner to the outer edge), see-through, both faces
    TEXTURE3D rings, 1: BLEND3D 1: CULL3D 0: DEPTH3D 1, 0: COLOR3D RGB(255, 255, 255), 220
    FOR i = 0 TO 47
      j = i + 1
      VERTEX3D 1.9 * rc(i), 0, 1.9 * rs(i), 0, 0: VERTEX3D 2.9 * rc(i), 0, 2.9 * rs(i), 1, 0: VERTEX3D 2.9 * rc(j), 0, 2.9 * rs(j), 1, 1
      VERTEX3D 1.9 * rc(i), 0, 1.9 * rs(i), 0, 0: VERTEX3D 2.9 * rc(j), 0, 2.9 * rs(j), 1, 1: VERTEX3D 1.9 * rc(j), 0, 1.9 * rs(j), 0, 1
    NEXT
    BLEND3D 0: CULL3D 1: DEPTH3D 1
  POP3D

  ' a small blue planet far out
  PUSH3D
    ROTATE3D 0, t * .25 + 90, 0: TRANSLATE3D 13, -.5, 0
    TEXTURE3D 0: COLOR3D RGB(90, 150, 255): SPHERE3D .6, 20
  POP3D

  RENDER3D

  frames = frames + 1
  IF TIMER - t0 >= 1 THEN fps = frames / (TIMER - t0): frames = 0: t0 = TIMER
  COLOR 15: LOCATE 1, 1
  IF GPU3D THEN PRINT "GPU"; ELSE PRINT "software";
  PRINT USING "   ##.# images/s"; fps;
  COLOR 7: LOCATE 30, 1: PRINT "Arrows turn  +/- zoom  Space pause  F full screen  Esc quit";
  PAUSE 1
LOOP
IF fs THEN FULLSCREEN OFF
