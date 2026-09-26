' arkanoid.bas -- Onyx Arkanoid: a brick breaker in SCREEN 13 (320 x 200, 256 colours),
' scaled to the whole display with FULLSCREEN (F: back to a window, and again).
'
'   Left / Right or the mouse ... move the paddle
'   Space or a click ............ launch the ball (and fire, with the laser)
'   P pause    F full screen on / off    Esc back to the title (Esc there: quit)
'
' Capsules fall out of some bricks: catch them with the paddle.
'   E expand   S slow   C catch   L laser   D disruption (3 balls)   P one more life
'
' Shows: FULLSCREEN, KEYDOWN, MOUSEX / MOUSEB, GET / PUT sprites, PLAY "MB" (the sound
' effects play in the background), ON ERROR, levels in DATA, SUBs and FUNCTIONs working on
' SHARED arrays.

CONST LEFTX = 8, RIGHTX = 215, TOPY = 8, BOTY = 199     ' the playfield, inside the walls
CONST NC = 13, NR = 12, BW = 16, BH = 8, BY0 = 24        ' the brick grid
CONST PY = 184, BS = 4, MAXB = 2, MAXS = 5, NLEVELS = 5
CONST SPR = 70                                           ' words for one 16 x 8 sprite (GET)

DIM SHARED br(NC - 1, NR - 1), hp(NC - 1, NR - 1), nleft
DIM SHARED bx(MAXB), by(MAXB), dx(MAXB), dy(MAXB), bon(MAXB), stuck(MAXB), soff(MAXB)
DIM SHARED obx(MAXB), oby(MAXB), bdrawn(MAXB), nballs
DIM SHARED px, pw, opx, opw, speed, catchOn, laserOn, fireWait
DIM SHARED capOn, capT, capX, capY, ocapY, capDrawn, capSpr(6 * SPR)
DIM SHARED shX(MAXS), shY(MAXS), shOn(MAXS), oshY(MAXS), shDrawn(MAXS)
DIM SHARED score, hiscore, lives, lvl, sndOn, nextT, fs, sfx$
DIM SHARED hue(7), shade(7), tx(1500), ty(1500), ntl, tw, th

RESTORE Colours
FOR i = 1 TO 7: READ hue(i): NEXT
FOR i = 0 TO 7: READ shade(i): NEXT

SCREEN 13
fs = -1: FULLSCREEN
RANDOMIZE TIMER

' The sound output may be busy (another program plays): then the game is silent.
sndOn = -1
ON ERROR GOTO NoSound
NOTEON 15, 100, 1, 0: NOTEOFF 15
ON ERROR GOTO 0

MakeSprites
DO WHILE TitleScreen
  PlayGame
LOOP
END

NoSound:
  sndOn = 0
  RESUME NEXT

Colours:     ' brick hues 1..7 in the VGA palette, then the grey shades of the walls
DATA 8, 10, 12, 16, 20, 0, 4
DATA 20, 24, 28, 31, 27, 23, 19, 17

Letters:     ' the capsule letters, 3 x 5 pixels: E S C L D P
DATA "111100110100111", "111100111001111", "111100100100111"
DATA "100100100100111", "110101101101110", "111101111100100"

Levels:      ' rows of 13: 1-8 colours, S silver (several hits), G gold (never breaks)
DATA 8
DATA ".............", ".............", "SSSSSSSSSSSSS", "1111111111111"
DATA "3333333333333", "6666666666666", "7777777777777", "4444444444444"
DATA 12
DATA "1............", "12...........", "123..........", "1234........."
DATA "12345........", "123456.......", "1234567......", "12345671....."
DATA "123456712....", "1234567123...", "12345671234..", "SSSSSSSSSSSS4"
DATA 10
DATA ".............", ".............", "..4.......4..", "...4.....4..."
DATA "..444444444..", ".44144444144.", "4444444444444", "4.444444444.4"
DATA "4.4.......4.4", "...44...44..."
DATA 12
DATA ".............", "8888888888888", ".............", "111GGGGGGGGGG"
DATA ".............", "2222222222222", ".............", "GGGGGGGGGG333"
DATA ".............", "5555555555555", ".............", "666GGGGGGGGGG"
DATA 11
DATA ".............", "......S......", ".....S3S.....", "....S323S...."
DATA "...S32G23S...", "..S32GGG23S..", "...S32G23S...", "....S323S...."
DATA ".....S3S.....", "......S......", "............."

' ---- the title screen: TRUE = play, FALSE = quit -----------------------------------------
FUNCTION TitleScreen
  CLS
  FOR x = 0 TO 304 STEP 16
    DrawBrickAt x, 0, 1 + (x \ 16) MOD 7
    DrawBrickAt x, 8, 1 + (x \ 16 + 3) MOD 7
  NEXT
  COLOR 7: LOCATE 4, 17: PRINT "O N Y X";
  ' the big title: the 8 x 16 font, each pixel a 3 x 3 block, red to yellow downwards
  sc = 3: IF tw * sc > 300 THEN sc = 2
  x0 = (320 - tw * sc) \ 2
  FOR i = 0 TO ntl - 1
    x = x0 + tx(i) * sc: y = 40 + ty(i) * sc
    LINE (x + 2, y + 2)-(x + sc + 1, y + sc + 1), 112, BF
  NEXT
  FOR i = 0 TO ntl - 1
    x = x0 + tx(i) * sc: y = 40 + ty(i) * sc
    LINE (x, y)-(x + sc - 1, y + sc - 1), 40 + (ty(i) * 5) \ (th + 1), BF
  NEXT
  COLOR 7: LOCATE 13, 12: PRINT "CATCH THE CAPSULES";
  names$ = "EXPAND    SLOW      CATCH     LASER     3 BALLS   ONE MORE"
  FOR t = 1 TO 6
    col = (t - 1) \ 3: row = (t - 1) MOD 3
    x = 40 + col * 136: y = 112 + row * 16
    PUT (x, y), capSpr((t - 1) * SPR), PSET
    COLOR 15: LOCATE 15 + row * 2, 9 + col * 17: PRINT MID$(names$, (t - 1) * 10 + 1, 10);
  NEXT
  COLOR 8: LOCATE 20, 4: PRINT "ARROWS / MOUSE  MOVE    P  PAUSE";
  LOCATE 21, 4: PRINT "SPACE / CLICK   FIRE    F  SCREEN";
  COLOR 7: LOCATE 25, 12: PRINT "HIGH SCORE"; hiscore;
  Sfx "T140 L16 O4 G > C E G E C8"
  FlushSfx
  DO WHILE MOUSEB AND 1: PAUSE 10: LOOP
  DO
    k$ = UCASE$(INKEY$)
    IF k$ = CHR$(27) THEN TitleScreen = 0: EXIT FUNCTION
    IF k$ = " " OR k$ = CHR$(13) OR (MOUSEB AND 1) THEN EXIT DO
    IF k$ = "F" THEN ToggleFull
    IF (TICKS \ 400) MOD 2 THEN COLOR 44 ELSE COLOR 0
    LOCATE 23, 6: PRINT "PRESS SPACE OR CLICK TO START";
    PAUSE 20
  LOOP
  DO WHILE MOUSEB AND 1: PAUSE 10: LOOP
  TitleScreen = -1
END FUNCTION

' ---- a game: rounds until no life is left -------------------------------------------------
SUB PlayGame
  score = 0: lives = 3: lvl = 1
  DO
    LoadLevel lvl
    IF PlayRound = 0 THEN EXIT DO
    lvl = lvl + 1
  LOOP
END SUB

' One round: 1 = cleared, 0 = game over or Esc.
FUNCTION PlayRound
  CLS
  DrawWalls
  DrawPanel
  FOR r = 0 TO NR - 1
    FOR c = 0 TO NC - 1
      IF br(c, r) THEN DrawBrick c, r
    NEXT
  NEXT
  speed = 2 + (lvl - 1) * .15: IF speed > 3 THEN speed = 3
  Serve
  Banner "ROUND" + STR$(lvl), "READY"
  lastM = MOUSEX: oldB = 0
  nextT = TICKS
  DO
    ' input first (it shows the last frame), then erase, move and draw in one go
    k$ = UCASE$(INKEY$)
    kl = KEYDOWN("LEFT"): kr = KEYDOWN("RIGHT"): ks = KEYDOWN("SPACE")
    m = MOUSEX: mb = MOUSEB AND 1
    IF k$ = CHR$(27) THEN PlayRound = 0: EXIT FUNCTION
    IF k$ = "P" THEN PauseGame
    IF k$ = "F" THEN ToggleFull
    IF k$ = CHR$(0) + "K" AND kl = 0 THEN px = px - 12   ' no held keys (older system)
    IF k$ = CHR$(0) + "M" AND kr = 0 THEN px = px + 12
    IF kl THEN px = px - 5
    IF kr THEN px = px + 5
    IF m <> lastM THEN px = m - pw \ 2: lastM = m
    IF px < LEFTX THEN px = LEFTX
    IF px > RIGHTX - pw + 1 THEN px = RIGHTX - pw + 1
    press = (k$ = " ") OR (mb AND NOT oldB): oldB = mb
    held = ks OR mb

    EraseMovers
    MoveBalls press
    IF laserOn AND (press OR (held AND fireWait = 0)) THEN Fire
    IF fireWait > 0 THEN fireWait = fireWait - 1
    MoveShots
    MoveCapsule
    DrawPaddle
    DrawMovers
    FlushSfx

    IF nleft = 0 THEN
      Sfx "T160 L16 O4 C E G > C8 < G > C4"
      Banner "ROUND" + STR$(lvl), "CLEAR!"
      PlayRound = 1: EXIT FUNCTION
    END IF
    IF nballs = 0 THEN
      LoseLife
      IF lives = 0 THEN
        Banner "GAME OVER", ""
        PlayRound = 0: EXIT FUNCTION
      END IF
      Serve
      Banner "", "READY"
      nextT = TICKS
    END IF
    WaitFrame
  LOOP
END FUNCTION

' ---- the level ----------------------------------------------------------------------------
SUB LoadLevel (n)
  k = (n - 1) MOD NLEVELS              ' the levels come round again (faster)
  RESTORE Levels
  FOR i = 1 TO k
    READ rows
    FOR j = 1 TO rows: READ a$: NEXT
  NEXT
  FOR r = 0 TO NR - 1
    FOR c = 0 TO NC - 1: br(c, r) = 0: hp(c, r) = 0: NEXT
  NEXT
  READ rows
  nleft = 0
  FOR r = 0 TO rows - 1
    READ a$
    FOR c = 0 TO NC - 1
      ch$ = MID$(a$, c + 1, 1): t = 0
      IF ch$ >= "1" AND ch$ <= "8" THEN t = VAL(ch$)
      IF ch$ = "S" THEN t = 9
      IF ch$ = "G" THEN t = 10
      br(c, r) = t: hp(c, r) = 1
      IF t = 9 THEN hp(c, r) = 2 + (n - 1) \ NLEVELS
      IF t > 0 AND t < 10 THEN nleft = nleft + 1
    NEXT
  NEXT
END SUB

' ---- drawing ------------------------------------------------------------------------------
SUB BrickColours (t, cb, cl, cd)        ' body, light edge, dark edge
  SELECT CASE t
  CASE 1 TO 7: cb = 32 + hue(t): cl = 56 + hue(t): cd = 104 + hue(t)
  CASE 8: cb = 29: cl = 31: cd = 24
  CASE 9: cb = 26: cl = 31: cd = 20
  CASE ELSE: cb = 43: cl = 68: cd = 116
  END SELECT
END SUB

SUB DrawBrickAt (x, y, t)
  IF t = 0 THEN LINE (x, y)-(x + BW - 1, y + BH - 1), 0, BF: EXIT SUB
  BrickColours t, cb, cl, cd
  LINE (x, y)-(x + BW - 1, y + BH - 1), cd, BF
  LINE (x, y)-(x + BW - 2, y + BH - 2), cl, BF
  LINE (x + 1, y + 1)-(x + BW - 2, y + BH - 2), cb, BF
END SUB

SUB DrawBrick (c, r)
  DrawBrickAt LEFTX + c * BW, BY0 + r * BH, br(c, r)
END SUB

SUB DrawWalls                          ' three grey pipes, with red rings
  FOR i = 0 TO 7
    c = shade(i)
    LINE (i, i)-(i, 199), c
    LINE (223 - i, i)-(223 - i, 199), c
    LINE (i, i)-(223 - i, i), c
  NEXT
  FOR y = 40 TO 190 STEP 50
    LINE (0, y)-(7, y + 3), 112, BF: LINE (216, y)-(223, y + 3), 112, BF
  NEXT
  FOR x = 50 TO 180 STEP 64
    LINE (x, 0)-(x + 3, 7), 112, BF
  NEXT
END SUB

SUB DrawPanel
  COLOR 36: LOCATE 2, 31: PRINT "O N Y X";
  COLOR 44: LOCATE 3, 30: PRINT "ARKANOID";
  COLOR 7
  LOCATE 6, 30: PRINT "SCORE";
  LOCATE 9, 30: PRINT "HIGH";
  LOCATE 12, 30: PRINT "ROUND";
  LOCATE 15, 30: PRINT "LIVES";
  COLOR 8
  LOCATE 22, 30: PRINT "P  PAUSE";
  LOCATE 23, 30: PRINT "F  SCREEN";
  LOCATE 24, 30: PRINT "ESC QUIT";
  DrawScore
  DrawLives
END SUB

SUB DrawScore
  IF score > hiscore THEN hiscore = score
  COLOR 15
  LOCATE 7, 30: PRINT USING "#######"; score;
  LOCATE 10, 30: PRINT USING "#######"; hiscore;
  LOCATE 13, 30: PRINT USING "#######"; lvl;
END SUB

SUB DrawLives                          ' one small paddle per spare life
  LINE (232, 128)-(319, 139), 0, BF
  n = lives - 1: IF n > 5 THEN n = 5
  FOR i = 1 TO n
    x = 232 + (i - 1) * 14
    LINE (x, 130)-(x + 11, 133), 7, BF
    LINE (x, 130)-(x + 2, 133), 40, BF: LINE (x + 9, 130)-(x + 11, 133), 40, BF
  NEXT
END SUB

SUB DrawPaddle
  IF opx <> px OR opw <> pw THEN LINE (opx, PY)-(opx + opw - 1, PY + 5), 0, BF
  x2 = px + pw - 1
  LINE (px, PY)-(x2, PY + 5), 7, BF
  LINE (px, PY)-(x2, PY), 15
  LINE (px, PY + 5)-(x2, PY + 5), 8
  IF laserOn THEN ec = 52 ELSE ec = 40
  LINE (px, PY)-(px + 4, PY + 5), ec, BF
  LINE (x2 - 4, PY)-(x2, PY + 5), ec, BF
  IF laserOn THEN LINE (px + 2, PY - 2)-(px + 2, PY - 1), 15: LINE (x2 - 2, PY - 2)-(x2 - 2, PY - 1), 15
  IF catchOn THEN LINE (px + 7, PY + 2)-(x2 - 7, PY + 3), 48, BF
  opx = px: opw = pw
END SUB

' The balls, shots and capsule: erased where they were, drawn where they are.
SUB EraseMovers
  FOR i = 0 TO MAXB
    IF bdrawn(i) THEN LINE (obx(i), oby(i))-(obx(i) + BS - 1, oby(i) + BS - 1), 0, BF: bdrawn(i) = 0
  NEXT
  FOR k = 0 TO MAXS
    IF shDrawn(k) THEN LINE (shX(k), oshY(k))-(shX(k), oshY(k) + 3), 0: shDrawn(k) = 0
  NEXT
  IF capDrawn THEN
    LINE (capX, ocapY)-(capX + 15, ocapY + 7), 0, BF
    c = CellC(capX)                     ' the capsule passes in front of bricks: put them back
    FOR r = CellR(ocapY) TO CellR(ocapY + 7)
      IF IsBrick(c, r) THEN DrawBrick c, r
    NEXT
    capDrawn = 0
  END IF
  LINE (px, PY - 2)-(px + pw - 1, PY - 1), 0, BF      ' the laser cannons
END SUB

SUB DrawMovers
  IF capOn THEN PUT (capX, capY), capSpr((capT - 1) * SPR), PSET: ocapY = capY: capDrawn = -1
  FOR k = 0 TO MAXS
    IF shOn(k) THEN LINE (shX(k), shY(k))-(shX(k), shY(k) + 3), 44: oshY(k) = shY(k): shDrawn(k) = -1
  NEXT
  FOR i = 0 TO MAXB
    IF bon(i) AND by(i) <= BOTY - BS THEN
      x = INT(bx(i)): y = INT(by(i))
      LINE (x + 1, y)-(x + 2, y + 3), 15, BF
      LINE (x, y + 1)-(x + 3, y + 2), 15, BF
      PSET (x + 1, y + 1), 31
      obx(i) = x: oby(i) = y: bdrawn(i) = -1
    END IF
  NEXT
END SUB

SUB Banner (a$, b$)                    ' two lines in the middle of the playfield, 1.5 s
  COLOR 15: LOCATE 17, 15 - LEN(a$) \ 2: PRINT a$;
  COLOR 44: LOCATE 19, 15 - LEN(b$) \ 2: PRINT b$;
  FlushSfx
  t = TICKS
  DO WHILE TICKS - t < 1500 AND TICKS >= t
    k$ = INKEY$
    PAUSE 20
  LOOP
  LINE (LEFTX, 128)-(RIGHTX, 159), 0, BF
END SUB

' ---- the paddle, the balls ----------------------------------------------------------------
SUB Serve                              ' a new paddle with one ball on it
  EraseMovers
  LINE (LEFTX, PY - 2)-(RIGHTX, PY + 5), 0, BF
  capOn = 0
  FOR k = 0 TO MAXS: shOn(k) = 0: NEXT
  pw = 32: px = (LEFTX + RIGHTX + 1 - pw) \ 2: opx = px: opw = pw
  catchOn = 0: laserOn = 0
  FOR i = 0 TO MAXB: bon(i) = 0: NEXT
  bon(0) = -1: stuck(0) = -1: soff(0) = pw \ 2 - BS \ 2: nballs = 1
  bx(0) = px + soff(0): by(0) = PY - BS
  DrawPaddle
  DrawMovers
END SUB

SUB MoveBalls (press)
  FOR i = 0 TO MAXB
    IF bon(i) THEN
      IF stuck(i) THEN
        bx(i) = px + soff(i): by(i) = PY - BS
        IF press THEN ReleaseBall i
      ELSE
        MoveBall i
      END IF
    END IF
  NEXT
END SUB

SUB ReleaseBall (i)                         ' off the paddle, the angle after where it sits
  stuck(i) = 0
  rel = (soff(i) + BS / 2 - pw / 2) / (pw / 2)
  a = rel * 1.05
  IF ABS(a) < .35 THEN
    IF a < 0 THEN a = -.35 ELSE a = .35
  END IF
  dx(i) = speed * SIN(a): dy(i) = -speed * COS(a)
  Sfx "T255 L32 O3 C"
END SUB

SUB MoveBall (i)
  n = INT(speed / 1.5) + 1              ' small steps: never through a brick
  FOR s = 1 TO n
    nx = bx(i) + dx(i) / n
    IF nx < LEFTX THEN nx = LEFTX: dx(i) = ABS(dx(i)): Sfx "T255 L64 O2 A"
    IF nx > RIGHTX - BS + 1 THEN nx = RIGHTX - BS + 1: dx(i) = -ABS(dx(i)): Sfx "T255 L64 O2 A"
    IF dx(i) > 0 THEN ex = nx + BS - 1 ELSE ex = nx
    IF Hit2(ex, by(i), ex, by(i) + BS - 1) THEN dx(i) = -dx(i) ELSE bx(i) = nx

    ny = by(i) + dy(i) / n
    IF ny < TOPY THEN ny = TOPY: dy(i) = ABS(dy(i)): Sfx "T255 L64 O2 A"
    IF dy(i) > 0 THEN ey = ny + BS - 1 ELSE ey = ny
    IF Hit2(bx(i), ey, bx(i) + BS - 1, ey) THEN dy(i) = -dy(i) ELSE by(i) = ny

    IF dy(i) > 0 THEN
      bot = by(i) + BS - 1
      IF bot >= PY AND bot <= PY + 3 AND bx(i) + BS - 1 >= px AND bx(i) <= px + pw - 1 THEN
        PaddleBounce i
        IF stuck(i) THEN EXIT SUB
      END IF
    END IF
    IF by(i) > BOTY THEN bon(i) = 0: nballs = nballs - 1: EXIT SUB
  NEXT
END SUB

SUB PaddleBounce (i)                   ' the further from the middle, the flatter
  rel = (bx(i) + BS / 2 - (px + pw / 2)) / (pw / 2 + BS / 2)
  IF rel < -1 THEN rel = -1
  IF rel > 1 THEN rel = 1
  IF speed < 4 THEN speed = speed + .03
  a = rel * 1.05
  dx(i) = speed * SIN(a): dy(i) = -speed * COS(a)
  by(i) = PY - BS
  IF catchOn THEN stuck(i) = -1: soff(i) = bx(i) - px
  Sfx "T255 L32 O3 C"
END SUB

' ---- the bricks ---------------------------------------------------------------------------
FUNCTION CellC (x)
  CellC = INT((x - LEFTX) / BW)
END FUNCTION

FUNCTION CellR (y)
  CellR = INT((y - BY0) / BH)
END FUNCTION

FUNCTION IsBrick (c, r)
  IsBrick = 0
  IF c < 0 OR c >= NC OR r < 0 OR r >= NR THEN EXIT FUNCTION
  IsBrick = br(c, r) <> 0
END FUNCTION

' The two corners of a moving edge: hits the brick(s) there, TRUE if there was one.
FUNCTION Hit2 (x1, y1, x2, y2)
  h = 0
  c1 = CellC(x1): r1 = CellR(y1): c2 = CellC(x2): r2 = CellR(y2)
  IF IsBrick(c1, r1) THEN HitBrick c1, r1: h = -1
  IF (c2 <> c1 OR r2 <> r1) AND IsBrick(c2, r2) THEN HitBrick c2, r2: h = -1
  Hit2 = h
END FUNCTION

SUB HitBrick (c, r)
  t = br(c, r)
  IF t = 10 THEN Sfx "T255 L64 O6 G": EXIT SUB          ' gold
  hp(c, r) = hp(c, r) - 1
  IF hp(c, r) > 0 THEN Sfx "T255 L64 O6 C": EXIT SUB     ' silver, not yet
  br(c, r) = 0: nleft = nleft - 1
  DrawBrick c, r
  IF t = 9 THEN score = score + 50 * lvl ELSE score = score + 40 + 10 * t
  Sfx "T255 L64 O5 " + MID$("CDEFGAB", (r MOD 7) + 1, 1)
  DrawScore
  ' now and then a capsule (not while several balls are in play)
  IF capOn = 0 AND nballs = 1 AND RND < .2 THEN
    capT = 1 + INT(RND * 5): IF RND < .1 THEN capT = 6
    capOn = -1: capX = LEFTX + c * BW: capY = BY0 + r * BH
  END IF
END SUB

' ---- capsules and the laser ---------------------------------------------------------------
SUB MoveCapsule
  IF capOn = 0 THEN EXIT SUB
  capY = capY + 1
  IF capY + 7 > BOTY THEN capOn = 0: EXIT SUB
  IF capY + 7 >= PY AND capY <= PY + 5 AND capX + 15 >= px AND capX <= px + pw - 1 THEN
    capOn = 0
    Collect capT
  END IF
END SUB

SUB Collect (t)
  score = score + 1000: DrawScore
  Sfx "T200 L32 O4 C E G > C"
  IF t <> 3 AND catchOn THEN            ' catch ends: the held balls go
    catchOn = 0
    FOR i = 0 TO MAXB
      IF bon(i) AND stuck(i) THEN ReleaseBall i
    NEXT
  END IF
  IF t <> 4 THEN laserOn = 0
  IF t <> 1 THEN pw = 32
  SELECT CASE t
  CASE 1: pw = 48: IF px + pw - 1 > RIGHTX THEN px = RIGHTX - pw + 1
  CASE 2
    speed = 1.6
    FOR i = 0 TO MAXB
      IF bon(i) AND stuck(i) = 0 THEN
        f = speed / SQR(dx(i) * dx(i) + dy(i) * dy(i))
        dx(i) = dx(i) * f: dy(i) = dy(i) * f
      END IF
    NEXT
  CASE 3: catchOn = -1
  CASE 4: laserOn = -1
  CASE 5: Split
  CASE 6: lives = lives + 1: DrawLives
  END SELECT
END SUB

SUB Split                              ' disruption: the ball becomes three
  j = -1
  FOR i = 0 TO MAXB
    IF bon(i) AND j < 0 THEN j = i
  NEXT
  IF j < 0 THEN EXIT SUB
  IF stuck(j) THEN ReleaseBall j
  a = .4
  FOR i = 0 TO MAXB
    IF bon(i) = 0 THEN
      bon(i) = -1: stuck(i) = 0: bx(i) = bx(j): by(i) = by(j)
      dx(i) = dx(j) * COS(a) - dy(j) * SIN(a)
      dy(i) = dx(j) * SIN(a) + dy(j) * COS(a)
      IF dy(i) > -.5 AND dy(i) < .5 THEN dy(i) = -.8
      nballs = nballs + 1
      a = -a
    END IF
  NEXT
END SUB

SUB Fire                               ' two shots, from the paddle's ends
  IF fireWait > 0 THEN EXIT SUB
  n = 0
  FOR k = 0 TO MAXS
    IF shOn(k) = 0 AND n < 2 THEN
      shOn(k) = -1: shY(k) = PY - 6
      IF n = 0 THEN shX(k) = px + 2 ELSE shX(k) = px + pw - 3
      n = n + 1
    END IF
  NEXT
  fireWait = 12
  Sfx "T255 L64 O6 E"
END SUB

SUB MoveShots
  FOR k = 0 TO MAXS
    IF shOn(k) THEN
      shY(k) = shY(k) - 5
      IF shY(k) < TOPY THEN
        shOn(k) = 0
      ELSEIF Hit2(shX(k), shY(k), shX(k), shY(k) + 3) THEN
        shOn(k) = 0
      END IF
    END IF
  NEXT
END SUB

' ---- lives, pause, full screen, time, sound -----------------------------------------------
SUB LoseLife
  EraseMovers
  Sfx "T150 L16 O3 G E C < G4": FlushSfx
  FOR f = 1 TO 8                       ' the paddle blinks and goes
    LINE (px, PY)-(px + pw - 1, PY + 5), 40 + (f MOD 2) * 4, BF
    PAUSE 70
  NEXT
  LINE (px, PY - 2)-(px + pw - 1, PY + 5), 0, BF
  lives = lives - 1
  DrawLives
END SUB

SUB PauseGame
  COLOR 44: LOCATE 19, 30: PRINT "PAUSED";
  DO
    k$ = UCASE$(INKEY$)
    IF k$ = "F" THEN ToggleFull
    PAUSE 20
  LOOP UNTIL k$ = "P" OR k$ = " " OR k$ = CHR$(27)
  LOCATE 19, 30: PRINT "      ";
  nextT = TICKS
END SUB

SUB ToggleFull
  fs = NOT fs
  IF fs THEN FULLSCREEN ON ELSE FULLSCREEN OFF
END SUB

SUB WaitFrame                          ' 50 frames a second
  nextT = nextT + 20
  t = TICKS
  IF t > nextT + 100 OR t < nextT - 100 THEN nextT = t    ' far behind (or midnight)
  DO WHILE TICKS < nextT
    PAUSE 5
  LOOP
END SUB

SUB Sfx (m$)                           ' a sound for the end of the frame (the first wins)
  IF sfx$ = "" THEN sfx$ = m$
END SUB

SUB FlushSfx
  IF sndOn AND sfx$ <> "" THEN
    IF PLAY(0) < 6 THEN PLAY "MB " + sfx$
  END IF
  sfx$ = ""
END SUB

' ---- sprites, made once: the capsules (GET) and the title's letters (POINT) ---------------
SUB MakeSprites
  RESTORE Letters
  FOR t = 1 TO 6
    READ l$
    c = VAL(MID$("32 42 48 40 52 24", (t - 1) * 3 + 1, 2))
    IF t = 6 THEN d = 20 ELSE d = c + 72
    CLS
    LINE (1, 0)-(14, 7), d, BF
    LINE (0, 1)-(15, 6), d, BF
    LINE (1, 1)-(14, 5), c, BF
    FOR j = 0 TO 14
      IF MID$(l$, j + 1, 1) = "1" THEN PSET (6 + j MOD 3, 1 + j \ 3), 15
    NEXT
    GET (0, 0)-(15, 7), capSpr((t - 1) * SPR)
  NEXT
  CLS
  DRAWTEXT 0, 0, "ARKANOID", 15
  ntl = 0: tw = 0: th = 0
  FOR y = 0 TO 23
    FOR x = 0 TO 95
      IF POINT(x, y) THEN
        tx(ntl) = x: ty(ntl) = y: ntl = ntl + 1
        IF x + 1 > tw THEN tw = x + 1
        IF y + 1 > th THEN th = y + 1
      END IF
    NEXT
  NEXT
  CLS
END SUB
