' antigrav.bas -- Onyx BASIC: Antigrav, an anti-gravity racer in the manner of the mid-90s ones.
'
' Everything is made by the program: the circuits (from a number: a closed loop that climbs, dives and
' banks in its turns, with tunnels, arches, speed pads and weapon pads), the city around them, the eight
' craft, the pictures (drawn with LINE / PSET and taken with GRAB3D), the music (the General MIDI
' synthesizer) and the sounds (the voices). No file is read but the system's language.
'
'   Left / Right ......... steer          Up or X .......... thrust
'   Z / C ................ air brakes     Space ............ fire the weapon picked up
'   P or Esc ............. pause          F ................ full screen on / off
'   A gamepad: the d-pad or the left stick, A thrust, X fire, L / R air brakes, Start pause.
'
' One to four players, as many as gamepads are plugged in (the first one plays on the keyboard too),
' the screen shared between them; the computer's pilots make six craft in all. Six circuits, two
' classes (Venom, Rapier), one, three or five laps. Blue chevrons throw you forward; the red pads
' give a weapon: rockets, a homing missile, mines, a shield, a turbo. The best lap of each circuit
' and class is kept (SD:/apps/antigrav.app/times.txt). It starts full screen (antigrav window: in
' a window); antigrav fps shows the pictures a second.
'
' Shows: 3D with VERTEX3D (the track, the craft: thousands of triangles a frame, coloured vertex by
' vertex), VIEW3D (the screen shared), BLEND3D 2 (glows), GRAB3D textures, CUBE3D buildings,
' KEYDOWN / PAD / PADS / STICK, MIDINOTE music, NOTEON sounds, FULLSCREEN, SUBs and FUNCTIONs on
' SHARED arrays.
'
' MIT licence -- the Onyx authors, 2026.

CONST PI = 3.14159265
CONST NSEG = 480, HW = 5, WALLH = 1.4, NSHIP = 6, NPROJ = 32, MAXBLD = 90, MAXP = 4
CONST NCIRC = 6

' ---- the circuit -------------------------------------------------------------------------------
DIM SHARED px(NSEG), py(NSEG), pz(NSEG)          ' the centre line
DIM SHARED tx(NSEG), ty(NSEG), tz(NSEG)          ' its direction
DIM SHARED rx(NSEG), ry(NSEG), rz(NSEG)          ' to the right, banked
DIM SHARED ux(NSEG), uy(NSEG), uz(NSEG)          ' up, banked
DIM SHARED cv(NSEG)                              ' the curvature (> 0: turning right)
DIM SHARED kind(NSEG), lane(NSEG)                ' 1 tunnel, 2 speed pad, 4 weapon pad, 8 arch; the pad's lane
DIM SHARED ex1(NSEG), ey1(NSEG), ez1(NSEG), ex2(NSEG), ey2(NSEG), ez2(NSEG)   ' the edges
DIM SHARED wx1(NSEG), wy1(NSEG), wz1(NSEG), wx2(NSEG), wy2(NSEG), wz2(NSEG)   ' the walls' tops
DIM SHARED bsx(MAXBLD), bsy(MAXBLD), bsz(MAXBLD), bh(MAXBLD), bw(MAXBLD), bcol(MAXBLD), bseg(MAXBLD), nbld
DIM SHARED mmx(60), mmy(60)                       ' the small map (-1..1)
DIM SHARED seglen, tlen, groundY, circ, cname$(NCIRC), cseed(NCIRC)
DIM SHARED dxs(4 * NSEG + 4), dys(4 * NSEG + 4), dzs(4 * NSEG + 4), dl(4 * NSEG + 4)
DIM SHARED mtx(48), mty(48), mtz(48)             ' the mountains around

' ---- the craft ---------------------------------------------------------------------------------
DIM SHARED sd(NSHIP), lx(NSHIP), v(NSHIP), vl(NSHIP), steer(NSHIP), thrust(NSHIP)
DIM SHARED wep(NSHIP), shield(NSHIP), turbo(NSHIP), spin(NSHIP), skill(NSHIP), aioff(NSHIP)
DIM SHARED arrived(NSHIP), fintime(NSHIP), lapst(NSHIP), bestlap(NSHIP), lastlap(NSHIP), place(NSHIP), firewait(NSHIP)
DIM SHARED col1(NSHIP), col2(NSHIP), pilot$(NSHIP), order(NSHIP)
DIM SHARED shx(NSHIP), shy(NSHIP), shz(NSHIP)    ' where each craft is (world)
' the model: 11 points, 14 triangles (indices), each with its shade (1 = colour 1, 2 = colour 2, 3 dark); drawn both
' faces (a face and its back are one triangle: two would fight in the depth)
DIM SHARED mvx(11), mvy(11), mvz(11), mt(42), mshade(14)
DIM SHARED wx, wy, wz                            ' TrackAt's answer
DIM SHARED ftx, fty, ftz, frx, fry, frz, fux, fuy, fuz, fcv, fkind, flane

' ---- the shots, the mines ----------------------------------------------------------------------
DIM SHARED pk(NPROJ), ps(NPROJ), pl(NPROJ), pv(NPROJ), pown(NPROJ), ptgt(NPROJ), plife(NPROJ)

' ---- the game ----------------------------------------------------------------------------------
DIM SHARED tex.track, tex.boost, tex.weapon, tex.wall, tex.glow, tex.ground, tex.check
DIM SHARED vmax, laps, klass, state, racet, cdown, paused, fs, demo
DIM SHARED autoP, frames, fps, fpsT, showFps, vdist, np, npOver, padslot(MAXP), pcamL(MAXP), pcamH(MAXP)
DIM SHARED vx(MAXP), vy(MAXP), vw(MAXP), vh(MAXP), pmsg$(MAXP), pmsgt(MAXP), kfire(MAXP)
DIM SHARED cex, cey, cez, ctx, cty, ctz, crx, cry, crz, cux, cuy, cuz
DIM SHARED t, dt, best(NCIRC, 1), recordNew
DIM SHARED sndOn, musOn, engF, sfxEnd(16), mstep, mnext, mbar, mseed, mlead(16), mbass, mleadOn, mpad(3)
DIM SHARED lang$, trE$(80), trF$(80), ntr, fontch$, glyph$(48)
DIM SHARED padprev, menuSel, wname$(5), aiName$(NSHIP)

RANDOMIZE TIMER
LoadWords
fontch$ = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-!.:/<>"
RESTORE BigFont
FOR i = 1 TO LEN(fontch$): READ glyph$(i): NEXT
RESTORE Circuits
FOR i = 1 TO NCIRC: READ cname$(i), cseed(i): NEXT
RESTORE Pilots
FOR i = 0 TO NSHIP - 1: READ aiName$(i), a, b: col1(i) = Hex2Rgb(a): col2(i) = Hex2Rgb(b): NEXT
wname$(1) = T$("ROCKETS"): wname$(2) = T$("MISSILE"): wname$(3) = T$("MINES")
wname$(4) = T$("SHIELD"): wname$(5) = T$("TURBO")
IF INSTR(UCASE$(COMMAND$), "DEMO") THEN demo = -1
IF INSTR(UCASE$(COMMAND$), "FPS") THEN showFps = -1
IF INSTR(UCASE$(COMMAND$), "AUTO") THEN autoP = -1     ' (a test: the players' craft fly themselves)
i = INSTR(UCASE$(COMMAND$), "PLAYERS=")
IF i THEN npOver = VAL(MID$(COMMAND$, i + 8, 1))      ' (players=n: n views without the pads -- with demo)
np = 1: vdist = 66

SCREEN 12
fs = -1
IF INSTR(UCASE$(COMMAND$), "WINDOW") THEN fs = 0
IF fs THEN FULLSCREEN ON

' The sound output may be busy (another program plays): then the game is silent. The music needs the
' General MIDI synthesizer's SoundFont (the package generaluser-gs): without it, the sounds only.
sndOn = -1: musOn = -1
ON ERROR GOTO NoSound
NOTEON 15, 100, 1, 0: NOTEOFF 15
ON ERROR GOTO NoMusic
MIDIPROGRAM 0, 38: MIDINOTE 0, 40, 0
ON ERROR GOTO 0

MakeTextures
MakeModel
LoadTimes
circ = 1: klass = 0: laps = 3
IF INSTR(UCASE$(COMMAND$), "LAPS1") THEN laps = 1
BuildCircuit circ

DO
  TitleScreen
  Race
LOOP

NoSound:
  sndOn = 0: musOn = 0
  RESUME NEXT
NoMusic:
  musOn = 0
  RESUME NEXT

Circuits:    ' the circuits: their names, the numbers they are made from
DATA "Nova Ring", 1207, "Cobalt Spire", 4410, "Helix Bay", 912, "Ember Canyon", 7781
DATA "Glacier Run", 3356, "Zenith Arc", 6060

Pilots:      ' the craft's colours, the computer's pilots (the players take the first ones: P1 blue, P2 red...)
DATA "KESTREL", &H2A6BFF, &HF2F4FA, "VOLTA", &HE8402A, &HF6D44A, "ORBIT", &H39D17A, &H1C2830
DATA "TALON", &HF5C800, &H3060D0, "ZEPHYR", &HB04CF0, &HEDE6F8, "QUASAR", &H2CD4E8, &H123A66

Words:       ' English, French (SD:/etc/system.ini's language=fr)
DATA "CIRCUIT", "CIRCUIT", "CLASS", "CLASSE", "LAPS", "TOURS", "START", "DEPART", "QUIT", "QUITTER"
DATA "LAP", "TOUR", "POS", "POS", "BEST", "RECORD", "FINISH", "ARRIVEE", "PAUSED", "PAUSE"
DATA "RESUME", "REPRENDRE", "ROCKETS", "ROQUETTES", "MISSILE", "MISSILE", "MINES", "MINES"
DATA "SHIELD", "BOUCLIER", "TURBO", "TURBO", "GO", "PARTEZ", "NEW RECORD", "NOUVEAU RECORD"
DATA "Up/Down choose  Left/Right change  Enter start  F full screen  Esc quit"
DATA "Haut/Bas choisir  Gauche/Droite changer  Entree jouer  F ecran  Esc quitter"
DATA "Arrows steer  Up/X thrust  Z/C air brakes  Space fire  P pause"
DATA "Fleches diriger  Haut/X pousser  Z/C aerofreins  Espace tirer  P pause"
DATA "Enter: back to the menu", "Entree : retour au menu", "km/h", "km/h"
DATA "Time", "Temps", "Best lap", "Meilleur tour", "Hit!", "Touche !", "Shield", "Bouclier"
DATA "BACK TO MENU", "MENU", "WRONG WAY", "MAUVAIS SENS", "ANTIGRAV", "ANTIGRAV"
DATA "PLAYERS", "JOUEURS", "P", "J", "gamepads", "manettes", "Waiting for the others", "Les autres arrivent"
DATA "", ""

Model:   ' x (right), y (up), z (forward); then the triangles: three points and a shade
DATA 0, .18, 1.7,  -.9, 0, -1.0,  .9, 0, -1.0,  0, .55, -.8,  0, -.12, -.9
DATA -1.35, .06, -1.15,  1.35, .06, -1.15,  -.55, .06, .5,  .55, .06, .5,  0, 1.05, -1.3,  0, .45, -.25
DATA 1, 4, 2, 1,   1, 3, 4, 1,   1, 2, 5, 3,   1, 5, 3, 3,   4, 3, 5, 3,   4, 5, 2, 3
DATA 8, 6, 2, 2,   9, 3, 7, 2,   11, 10, 4, 2,  1, 11, 4, 1
DATA 1, 8, 2, 2,   1, 3, 9, 2,   8, 1, 11, 4,  9, 11, 1, 4

BigFont:     ' the big letters, 5 x 7: a string of 35 a glyph (# = lit), in the order of FONTCH$
DATA ".###.#...##...#######...##...##...#"
DATA "####.#...##...#####.#...##...#####."
DATA ".###.#...##....#....#....#...#.###."
DATA "####.#...##...##...##...##...#####."
DATA "######....#....####.#....#....#####"
DATA "######....#....####.#....#....#...."
DATA ".###.#...##....#.####...##...#.####"
DATA "#...##...##...#######...##...##...#"
DATA ".###...#....#....#....#....#...###."
DATA "..###...#....#....#.#..#.#..#..##.."
DATA "#...##..#.#.#..##...#.#..#..#.#...#"
DATA "#....#....#....#....#....#....#####"
DATA "#...###.###.#.##.#.##...##...##...#"
DATA "#...##...###..##.#.##..###...##...#"
DATA ".###.#...##...##...##...##...#.###."
DATA "####.#...##...#####.#....#....#...."
DATA ".###.#...##...##...##.#.##..#..##.#"
DATA "####.#...##...#####.#.#..#..#.#...#"
DATA ".#####....#.....###.....#....#####."
DATA "#####..#....#....#....#....#....#.."
DATA "#...##...##...##...##...##...#.###."
DATA "#...##...##...##...##...#.#.#...#.."
DATA "#...##...##...##.#.##.#.##.#.#.#.#."
DATA "#...##...#.#.#...#...#.#.#...##...#"
DATA "#...##...#.#.#...#....#....#....#.."
DATA "#####....#...#...#...#...#....#####"
DATA ".###.#...##..###.#.###..##...#.###."
DATA "..#...##....#....#....#....#...###."
DATA ".###.#...#....#...#...#...#...#####"
DATA "####.....#....#.###.....#....#####."
DATA "...#...##..#.#.#..#.#####...#....#."
DATA "######....####.....#....##...#.###."
DATA ".###.#....#....####.#...##...#.###."
DATA "#####....#...#...#...#....#....#..."
DATA ".###.#...##...#.###.#...##...#.###."
DATA ".###.#...##...#.####....#....#.###."
DATA "...............#####..............."
DATA "..#....#....#....#....#.........#.."
DATA "..........................##...##.."
DATA "......##...##........##...##......."
DATA "....#....#...#...#...#...#....#...."
DATA "...#...#...#...#.....#.....#.....#."
DATA ".#.....#.....#.....#...#...#...#..."

' ================================================================================================
' Words
' ================================================================================================
' &HRRGGBB -> an RGB () colour
FUNCTION Hex2Rgb (h)
  Hex2Rgb = RGB((h \ 65536) AND 255, (h \ 256) AND 255, h AND 255)
END FUNCTION

SUB LoadWords
  lang$ = "en"
  IF FILEEXISTS("SD:/etc/system.ini") THEN
    f = FREEFILE
    OPEN "SD:/etc/system.ini" FOR INPUT AS #f
    DO WHILE NOT EOF(f)
      LINE INPUT #f, l$
      l$ = LCASE$(TRIM$(l$))
      IF LEFT$(l$, 9) = "language=" THEN lang$ = TRIM$(MID$(l$, 10))
      IF LEFT$(l$, 10) = "language =" THEN lang$ = TRIM$(MID$(l$, 11))
    LOOP
    CLOSE #f
  END IF
  RESTORE Words
  ntr = 0
  DO
    READ e$, f$
    IF e$ = "" THEN EXIT DO
    ntr = ntr + 1: trE$(ntr) = e$: trF$(ntr) = f$
  LOOP
END SUB

FUNCTION T$ (e$)
  T$ = e$
  IF lang$ <> "fr" THEN EXIT FUNCTION
  FOR i = 1 TO ntr
    IF trE$(i) = e$ THEN T$ = trF$(i): EXIT FUNCTION
  NEXT
END FUNCTION

' ================================================================================================
' The pictures: drawn in a corner of the screen, then taken (GRAB3D)
' ================================================================================================
SUB MakeTextures
  CLS
  ' the track: dark tiles, the lanes' dashes, a bright edge
  LINE (0, 0)-(63, 63), RGB(46, 50, 64), BF
  FOR i = 1 TO 260: PSET (RND * 63, RND * 63), RGB(54 + RND * 14, 58 + RND * 14, 74 + RND * 14): NEXT
  LINE (0, 31)-(63, 32), RGB(36, 38, 50), BF
  LINE (20, 0)-(21, 20), RGB(110, 140, 190), BF: LINE (42, 0)-(43, 20), RGB(110, 140, 190), BF
  LINE (20, 32)-(21, 52), RGB(110, 140, 190), BF: LINE (42, 32)-(43, 52), RGB(110, 140, 190), BF
  LINE (0, 0)-(3, 63), RGB(230, 200, 60), BF: LINE (60, 0)-(63, 63), RGB(230, 200, 60), BF
  FOR y = 0 TO 63 STEP 16: LINE (0, y)-(3, y + 7), RGB(30, 30, 30), BF: LINE (60, y + 8)-(63, y + 15), RGB(30, 30, 30), BF: NEXT
  tex.track = GRAB3D(0, 0, 64, 64)

  ' a speed pad: chevrons pointing forward (the texture's top is ahead)
  LINE (0, 0)-(31, 31), RGB(10, 30, 70), BF
  FOR k = 0 TO 1
    yo = k * 16
    FOR w = 0 TO 4
      LINE (3, 12 + yo + w)-(15, 2 + yo + w), RGB(80, 200, 255)
      LINE (28, 12 + yo + w)-(16, 2 + yo + w), RGB(80, 200, 255)
    NEXT
  NEXT
  LINE (0, 0)-(31, 31), RGB(140, 230, 255), B
  tex.boost = GRAB3D(0, 0, 32, 32)

  ' a weapon pad: red, a diamond
  LINE (0, 0)-(31, 31), RGB(90, 10, 16), BF
  FOR w = 0 TO 3
    LINE (16, 3 + w)-(28 - w, 16), RGB(255, 70, 60): LINE (28 - w, 16)-(16, 28 - w), RGB(255, 70, 60)
    LINE (16, 28 - w)-(3 + w, 16), RGB(255, 70, 60): LINE (3 + w, 16)-(16, 3 + w), RGB(255, 70, 60)
  NEXT
  LINE (13, 13)-(18, 18), RGB(255, 220, 120), BF
  LINE (0, 0)-(31, 31), RGB(255, 140, 120), B
  tex.weapon = GRAB3D(0, 0, 32, 32)

  ' a wall: dark panels, a neon line along the top
  FOR y = 0 TO 31
    c = 40 + y * 2
    LINE (0, y)-(31, y), RGB(c * .5, c * .55, c * .8)
  NEXT
  LINE (0, 0)-(31, 3), RGB(255, 255, 255), BF: LINE (0, 4)-(31, 5), RGB(120, 220, 255), BF
  LINE (15, 6)-(16, 31), RGB(20, 22, 30), BF
  tex.wall = GRAB3D(0, 0, 32, 32)

  ' a glow (added light: black adds nothing)
  LINE (0, 0)-(31, 31), RGB(0, 0, 0), BF
  FOR r = 15 TO 0 STEP -1
    c = 255 * (1 - r / 16) ^ 2
    CIRCLE (16, 16), r, RGB(c, c, c): PAINT (16, 16), RGB(c, c, c), RGB(c, c, c)
  NEXT
  tex.glow = GRAB3D(0, 0, 32, 32)

  ' the ground: a dark grid
  LINE (0, 0)-(31, 31), RGB(10, 8, 22), BF
  LINE (0, 0)-(31, 0), RGB(70, 30, 110): LINE (0, 0)-(0, 31), RGB(70, 30, 110)
  tex.ground = GRAB3D(0, 0, 32, 32)

  ' the chequered banner of the start
  FOR y = 0 TO 3: FOR x = 0 TO 15
    IF (x + y) MOD 2 THEN c = RGB(250, 250, 250) ELSE c = RGB(20, 20, 20)
    LINE (x * 4, y * 4)-(x * 4 + 3, y * 4 + 3), c, BF
  NEXT: NEXT
  tex.check = GRAB3D(0, 0, 64, 16)
  CLS
END SUB

' ================================================================================================
' The circuit, from its number
' ================================================================================================
SUB BuildCircuit (c)
  RANDOMIZE cseed(c)
  M = 4 * NSEG
  ' a star-shaped loop (a radius for each angle: it never crosses itself) that climbs and dives
  f1 = 2 + INT(RND * 2): f2 = f1 + 1 + INT(RND * 2): f3 = 5 + INT(RND * 3)
  p1 = RND * 2 * PI: p2 = RND * 2 * PI: p3 = RND * 2 * PI
  g1 = 1 + INT(RND * 3): g2 = 3 + INT(RND * 3): q1 = RND * 2 * PI: q2 = RND * 2 * PI
  h1 = 10 + RND * 16: h2 = 3 + RND * 7
  amp = 1
  DO
    a1 = (.16 + RND * .14) * amp: a2 = (.08 + RND * .1) * amp: a3 = (.03 + RND * .05) * amp
    ' the horizontal length of the loop at radius 1, to make it NSEG segments of about 5 long
    lh = 0: ox = 0: oz = 0
    FOR i = 0 TO M
      th = i * 2 * PI / M
      r = 1 + a1 * SIN(f1 * th + p1) + a2 * SIN(f2 * th + p2) + a3 * SIN(f3 * th + p3)
      x = r * COS(th): z = r * SIN(th)
      IF i > 0 THEN lh = lh + SQR((x - ox) ^ 2 + (z - oz) ^ 2)
      ox = x: oz = z
    NEXT
    r0 = NSEG * 5 / lh
    FOR i = 0 TO M
      th = i * 2 * PI / M
      r = r0 * (1 + a1 * SIN(f1 * th + p1) + a2 * SIN(f2 * th + p2) + a3 * SIN(f3 * th + p3))
      dxs(i) = r * COS(th): dzs(i) = -r * SIN(th)
      dys(i) = h1 * SIN(g1 * th + q1) + h2 * SIN(g2 * th + q2)
      IF i = 0 THEN dl(i) = 0 ELSE dl(i) = dl(i - 1) + SQR((dxs(i) - dxs(i - 1)) ^ 2 + (dys(i) - dys(i - 1)) ^ 2 + (dzs(i) - dzs(i - 1)) ^ 2)
    NEXT
    ' the same length between the points
    tlen = dl(M): seglen = tlen / NSEG
    j = 0
    FOR i = 0 TO NSEG - 1
      d = i * seglen
      DO WHILE dl(j + 1) < d: j = j + 1: LOOP
      f = (d - dl(j)) / (dl(j + 1) - dl(j) + .000001)
      px(i) = dxs(j) + (dxs(j + 1) - dxs(j)) * f
      py(i) = dys(j) + (dys(j + 1) - dys(j)) * f
      pz(i) = dzs(j) + (dzs(j + 1) - dzs(j)) * f
    NEXT
    px(NSEG) = px(0): py(NSEG) = py(0): pz(NSEG) = pz(0)
    ' the directions and the curvature
    FOR i = 0 TO NSEG - 1
      a = (i + NSEG - 1) MOD NSEG: b = (i + 1) MOD NSEG
      x = px(b) - px(a): y = py(b) - py(a): z = pz(b) - pz(a)
      l = SQR(x * x + y * y + z * z)
      tx(i) = x / l: ty(i) = y / l: tz(i) = z / l
    NEXT
    kmax = 0
    FOR i = 0 TO NSEG - 1
      a = (i + NSEG - 1) MOD NSEG: b = (i + 1) MOD NSEG
      ' the flat right: direction x up
      hx = -tz(i): hz = tx(i): l = SQR(hx * hx + hz * hz): hx = hx / l: hz = hz / l
      cv(i) = ((tx(b) - tx(a)) * hx + (tz(b) - tz(a)) * hz) / (2 * seglen)
      IF ABS(cv(i)) > kmax THEN kmax = ABS(cv(i))
    NEXT
    IF kmax < 1 / 20 THEN EXIT DO
    amp = amp * .85                             ' (a turn too tight for the track's width: gentler)
  LOOP
  ' the curvature smoothed (the banking follows it)
  FOR i = 0 TO NSEG - 1: dl(i) = 0: FOR k = -5 TO 5: dl(i) = dl(i) + cv((i + k + NSEG) MOD NSEG): NEXT: dl(i) = dl(i) / 11: NEXT
  FOR i = 0 TO NSEG - 1: cv(i) = dl(i): NEXT
  ' the frames: the up tilted towards the inside of the turn
  groundY = 1E+09
  FOR i = 0 TO NSEG - 1
    hx = -tz(i): hz = tx(i): l = SQR(hx * hx + hz * hz): hx = hx / l: hz = hz / l
    bank = cv(i) * 14: IF bank > .55 THEN bank = .55
    IF bank < -.55 THEN bank = -.55
    upx = hx * bank: upy = 1: upz = hz * bank
    ' right = direction x up, then up = right x direction
    x = ty(i) * upz - tz(i) * upy: y = tz(i) * upx - tx(i) * upz: z = tx(i) * upy - ty(i) * upx
    l = SQR(x * x + y * y + z * z): rx(i) = x / l: ry(i) = y / l: rz(i) = z / l
    ux(i) = ry(i) * tz(i) - rz(i) * ty(i): uy(i) = rz(i) * tx(i) - rx(i) * tz(i): uz(i) = rx(i) * ty(i) - ry(i) * tx(i)
    IF py(i) < groundY THEN groundY = py(i)
  NEXT
  groundY = groundY - 26
  ' tunnels, pads, arches
  FOR i = 0 TO NSEG - 1: kind(i) = 0: lane(i) = 0: NEXT
  nt = 1 + INT(RND * 2)
  FOR k = 1 TO nt
    a = 60 + INT(RND * (NSEG - 140)): b = a + 22 + INT(RND * 22)
    FOR i = a TO b: kind(i MOD NSEG) = kind(i MOD NSEG) OR 1: NEXT
  NEXT
  FOR i = 24 TO NSEG - 12 STEP 30 + INT(RND * 6)
    IF (kind(i) AND 1) = 0 THEN kind(i) = kind(i) OR 8
  NEXT
  kind(0) = kind(0) OR 8
  i = 18
  DO WHILE i < NSEG - 10
    IF ABS(cv(i)) < .02 AND (kind(i) AND 9) = 0 THEN
      IF RND < .55 THEN kk = 2 ELSE kk = 4
      ln = INT(RND * 3) - 1
      kind(i) = kind(i) OR kk: lane(i) = ln: kind(i + 1) = kind(i + 1) OR kk: lane(i + 1) = ln
      i = i + 14 + INT(RND * 18)
    ELSE
      i = i + 3
    END IF
  LOOP
  ' the edges and the walls' tops
  FOR i = 0 TO NSEG
    j = i MOD NSEG
    ex1(i) = px(j) - rx(j) * HW: ey1(i) = py(j) - ry(j) * HW: ez1(i) = pz(j) - rz(j) * HW
    ex2(i) = px(j) + rx(j) * HW: ey2(i) = py(j) + ry(j) * HW: ez2(i) = pz(j) + rz(j) * HW
    wx1(i) = ex1(i) + ux(j) * WALLH - rx(j) * .3: wy1(i) = ey1(i) + uy(j) * WALLH - ry(j) * .3: wz1(i) = ez1(i) + uz(j) * WALLH - rz(j) * .3
    wx2(i) = ex2(i) + ux(j) * WALLH + rx(j) * .3: wy2(i) = ey2(i) + uy(j) * WALLH + ry(j) * .3: wz2(i) = ez2(i) + uz(j) * WALLH + rz(j) * .3
  NEXT
  ' the city: blocks along the track, never on it
  nbld = 0
  FOR i = 0 TO NSEG - 1 STEP 5
    IF nbld < MAXBLD AND RND < .85 THEN
      sd0 = 1: IF RND < .5 THEN sd0 = -1
      offs = HW + 9 + RND * 34
      x = px(i) + rx(i) * offs * sd0: z = pz(i) + rz(i) * offs * sd0
      w = 4 + RND * 9
      ok = -1
      FOR k = 0 TO NSEG - 1 STEP 2
        IF (px(k) - x) ^ 2 + (pz(k) - z) ^ 2 < (HW + w * .75 + 4) ^ 2 THEN ok = 0: EXIT FOR
      NEXT
      IF ok THEN
        nbld = nbld + 1
        bsx(nbld) = x: bsz(nbld) = z: bw(nbld) = w: bh(nbld) = 8 + RND * RND * 70
        bsy(nbld) = groundY: bseg(nbld) = i
        h = INT(RND * 4)
        IF h = 0 THEN bcol(nbld) = RGB(60, 70, 110) ELSE IF h = 1 THEN bcol(nbld) = RGB(90, 60, 120) ELSE IF h = 2 THEN bcol(nbld) = RGB(50, 90, 110) ELSE bcol(nbld) = RGB(110, 100, 120)
      END IF
    END IF
  NEXT
  ' the mountains on the horizon
  FOR i = 0 TO 47: mty(i) = 40 + RND * 120 + 60 * (i MOD 2): NEXT
  ' the small map
  mx0 = 1E+09: mx1 = -1E+09: mz0 = 1E+09: mz1 = -1E+09
  FOR i = 0 TO NSEG - 1
    IF px(i) < mx0 THEN mx0 = px(i)
    IF px(i) > mx1 THEN mx1 = px(i)
    IF pz(i) < mz0 THEN mz0 = pz(i)
    IF pz(i) > mz1 THEN mz1 = pz(i)
  NEXT
  sc = 2 / MAX(mx1 - mx0, mz1 - mz0)
  FOR k = 0 TO 59
    i = k * 8
    mmx(k) = (px(i) - (mx0 + mx1) / 2) * sc: mmy(k) = (pz(i) - (mz0 + mz1) / 2) * sc
  NEXT
  mmx(60) = mmx(0): mmy(60) = mmy(0)
  RANDOMIZE TIMER
END SUB

' Where a distance along the circuit is, d a lateral offset, h a height: wx, wy, wz; and its frame
' (ftx.. fux.., the curvature, the kind of the segment).
SUB TrackAt (s, d, h)
  sw = s - INT(s / tlen) * tlen
  f = sw / seglen: i = INT(f): f = f - i
  IF i >= NSEG THEN i = 0
  j = i + 1: IF j >= NSEG THEN j = 0
  g = 1 - f
  ftx = tx(i) * g + tx(j) * f: fty = ty(i) * g + ty(j) * f: ftz = tz(i) * g + tz(j) * f
  frx = rx(i) * g + rx(j) * f: fry = ry(i) * g + ry(j) * f: frz = rz(i) * g + rz(j) * f
  fux = ux(i) * g + ux(j) * f: fuy = uy(i) * g + uy(j) * f: fuz = uz(i) * g + uz(j) * f
  fcv = cv(i) * g + cv(j) * f: fkind = kind(i): flane = lane(i)
  wx = px(i) * g + px(j) * f + frx * d + fux * h
  wy = py(i) * g + py(j) * f + fry * d + fuy * h
  wz = pz(i) * g + pz(j) * f + frz * d + fuz * h
END SUB

' ================================================================================================
' Drawing a frame
' ================================================================================================
SUB DrawWorld (camS)
  SCENE3D RGB(8, 6, 24)
  CAMERA3D cex, cey, cez, ctx, cty, ctz, 70
  ' the camera's right and up (the glows face the camera)
  fx = ctx - cex: fy = cty - cey: fz = ctz - cez: l = SQR(fx * fx + fy * fy + fz * fz): fx = fx / l: fy = fy / l: fz = fz / l
  crx = -fz: cry = 0: crz = fx: l = SQR(crx * crx + crz * crz): crx = crx / l: crz = crz / l
  cux = cry * fz - crz * fy: cuy = crz * fx - crx * fz: cuz = crx * fy - cry * fx

  ' the sky: a ring of quads around the eye, from the horizon's glow to the night; the mountains; the sun
  LIGHT3D 0, 0, 0: DEPTH3D 0: CULL3D 0: TEXTURE3D 0
  FOR k = 0 TO 23
    a0 = k * PI / 12: a1 = (k + 1) * PI / 12
    x0 = 800 * COS(a0): z0 = 800 * SIN(a0): x1 = 800 * COS(a1): z1 = 800 * SIN(a1)
    COLOR3D RGB(120, 40, 90): VERTEX3D cex + x0, cey - 60, cez + z0
    COLOR3D RGB(120, 40, 90): VERTEX3D cex + x1, cey - 60, cez + z1
    COLOR3D RGB(10, 8, 30): VERTEX3D cex + x1, cey + 400, cez + z1
    COLOR3D RGB(120, 40, 90): VERTEX3D cex + x0, cey - 60, cez + z0
    COLOR3D RGB(10, 8, 30): VERTEX3D cex + x1, cey + 400, cez + z1
    COLOR3D RGB(10, 8, 30): VERTEX3D cex + x0, cey + 400, cez + z0
    COLOR3D RGB(70, 20, 60): VERTEX3D cex + x0, cey - 300, cez + z0
    VERTEX3D cex + x1, cey - 300, cez + z1: VERTEX3D cex + x1, cey - 60, cez + z1
    VERTEX3D cex + x0, cey - 300, cez + z0: VERTEX3D cex + x1, cey - 60, cez + z1: VERTEX3D cex + x0, cey - 60, cez + z0
  NEXT
  ' the sun: a glow low in the sky, then the mountains in front of it
  TEXTURE3D tex.glow, 1: BLEND3D 2
  sx = cex + 700: sy = cey + 60: sz = cez - 300
  COLOR3D RGB(255, 140, 90)
  Billboard sx, sy, sz, 260
  COLOR3D RGB(255, 230, 160)
  Billboard sx, sy, sz, 90
  BLEND3D 0: TEXTURE3D 0
  FOR k = 0 TO 47
    a0 = k * PI / 24: a1 = (k + 1) * PI / 24: am = (a0 + a1) / 2
    COLOR3D RGB(24, 12, 44)
    VERTEX3D cex + 760 * COS(a0), cey - 60, cez + 760 * SIN(a0)
    VERTEX3D cex + 760 * COS(a1), cey - 60, cez + 760 * SIN(a1)
    COLOR3D RGB(60, 26, 80)
    VERTEX3D cex + 760 * COS(am), cey - 60 + mty(k), cez + 760 * SIN(am)
  NEXT
  DEPTH3D 1

  ' the ground's grid around the eye
  gx = INT(cex / 40) * 40: gz = INT(cez / 40) * 40
  TEXTURE3D tex.ground, 0, 0: COLOR3D RGB(255, 255, 255)
  VERTEX3D gx - 900, groundY, gz - 900, 0, 0: VERTEX3D gx - 900, groundY, gz + 900, 0, 45: VERTEX3D gx + 900, groundY, gz + 900, 45, 45
  VERTEX3D gx - 900, groundY, gz - 900, 0, 0: VERTEX3D gx + 900, groundY, gz + 900, 45, 45: VERTEX3D gx + 900, groundY, gz - 900, 45, 0

  c0 = INT(camS / seglen) - 4
  ' the track's floor
  TEXTURE3D tex.track, 1, 0
  FOR k = 0 TO vdist
    i = (c0 + k + NSEG * 4) MOD NSEG: j = i + 1
    f0 = Fade(k): f1 = Fade(k + 1)
    t0 = (i MOD 8) * .5: t1 = t0 + .5
    COLOR3D f0: VERTEX3D ex1(i), ey1(i), ez1(i), 0, t0
    VERTEX3D ex2(i), ey2(i), ez2(i), 1, t0
    COLOR3D f1: VERTEX3D ex2(j), ey2(j), ez2(j), 1, t1
    COLOR3D f0: VERTEX3D ex1(i), ey1(i), ez1(i), 0, t0
    COLOR3D f1: VERTEX3D ex2(j), ey2(j), ez2(j), 1, t1
    VERTEX3D ex1(j), ey1(j), ez1(j), 0, t1
    ' under the floor (seen from below in the dives)
    COLOR3D RGB(18, 18, 30)
    VERTEX3D ex1(i), ey1(i) - .6, ez1(i): VERTEX3D ex2(j), ey2(j) - .6, ez2(j): VERTEX3D ex2(i), ey2(i) - .6, ez2(i)
    VERTEX3D ex1(i), ey1(i) - .6, ez1(i): VERTEX3D ex1(j), ey1(j) - .6, ez1(j): VERTEX3D ex2(j), ey2(j) - .6, ez2(j)
  NEXT
  ' the pads (a little above the floor)
  FOR pass = 0 TO 1
    IF pass = 0 THEN TEXTURE3D tex.boost, 1: want = 2 ELSE TEXTURE3D tex.weapon, 1: want = 4
    FOR k = 0 TO vdist
      i = (c0 + k + NSEG * 4) MOD NSEG
      IF kind(i) AND want THEN
        j = i + 1
        a = lane(i) * 3 - 1.5: b = a + 3
        COLOR3D Fade(k)
        TrackAt i * seglen, a, .05: x0 = wx: y0 = wy: z0 = wz
        TrackAt i * seglen, b, .05: x1 = wx: y1 = wy: z1 = wz
        TrackAt j * seglen, b, .05: x2 = wx: y2 = wy: z2 = wz
        TrackAt j * seglen, a, .05: x3 = wx: y3 = wy: z3 = wz
        VERTEX3D x0, y0, z0, 0, 1: VERTEX3D x1, y1, z1, 1, 1: VERTEX3D x2, y2, z2, 1, 0
        VERTEX3D x0, y0, z0, 0, 1: VERTEX3D x2, y2, z2, 1, 0: VERTEX3D x3, y3, z3, 0, 0
      END IF
    NEXT
  NEXT
  ' the walls (both faces)
  CULL3D 0: TEXTURE3D tex.wall, 1, 0
  FOR k = 0 TO vdist
    i = (c0 + k + NSEG * 4) MOD NSEG: j = i + 1
    f0 = Fade(k): f1 = Fade(k + 1)
    t0 = (i MOD 4) * .25: t1 = t0 + .25
    COLOR3D f0: VERTEX3D ex1(i), ey1(i), ez1(i), t0, 1: VERTEX3D wx1(i), wy1(i), wz1(i), t0, 0
    COLOR3D f1: VERTEX3D wx1(j), wy1(j), wz1(j), t1, 0
    COLOR3D f0: VERTEX3D ex1(i), ey1(i), ez1(i), t0, 1
    COLOR3D f1: VERTEX3D wx1(j), wy1(j), wz1(j), t1, 0: VERTEX3D ex1(j), ey1(j), ez1(j), t1, 1
    COLOR3D f0: VERTEX3D ex2(i), ey2(i), ez2(i), t0, 1: VERTEX3D wx2(i), wy2(i), wz2(i), t0, 0
    COLOR3D f1: VERTEX3D wx2(j), wy2(j), wz2(j), t1, 0
    COLOR3D f0: VERTEX3D ex2(i), ey2(i), ez2(i), t0, 1
    COLOR3D f1: VERTEX3D wx2(j), wy2(j), wz2(j), t1, 0: VERTEX3D ex2(j), ey2(j), ez2(j), t1, 1
  NEXT
  ' the tunnels' vaults, the arches
  TEXTURE3D 0
  FOR k = 0 TO vdist
    i = (c0 + k + NSEG * 4) MOD NSEG: j = i + 1
    IF kind(i) AND 1 THEN
      sh = 1 - k / (vdist + 8)
      FOR q = 0 TO 3
        a0 = q * PI / 4: a1 = (q + 1) * PI / 4
        d0 = -HW * 1.1 * COS(a0): h0 = WALLH + 6 * SIN(a0): d1 = -HW * 1.1 * COS(a1): h1 = WALLH + 6 * SIN(a1)
        COLOR3D RGB((34 + q * 6) * sh, (30 + q * 5) * sh, (52 + q * 9) * sh)
        TrackAt i * seglen, d0, h0: x0 = wx: y0 = wy: z0 = wz
        TrackAt i * seglen, d1, h1: x1 = wx: y1 = wy: z1 = wz
        TrackAt j * seglen, d1, h1: x2 = wx: y2 = wy: z2 = wz
        TrackAt j * seglen, d0, h0: x3 = wx: y3 = wy: z3 = wz
        VERTEX3D x0, y0, z0: VERTEX3D x1, y1, z1: VERTEX3D x2, y2, z2
        VERTEX3D x0, y0, z0: VERTEX3D x2, y2, z2: VERTEX3D x3, y3, z3
      NEXT
    END IF
    IF kind(i) AND 8 THEN DrawArch i, k
  NEXT
  ' the city (lit blocks)
  LIGHT3D .4, 1, .3, 35: CULL3D 1
  FOR b = 1 TO nbld
    dd = (bseg(b) - c0 + NSEG * 4) MOD NSEG
    IF dd < vdist + 10 THEN
      PUSH3D
      TRANSLATE3D bsx(b), bsy(b) + bh(b) / 2, bsz(b)
      SCALE3D bw(b), bh(b), bw(b)
      COLOR3D bcol(b)
      CUBE3D 1
      POP3D
    END IF
  NEXT
  LIGHT3D 0, 0, 0
  ' the craft
  CULL3D 0
  FOR n = 0 TO NSHIP - 1
    dd = sd(n) - camS: dd = dd - tlen * INT(dd / tlen + .5)
    IF dd > -25 AND dd < vdist * seglen THEN DrawShip n
  NEXT
  ' the lights: the neon along the walls, the engines, the shots, the mines
  TEXTURE3D tex.glow, 1: BLEND3D 2: DEPTH3D 1, 0
  FOR k = 0 TO vdist STEP 2
    i = (c0 + k + NSEG * 4) MOD NSEG
    sh = 1 - k / (vdist + 4)
    COLOR3D RGB(60 * sh, 170 * sh, 255 * sh)
    Billboard wx1(i), wy1(i), wz1(i), 1.1
    Billboard wx2(i), wy2(i), wz2(i), 1.1
  NEXT
  FOR k = 0 TO vdist STEP 3
    i = (c0 + k + NSEG * 4) MOD NSEG
    IF kind(i) AND 1 THEN
      sh = 1 - k / (vdist + 4)
      TrackAt i * seglen, 0, WALLH + 5.6
      COLOR3D RGB(200 * sh, 120 * sh, 255 * sh): Billboard wx, wy, wz, 1.6
    END IF
  NEXT
  FOR n = 0 TO NSHIP - 1
    dd = sd(n) - camS: dd = dd - tlen * INT(dd / tlen + .5)
    IF dd > -25 AND dd < vdist * seglen THEN
      TrackAt sd(n) - 1.3, lx(n), .7 + .06 * SIN(t * 6 + n)
      g = 120 + thrust(n) * 120: IF turbo(n) > 0 THEN g = 255
      COLOR3D RGB(g, g * .7, 255): Billboard wx, wy, wz, .9 + thrust(n) * .5 + turbo(n) * 1.5
      IF shield(n) > 0 THEN
        TrackAt sd(n), lx(n), .8
        COLOR3D RGB(40, 90, 200): Billboard wx, wy, wz, 3.2
      END IF
    END IF
  NEXT
  FOR p = 0 TO NPROJ - 1
    IF pk(p) THEN
      dd = ps(p) - camS: dd = dd - tlen * INT(dd / tlen + .5)
      IF dd > -10 AND dd < vdist * seglen THEN
        TrackAt ps(p), pl(p), .7
        IF pk(p) = 3 THEN
          b = .5 + .5 * SIN(t * 10 + p): COLOR3D RGB(255, 60 + 120 * b, 30): Billboard wx, wy, wz, 1.4
        ELSEIF pk(p) = 2 THEN
          COLOR3D RGB(255, 200, 80): Billboard wx, wy, wz, 1.6
        ELSEIF pk(p) = 9 THEN
          b = plife(p) * 2: COLOR3D RGB(255 * b, 160 * b, 60 * b): Billboard wx, wy, wz, 6 - b * 3
        ELSE
          COLOR3D RGB(255, 120, 60): Billboard wx, wy, wz, 1.2
        END IF
      END IF
    END IF
  NEXT
  BLEND3D 0: DEPTH3D 1: CULL3D 1: TEXTURE3D 0
  RENDER3D
END SUB

' A square facing the camera, centred on (x, y, z), of half-size s (the texture: tex.glow).
SUB Billboard (x, y, z, s)
  ax = (crx + cux) * s: ay = (cry + cuy) * s: az = (crz + cuz) * s
  bx = (crx - cux) * s: by = (cry - cuy) * s: bz = (crz - cuz) * s
  VERTEX3D x - ax, y - ay, z - az, 0, 0: VERTEX3D x + bx, y + by, z + bz, 1, 0: VERTEX3D x + ax, y + ay, z + az, 1, 1
  VERTEX3D x - ax, y - ay, z - az, 0, 0: VERTEX3D x + ax, y + ay, z + az, 1, 1: VERTEX3D x - bx, y - by, z - bz, 0, 1
END SUB

' Far away is darker (the night's haze).
FUNCTION Fade (k)
  f = 1 - (k / vdist) ^ 1.6 * .85
  IF f < .1 THEN f = .1
  c = 255 * f
  Fade = RGB(c, c, c)
END FUNCTION

' An arch over the track: two posts and a beam, neon coloured; the start's has the chequered banner.
SUB DrawArch (i, k)
  sh = 1 - k / (vdist + 8)
  IF i = 0 THEN c = RGB(240 * sh, 240 * sh, 255 * sh) ELSE c = RGB(255 * sh, 60 * sh, 170 * sh)
  COLOR3D c
  FOR side = -1 TO 1 STEP 2
    d = side * (HW + .6)
    TrackAt i * seglen, d - .35, 0: x0 = wx: y0 = wy: z0 = wz
    TrackAt i * seglen, d + .35, 0: x1 = wx: y1 = wy: z1 = wz
    TrackAt i * seglen, d + .35, 7: x2 = wx: y2 = wy: z2 = wz
    TrackAt i * seglen, d - .35, 7: x3 = wx: y3 = wy: z3 = wz
    VERTEX3D x0, y0, z0: VERTEX3D x1, y1, z1: VERTEX3D x2, y2, z2
    VERTEX3D x0, y0, z0: VERTEX3D x2, y2, z2: VERTEX3D x3, y3, z3
  NEXT
  IF i = 0 THEN TEXTURE3D tex.check, 0, 0: COLOR3D RGB(255 * sh, 255 * sh, 255 * sh)
  TrackAt i * seglen, -HW - 1, 6: x0 = wx: y0 = wy: z0 = wz
  TrackAt i * seglen, HW + 1, 6: x1 = wx: y1 = wy: z1 = wz
  TrackAt i * seglen, HW + 1, 7.6: x2 = wx: y2 = wy: z2 = wz
  TrackAt i * seglen, -HW - 1, 7.6: x3 = wx: y3 = wy: z3 = wz
  VERTEX3D x0, y0, z0, 0, 1: VERTEX3D x1, y1, z1, 4, 1: VERTEX3D x2, y2, z2, 4, 0
  VERTEX3D x0, y0, z0, 0, 1: VERTEX3D x2, y2, z2, 4, 0: VERTEX3D x3, y3, z3, 0, 0
  IF i = 0 THEN TEXTURE3D 0
END SUB

' ================================================================================================
' The craft
' ================================================================================================
SUB MakeModel
  RESTORE Model
  FOR i = 1 TO 11: READ mvx(i), mvy(i), mvz(i): NEXT
  FOR i = 1 TO 14: READ mt(i * 3 - 2), mt(i * 3 - 1), mt(i * 3), mshade(i): NEXT
END SUB

SUB DrawShip (n)
  hov = .7 + .06 * SIN(t * 6 + n)
  TrackAt sd(n), lx(n), hov
  shx(n) = wx: shy(n) = wy: shz(n) = wz
  ' the craft's frame: the track's, turned by its drift (yaw) and its lean (roll), spinning when hit
  yaw = vl(n) / (ABS(v(n)) + 15): IF yaw > .5 THEN yaw = .5
  IF yaw < -.5 THEN yaw = -.5
  roll = steer(n) * .35 + spin(n) * 9
  cy = COS(yaw): sy = SIN(yaw)
  ax = ftx * cy + frx * sy: ay = fty * cy + fry * sy: az = ftz * cy + frz * sy          ' forward
  bx = frx * cy - ftx * sy: by = fry * cy - fty * sy: bz = frz * cy - ftz * sy          ' right
  cr = COS(roll): sr = SIN(roll)
  qx = bx * cr + fux * sr: qy = by * cr + fuy * sr: qz = bz * cr + fuz * sr              ' right, leaning
  ox = fux * cr - bx * sr: oy = fuy * cr - by * sr: oz = fuz * cr - bz * sr              ' up, leaning
  c1 = col1(n): c2 = col2(n)
  FOR f = 1 TO 14
    s = mshade(f)
    IF s = 1 THEN c = c1 ELSE IF s = 2 THEN c = c2 ELSE IF s = 3 THEN c = RGB(28, 30, 40) ELSE c = RGB(30, 40, 60)
    COLOR3D c
    FOR k = 0 TO 2
      m = mt(f * 3 - 2 + k)
      x = mvx(m): y = mvy(m): z = mvz(m)
      VERTEX3D wx + qx * x + ox * y + ax * z, wy + qy * x + oy * y + ay * z, wz + qz * x + oz * y + az * z
    NEXT
  NEXT
END SUB

' ================================================================================================
' The race
' ================================================================================================
FUNCTION Human (n)
  Human = (n < np AND NOT demo)
END FUNCTION

SUB SetupGrid
  vmax = 95: IF klass = 1 THEN vmax = 122
  ' the order on the grid: at random
  FOR n = 0 TO NSHIP - 1: order(n) = n: NEXT
  FOR n = NSHIP - 1 TO 1 STEP -1: k = INT(RND * (n + 1)): SWAP order(n), order(k): NEXT
  FOR g = 0 TO NSHIP - 1
    n = order(g)
    sd(n) = -6 - g * 4.5: lx(n) = 2.2 * (1 - 2 * (g MOD 2))
    v(n) = 0: vl(n) = 0: steer(n) = 0: thrust(n) = 0: wep(n) = 0: shield(n) = 0: turbo(n) = 0: spin(n) = 0
    arrived(n) = 0: fintime(n) = 0: lapst(n) = 0: bestlap(n) = 0: lastlap(n) = 0: firewait(n) = 0
    skill(n) = .9 + RND * .1: aioff(n) = (RND - .5) * 2
    IF Human(n) THEN skill(n) = 1: pilot$(n) = T$("P") + LTRIM$(STR$(n + 1)) ELSE pilot$(n) = aiName$(n)
  NEXT
  FOR p = 0 TO NPROJ - 1: pk(p) = 0: NEXT
  racet = 0: recordNew = 0
END SUB

SUB Race
  SetupGrid
  state = 1: cdown = 3.99: paused = 0: lastBeep = 4
  ' the pads: the players take them in order (the first player has the keyboard too)
  k = 0
  FOR i = 0 TO MAXP - 1: padslot(i) = -1: NEXT
  FOR i = 0 TO 3
    IF PADS(i) AND k < MAXP THEN padslot(k) = i: k = k + 1
  NEXT
  Layout
  FOR i = 0 TO np - 1: pcamL(i) = lx(i) * .6: pcamH(i) = 2.9: pmsgt(i) = 0: kfire(i) = 0: NEXT
  last = TIMER
  DO
    SCREEN , , 1, 0                 ' (drawn on page 1, out of sight: PCOPY shows the whole frame at once)
    now = TIMER: dt = now - last: last = now
    IF dt < 0 OR dt > .05 THEN dt = .05
    k$ = UCASE$(INKEY$)
    p = PAD
    IF k$ = "F" THEN fs = NOT fs: IF fs THEN FULLSCREEN ON ELSE FULLSCREEN OFF
    IF state = 3 THEN
      IF k$ = CHR$(13) OR k$ = CHR$(27) OR ((p AND 16) <> 0 AND (padprev AND 16) = 0) OR ((p AND 8192) <> 0 AND (padprev AND 8192) = 0) THEN padprev = p: EXIT DO
    ELSEIF k$ = "P" OR k$ = CHR$(27) OR ((p AND 8192) <> 0 AND (padprev AND 8192) = 0) THEN
      paused = NOT paused: menuSel = 0
      IF paused THEN SoundsOff
    ELSEIF paused THEN
      IF k$ = CHR$(0) + "H" OR k$ = CHR$(0) + "P" OR ((p AND 3) <> 0 AND (padprev AND 3) = 0) THEN menuSel = 1 - menuSel
      IF k$ = CHR$(13) OR ((p AND 16) <> 0 AND (padprev AND 16) = 0) THEN
        IF menuSel = 1 THEN padprev = p: SoundsOff: EXIT DO
        paused = 0
      END IF
    END IF
    padprev = p
    IF NOT paused THEN
      t = t + dt
      IF state = 1 THEN
        cdown = cdown - dt
        IF INT(cdown) < lastBeep AND cdown > 0 THEN lastBeep = INT(cdown): Sfx 10, 440, 0, 160, .15
        IF cdown <= 0 THEN
          state = 2: Sfx 10, 880, 0, 180, .35
          FOR i = 0 TO np - 1: Say i, T$("GO"), 1: NEXT
        END IF
      ELSE
        racet = racet + dt
      END IF
      Controls
      Physics
      Shots
      MusicTick
      Engine
      ' the race ends when every player is through the line
      IF state = 2 THEN
        done = -1
        FOR i = 0 TO np - 1: IF arrived(i) = 0 THEN done = 0
        NEXT
        IF done THEN
          state = 3
          FOR n = 0 TO NSHIP - 1
            IF arrived(n) = 0 THEN fintime(n) = racet + (laps * tlen - sd(n)) / (v(n) + 40)
          NEXT
          SaveTimes
        END IF
      END IF
    END IF
    ' each player's view, then what is over them all
    FOR i = 0 TO np - 1
      VIEW3D vx(i), vy(i), vw(i), vh(i)
      Camera i
      DrawWorld sd(i) - 7.5
      Hud i
    NEXT
    VIEW3D
    Overlay
    SfxTick
    PCOPY 1, 0
    PAUSE 1
  LOOP
  SoundsOff
END SUB

' The players' parts of the screen (640 x 480): one, two above each other, four corners (three: the
' fourth corner has the map and the order); the farther a view draws, the costlier.
SUB Layout
  FOR i = 0 TO MAXP - 1: vx(i) = 0: vy(i) = 0: vw(i) = 640: vh(i) = 480: NEXT
  IF np = 2 THEN
    vh(0) = 239: vy(1) = 241: vh(1) = 239
  ELSEIF np >= 3 THEN
    FOR i = 0 TO 3: vx(i) = (i MOD 2) * 321: vy(i) = (i \ 2) * 241: vw(i) = 319: vh(i) = 239: NEXT
  END IF
  IF np = 1 THEN vdist = 66 ELSE IF np = 2 THEN vdist = 52 ELSE vdist = 42
END SUB

' A word in the middle of a player's view for a while.
SUB Say (i, a$, secs)
  IF i < 0 OR i >= np OR demo THEN EXIT SUB
  pmsg$(i) = a$: pmsgt(i) = secs
END SUB

' The player's controls, the others' minds.
SUB Controls
  FOR n = 0 TO NSHIP - 1
    IF state = 1 THEN
      steer(n) = 0: thrust(n) = 0
    ELSEIF Human(n) AND arrived(n) = 0 AND NOT autoP THEN
      p = 0: q = padslot(n)
      IF q >= 0 THEN p = PAD(q)
      kb = (n = 0)
      s = 0
      IF (kb AND KEYDOWN("LEFT")) OR (p AND 4) THEN s = s - 1
      IF (kb AND KEYDOWN("RIGHT")) OR (p AND 8) THEN s = s + 1
      IF q = 0 OR q = 1 THEN st = STICK(q * 2): IF ABS(st - 100) > 15 THEN s = (st - 100) / 99
      steer(n) = s
      IF (kb AND (KEYDOWN("UP") OR KEYDOWN("X"))) OR (p AND 16) THEN thrust(n) = 1 ELSE thrust(n) = 0
      bl = (kb AND KEYDOWN("Z")) OR (p AND 256): br = (kb AND KEYDOWN("C")) OR (p AND 512)
      IF bl THEN steer(n) = steer(n) - .8
      IF br THEN steer(n) = steer(n) + .8
      IF bl OR br THEN v(n) = v(n) - v(n) * .5 * dt
      f = (kb AND KEYDOWN("SPACE")) OR (p AND 64)
      IF f AND kfire(n) = 0 THEN FireWeapon n
      kfire(n) = f
    ELSE
      Think n
    END IF
  NEXT
END SUB

SUB Think (n)
  ' look ahead: the inside of the coming turn, a little of one's own line, and around the craft ahead
  TrackAt sd(n) + 22 + v(n) * .25, 0, 0
  ka = fcv
  tg = ka * 70 + aioff(n): IF tg > HW - 1.4 THEN tg = HW - 1.4
  IF tg < -HW + 1.4 THEN tg = -HW + 1.4
  FOR m = 0 TO NSHIP - 1
    IF m <> n THEN
      d = sd(m) - sd(n): d = d - tlen * INT(d / tlen + .5)
      IF d > 0 AND d < 9 AND ABS(lx(m) - tg) < 1.8 THEN
        IF lx(m) > 0 THEN tg = lx(m) - 2.2 ELSE tg = lx(m) + 2.2
      END IF
    END IF
  NEXT
  ' a pad on the way: go for it
  TrackAt sd(n) + 14, 0, 0
  IF (fkind AND 2) OR ((fkind AND 4) AND wep(n) = 0) THEN tg = flane * 3
  s = (tg - lx(n)) * .55 - vl(n) * .3
  IF s > 1 THEN s = 1
  IF s < -1 THEN s = -1
  steer(n) = s
  ' the speed the turn allows
  lim = vmax * skill(n) * (1 + turbo(n) * 0)
  IF ABS(ka) > .001 THEN vt = SQR(62 / (.1 * ABS(ka))): IF vt < lim THEN lim = vt
  ' a little rubber band with the leading player
  IF NOT Human(n) AND NOT demo THEN
    lead = sd(0): FOR i = 1 TO np - 1: IF sd(i) > lead THEN lead = sd(i)
    NEXT
    d = sd(n) - lead
    IF d > 120 THEN lim = lim * .94
    IF d < -120 THEN lim = lim * 1.06
  END IF
  IF v(n) < lim THEN thrust(n) = 1 ELSE thrust(n) = 0: v(n) = v(n) - (v(n) - lim) * 1.2 * dt
  ' the weapons
  IF wep(n) THEN
    firewait(n) = firewait(n) - dt
    IF firewait(n) < 0 THEN
      w = wep(n)
      IF w = 4 OR w = 5 THEN
        FireWeapon n
      ELSE
        tgt = Ahead(n, 70)
        IF w = 3 THEN
          IF Behind(n, 40) >= 0 THEN FireWeapon n
        ELSEIF tgt >= 0 THEN
          FireWeapon n
        END IF
      END IF
      firewait(n) = .3 + RND
    END IF
  END IF
END SUB

' The craft just ahead of n within dmax (its number), or -1; the one just behind.
FUNCTION Ahead (n, dmax)
  bd = dmax: Ahead = -1
  FOR m = 0 TO NSHIP - 1
    IF m <> n THEN
      d = sd(m) - sd(n): d = d - tlen * INT(d / tlen + .5)
      IF d > 0 AND d < bd THEN bd = d: Ahead = m
    END IF
  NEXT
END FUNCTION
FUNCTION Behind (n, dmax)
  bd = dmax: Behind = -1
  FOR m = 0 TO NSHIP - 1
    IF m <> n THEN
      d = sd(n) - sd(m): d = d - tlen * INT(d / tlen + .5)
      IF d > 0 AND d < bd THEN bd = d: Behind = m
    END IF
  NEXT
END FUNCTION

SUB Physics
  FOR n = 0 TO NSHIP - 1
    TrackAt sd(n), lx(n), 0
    top = vmax * skill(n)
    IF turbo(n) > 0 THEN top = top * 1.45: turbo(n) = turbo(n) - dt
    IF turbo(n) < 0 THEN turbo(n) = 0
    IF thrust(n) THEN
      v(n) = v(n) + (top - v(n)) * .75 * dt
    ELSE
      v(n) = v(n) - v(n) * .45 * dt
    END IF
    IF v(n) > top THEN v(n) = v(n) - (v(n) - top) * 1.5 * dt
    ' downhill faster, uphill slower
    v(n) = v(n) - fty * 18 * dt
    IF v(n) < 0 THEN v(n) = 0
    ' steering, the turn pushing out, the air's damping
    vl(n) = vl(n) + steer(n) * 46 * dt - fcv * v(n) * v(n) * .1 * dt
    vl(n) = vl(n) - vl(n) * 3 * dt
    lx(n) = lx(n) + vl(n) * dt
    ' the walls
    lim = HW - .95
    IF ABS(lx(n)) > lim THEN
      lx(n) = lim * SGN(lx(n)): vl(n) = -vl(n) * .35
      v(n) = v(n) - v(n) * 1.6 * dt
      IF Human(n) AND state = 2 THEN Sfx 11, 90 + RND * 40, 4, 70, .08
    END IF
    sold = sd(n)
    sd(n) = sd(n) + v(n) * dt
    IF spin(n) > 0 THEN spin(n) = spin(n) - dt * 1.4: IF spin(n) < 0 THEN spin(n) = 0
    IF shield(n) > 0 THEN shield(n) = shield(n) - dt: IF shield(n) < 0 THEN shield(n) = 0
    ' the pads
    IF (fkind AND 6) AND ABS(lx(n) - flane * 3) < 1.7 THEN
      IF fkind AND 2 THEN
        IF turbo(n) < .45 THEN turbo(n) = .45: v(n) = v(n) + 10
        IF Human(n) THEN Sfx 12, 220, 2, 150, .3
      ELSEIF wep(n) = 0 THEN
        wep(n) = 1 + INT(RND * 5): firewait(n) = .5 + RND
        IF Human(n) THEN Sfx 12, 660, 1, 150, .12: Say n, wname$(wep(n)), 1.2
      END IF
    END IF
    ' a lap
    IF INT(sd(n) / tlen) > INT(sold / tlen) AND sd(n) > 0 AND arrived(n) = 0 THEN
      lt = racet - lapst(n): lastlap(n) = lt
      IF INT(sold / tlen) >= 0 THEN
        IF bestlap(n) = 0 OR lt < bestlap(n) THEN bestlap(n) = lt
      END IF
      lapst(n) = racet
      IF sd(n) >= laps * tlen THEN arrived(n) = -1: fintime(n) = racet
      IF Human(n) AND arrived(n) = 0 AND INT(sold / tlen) >= 0 THEN Sfx 12, 523, 1, 140, .25
    END IF
  NEXT
  ' the craft against each other
  FOR a = 0 TO NSHIP - 2
    FOR b = a + 1 TO NSHIP - 1
      d = sd(b) - sd(a): d = d - tlen * INT(d / tlen + .5)
      IF ABS(d) < 2.6 AND ABS(lx(b) - lx(a)) < 1.7 THEN
        push = (1.7 - ABS(lx(b) - lx(a))) / 2 + .02
        IF lx(b) >= lx(a) THEN lx(b) = lx(b) + push: lx(a) = lx(a) - push ELSE lx(b) = lx(b) - push: lx(a) = lx(a) + push
        x = vl(a): vl(a) = vl(b) * .6: vl(b) = x * .6
        IF d > 0 THEN v(a) = v(a) * .985 ELSE v(b) = v(b) * .985
        IF (Human(a) OR Human(b)) AND state = 2 THEN Sfx 11, 70, 4, 110, .1
      END IF
    NEXT
  NEXT
  ' the places
  FOR n = 0 TO NSHIP - 1
    place(n) = 1
    FOR m = 0 TO NSHIP - 1
      IF m <> n THEN
        IF arrived(m) AND arrived(n) THEN
          IF fintime(m) < fintime(n) THEN place(n) = place(n) + 1
        ELSEIF arrived(m) THEN
          place(n) = place(n) + 1
        ELSEIF arrived(n) = 0 AND sd(m) > sd(n) THEN
          place(n) = place(n) + 1
        END IF
      END IF
    NEXT
  NEXT
END SUB

SUB FireWeapon (n)
  w = wep(n)
  IF w = 0 THEN EXIT SUB
  wep(n) = 0
  SELECT CASE w
    CASE 1          ' three rockets, straight on
      FOR k = -1 TO 1: NewShot 1, sd(n) + 2.5, lx(n) + k * 1.1, v(n) + 85, n, -1, 2.2: NEXT
      IF Human(n) THEN Sfx 13, 300, 4, 140, .2
    CASE 2          ' a missile: it follows the craft ahead
      NewShot 2, sd(n) + 2.5, lx(n), v(n) + 70, n, Ahead(n, 160), 4
      IF Human(n) THEN Sfx 13, 200, 3, 140, .3
    CASE 3          ' three mines, dropped behind
      FOR k = 0 TO 2: NewShot 3, sd(n) - 3 - k * 3, lx(n) + (k - 1) * 1.2, 0, n, -1, 40: NEXT
      IF Human(n) THEN Sfx 13, 120, 2, 120, .2
    CASE 4
      shield(n) = 5
      IF Human(n) THEN Sfx 13, 520, 1, 120, .4
    CASE 5
      turbo(n) = 1.6: v(n) = v(n) + 25
      IF Human(n) THEN Sfx 13, 160, 3, 160, .6
  END SELECT
END SUB

SUB NewShot (k, s, l, sp, own, tgt, life)
  FOR p = 0 TO NPROJ - 1
    IF pk(p) = 0 THEN
      pk(p) = k: ps(p) = s: pl(p) = l: pv(p) = sp: pown(p) = own: ptgt(p) = tgt: plife(p) = life
      EXIT SUB
    END IF
  NEXT
END SUB

SUB Shots
  FOR p = 0 TO NPROJ - 1
    IF pk(p) THEN
      plife(p) = plife(p) - dt
      IF plife(p) <= 0 THEN pk(p) = 0
    END IF
    IF pk(p) = 9 THEN
      ps(p) = ps(p) + pv(p) * dt              ' (an explosion, carried along)
    ELSEIF pk(p) THEN
      ps(p) = ps(p) + pv(p) * dt
      IF pk(p) = 2 AND ptgt(p) >= 0 THEN pl(p) = pl(p) + SGN(lx(ptgt(p)) - pl(p)) * MIN(ABS(lx(ptgt(p)) - pl(p)), 14 * dt)
      IF ABS(pl(p)) > HW - .4 AND pk(p) = 1 THEN pk(p) = 0
      FOR n = 0 TO NSHIP - 1
        IF n <> pown(p) OR pk(p) = 3 THEN
          d = sd(n) - ps(p): d = d - tlen * INT(d / tlen + .5)
          IF ABS(d) < 1.8 AND ABS(lx(n) - pl(p)) < 1.2 AND (pk(p) <> 3 OR plife(p) < 39.6) THEN
            Hit n
            pk(p) = 9: plife(p) = .5: pv(p) = v(n) * .8
            EXIT FOR
          END IF
        END IF
      NEXT
    END IF
  NEXT
END SUB

SUB Hit (n)
  IF shield(n) > 0 THEN EXIT SUB
  v(n) = v(n) * .35: spin(n) = 1: vl(n) = vl(n) + (RND - .5) * 20
  IF Human(n) THEN Sfx 13, 60, 4, 220, .5: Say n, T$("Hit!"), .8 ELSE Sfx 9, 80, 4, 90, .3
END SUB

' The camera: behind the player and above, following softly.
' A player's camera: behind the craft and above it (placed along the track itself -- a camera chasing in
' the world would cut the turns -- its side and its height following softly).
SUB Camera (i)
  k = dt * 6: IF k > 1 THEN k = 1
  pcamL(i) = pcamL(i) + (lx(i) * .6 - pcamL(i)) * k
  pcamH(i) = pcamH(i) + (2.9 + v(i) * .004 - pcamH(i)) * k
  TrackAt sd(i) - 7.5, pcamL(i), pcamH(i)
  cex = wx: cey = wy: cez = wz
  TrackAt sd(i) + 9, pcamL(i) * .8, .8
  ctx = wx: cty = wy: ctz = wz
END SUB

' ================================================================================================
' What is written over the picture
' ================================================================================================
' What a player sees over the picture: its lap, its place, its time, its speed, its weapon.
SUB Hud (i)
  x0 = vx(i): y0 = vy(i): w = vw(i): h = vh(i): cx = x0 + w / 2
  IF np = 1 THEN bs = 2 ELSE bs = 1
  IF state = 1 THEN
    n = INT(cdown): IF n > 3 THEN n = 3
    IF n >= 1 THEN BigText LTRIM$(STR$(n)), cx, y0 + h * .3, 6 * bs, RGB(255, 220, 80)
  END IF
  IF pmsgt(i) > 0 THEN
    pmsgt(i) = pmsgt(i) - dt
    BigText pmsg$(i), cx, y0 + h * .25, 3 * bs + (bs = 1), RGB(120, 230, 255)
  END IF
  IF demo THEN EXIT SUB
  lp = INT(sd(i) / tlen) + 1: IF lp < 1 THEN lp = 1
  IF lp > laps THEN lp = laps
  sp = v(i) * 4
  IF np = 1 THEN
    Panel 8, 8, 150, 52
    DRAWTEXT 18, 14, T$("LAP"), RGB(150, 170, 210)
    BigText LTRIM$(STR$(lp)) + "/" + LTRIM$(STR$(laps)), 92, 34, 3, RGB(255, 255, 255)
    Panel 8, 66, 150, 52
    DRAWTEXT 18, 72, T$("POS"), RGB(150, 170, 210)
    BigText LTRIM$(STR$(place(i))) + "/" + LTRIM$(STR$(NSHIP)), 92, 92, 3, RGB(255, 210, 90)
    Panel 482, 8, 150, 52
    DRAWTEXT 492, 14, T$("Time"), RGB(150, 170, 210)
    DRAWTEXT 540, 36, Clock$(racet), RGB(255, 255, 255)
    IF best(circ, klass) > 0 THEN DRAWTEXT 492, 66, T$("BEST") + " " + Clock$(best(circ, klass)), RGB(150, 170, 210)
    Panel 8, 420, 230, 52
    wb = sp / (vmax * 4 * 1.5) * 200: IF wb > 200 THEN wb = 200
    LINE (20, 448)-(220, 460), RGB(30, 34, 50), BF
    IF wb > 1 THEN LINE (20, 448)-(20 + wb, 460), RGB(80 + wb / 2, 200 - wb / 3, 255 - wb), BF
    DRAWTEXT 20, 426, LTRIM$(STR$(INT(sp))) + " " + T$("km/h"), RGB(255, 255, 255)
    IF turbo(i) > 0 THEN DRAWTEXT 150, 426, "TURBO", RGB(255, 200, 80)
    IF wep(i) THEN Panel 250, 420, 140, 52: DRAWTEXT 262, 438, wname$(wep(i)), RGB(255, 120, 90)
    IF shield(i) > 0 THEN DRAWTEXT 262, 400, T$("Shield"), RGB(120, 170, 255)
    MiniMap 551, 386, 75, -1
  ELSE
    ' (a shared screen: smaller)
    Panel x0 + 4, y0 + 4, 112, 40
    LINE (x0 + 10, y0 + 12)-(x0 + 18, y0 + 20), col1(i), BF
    DRAWTEXT x0 + 24, y0 + 8, pilot$(i) + "  " + T$("LAP") + " " + LTRIM$(STR$(lp)) + "/" + LTRIM$(STR$(laps)), RGB(255, 255, 255)
    DRAWTEXT x0 + 10, y0 + 26, T$("POS") + " " + LTRIM$(STR$(place(i))) + "/" + LTRIM$(STR$(NSHIP)), RGB(255, 210, 90)
    DRAWTEXT x0 + w - 84, y0 + 8, Clock$(racet), RGB(255, 255, 255)
    Panel x0 + 4, y0 + h - 30, 124, 26
    wb = sp / (vmax * 4 * 1.5) * 100: IF wb > 100 THEN wb = 100
    LINE (x0 + 10, y0 + h - 12)-(x0 + 110, y0 + h - 8), RGB(30, 34, 50), BF
    IF wb > 1 THEN LINE (x0 + 10, y0 + h - 12)-(x0 + 10 + wb, y0 + h - 8), RGB(80 + wb, 200 - wb / 1.5, 255 - wb * 2), BF
    DRAWTEXT x0 + 10, y0 + h - 30, LTRIM$(STR$(INT(sp))) + " " + T$("km/h"), RGB(255, 255, 255)
    IF wep(i) THEN DRAWTEXT x0 + 136, y0 + h - 22, wname$(wep(i)), RGB(255, 120, 90)
    IF shield(i) > 0 THEN DRAWTEXT x0 + 136, y0 + h - 40, T$("Shield"), RGB(120, 170, 255)
  END IF
  IF arrived(i) AND state = 2 THEN
    BigText T$("FINISH"), cx, y0 + h * .3, 3 * bs, RGB(255, 220, 80)
    DRAWTEXT cx - LEN(T$("Waiting for the others")) * 4, y0 + h * .3 + 30 * bs, T$("Waiting for the others"), RGB(200, 210, 240)
  END IF
END SUB

' Over all the views: the lines between them, the map of a shared screen, the pause, the results.
SUB Overlay
  IF np = 2 THEN LINE (0, 239)-(639, 240), RGB(90, 120, 200), BF
  IF np >= 3 THEN LINE (319, 0)-(320, 479), RGB(90, 120, 200), BF: LINE (0, 239)-(639, 240), RGB(90, 120, 200), BF
  IF np = 3 THEN
    ' the free corner: the map and the order
    LINE (321, 241)-(639, 479), RGB(8, 10, 26), BF
    MiniMap 400, 360, 70, 0
    FOR n = 0 TO NSHIP - 1: order(n) = n: NEXT
    FOR a = 0 TO NSHIP - 2: FOR b = a + 1 TO NSHIP - 1
      IF place(order(b)) < place(order(a)) THEN SWAP order(a), order(b)
    NEXT: NEXT
    FOR k = 0 TO NSHIP - 1
      n = order(k): y = 262 + k * 20
      IF Human(n) THEN c = RGB(255, 255, 255) ELSE c = RGB(150, 160, 190)
      DRAWTEXT 488, y, LTRIM$(STR$(k + 1)) + ".", c
      LINE (512, y + 4)-(520, y + 12), col1(n), BF
      DRAWTEXT 526, y, pilot$(n), c
    NEXT
  ELSEIF np = 2 OR np = 4 THEN
    LINE (290, 210)-(350, 270), RGB(8, 10, 26), BF: LINE (290, 210)-(350, 270), RGB(90, 120, 200), B
    MiniMap 320, 240, 25, 0
  END IF
  IF paused THEN
    LINE (200, 160)-(440, 300), RGB(10, 12, 30), BF: LINE (200, 160)-(440, 300), RGB(120, 140, 220), B
    BigText T$("PAUSED"), 320, 190, 4, RGB(255, 255, 255)
    IF menuSel = 0 THEN c = RGB(255, 220, 80) ELSE c = RGB(140, 150, 180)
    BigText T$("RESUME"), 320, 240, 2, c
    IF menuSel = 1 THEN c = RGB(255, 220, 80) ELSE c = RGB(140, 150, 180)
    BigText T$("BACK TO MENU"), 320, 272, 2, c
  END IF
  IF state = 3 THEN Results
  ShowRate
END SUB

' The circuit seen from above, the craft on it: centred on (x, y), r its half size. framed: a panel.
SUB MiniMap (x, y, r, framed)
  IF framed THEN Panel x - r - 10, y - r - 10, 2 * r + 20, 2 * r + 20
  FOR k = 0 TO 59: LINE (x + mmx(k) * r, y + mmy(k) * r)-(x + mmx(k + 1) * r, y + mmy(k + 1) * r), RGB(110, 120, 170): NEXT
  FOR n = NSHIP - 1 TO 0 STEP -1
    i = INT((sd(n) - tlen * INT(sd(n) / tlen)) / seglen) \ 8: IF i > 59 THEN i = 59
    IF Human(n) THEN c = col1(n): q = 3 ELSE c = RGB(170, 170, 190): q = 2
    LINE (x + mmx(i) * r - q, y + mmy(i) * r - q)-(x + mmx(i) * r + q, y + mmy(i) * r + q), c, BF
  NEXT
END SUB

' (antigrav fps: the pictures a second, in a corner)
SUB ShowRate
  frames = frames + 1
  IF TIMER - fpsT >= 1 OR TIMER < fpsT THEN fps = frames / (TIMER - fpsT + .0001): frames = 0: fpsT = TIMER
  IF showFps THEN DRAWTEXT 300, 4, LTRIM$(STR$(INT(fps))) + " fps", RGB(120, 255, 120)
END SUB

SUB Panel (x, y, w, h)
  LINE (x, y)-(x + w, y + h), RGB(10, 12, 28), BF
  LINE (x, y)-(x + w, y + h), RGB(60, 80, 140), B
  LINE (x, y)-(x + w, y + 1), RGB(90, 200, 255), BF
END SUB

FUNCTION Clock$ (s)
  IF s < 0 THEN s = 0
  m = INT(s / 60): r = s - m * 60
  c = INT(r * 100 + .5): IF c >= 6000 THEN c = 5999
  a$ = LTRIM$(STR$(c \ 100)): IF LEN(a$) < 2 THEN a$ = "0" + a$
  b$ = LTRIM$(STR$(c MOD 100)): IF LEN(b$) < 2 THEN b$ = "0" + b$
  Clock$ = LTRIM$(STR$(m)) + ":" + a$ + "." + b$
END FUNCTION

' Big letters (the 5 x 7 font), centred on x; s = the size of a dot.
SUB BigText (a$, x, y, s, c)
  a$ = UCASE$(a$)
  w = LEN(a$) * 6 * s - s
  x0 = x - w / 2
  FOR i = 1 TO LEN(a$)
    k = INSTR(fontch$, MID$(a$, i, 1))
    IF k THEN
      g$ = glyph$(k)
      FOR r = 0 TO 6
        FOR q = 0 TO 4
          IF MID$(g$, r * 5 + q + 1, 1) = "#" THEN
            xx = x0 + (i - 1) * 6 * s + q * s: yy = y + r * s
            LINE (xx + 1, yy + 1)-(xx + s, yy + s), RGB(0, 0, 0), BF
            LINE (xx, yy)-(xx + s - 1, yy + s - 1), c, BF
          END IF
        NEXT
      NEXT
    END IF
  NEXT
END SUB

SUB Results
  LINE (130, 110)-(510, 400), RGB(8, 10, 26), BF: LINE (130, 110)-(510, 400), RGB(120, 140, 220), B
  BigText T$("FINISH"), 320, 124, 4, RGB(255, 220, 80)
  ' the order
  FOR n = 0 TO NSHIP - 1: order(n) = n: NEXT
  FOR a = 0 TO NSHIP - 2
    FOR b = a + 1 TO NSHIP - 1
      IF fintime(order(b)) < fintime(order(a)) THEN SWAP order(a), order(b)
    NEXT
  NEXT
  bl = 0
  FOR k = 0 TO NSHIP - 1
    n = order(k)
    y = 176 + k * 24
    IF Human(n) THEN c = RGB(255, 255, 255): LINE (150, y - 3)-(490, y + 17), RGB(30, 40, 90), BF ELSE c = RGB(170, 180, 210)
    DRAWTEXT 160, y, LTRIM$(STR$(k + 1)) + ".", c
    LINE (190, y + 3)-(200, y + 13), col1(n), BF
    DRAWTEXT 210, y, pilot$(n), c
    DRAWTEXT 400, y, Clock$(fintime(n)), c
    IF Human(n) AND bestlap(n) > 0 AND (bl = 0 OR bestlap(n) < bl) THEN bl = bestlap(n)
  NEXT
  DRAWTEXT 160, 330, T$("Best lap") + "  " + Clock$(bl), RGB(150, 200, 255)
  IF recordNew THEN DRAWTEXT 360, 330, T$("NEW RECORD"), RGB(255, 220, 80)
  DRAWTEXT 160, 370, T$("Enter: back to the menu"), RGB(140, 150, 180)
END SUB

' ================================================================================================
' The title: the race goes on by itself behind the menu
' ================================================================================================
SUB TitleScreen
  demo = -1
  SetupGrid
  state = 2: racet = 0: menuSel = 4: paused = 0: vdist = 66
  last = TIMER
  DO
    SCREEN , , 1, 0                 ' (drawn on page 1, out of sight: PCOPY shows the whole frame at once)
    now = TIMER: dt = now - last: last = now
    IF dt < 0 OR dt > .05 THEN dt = .05
    t = t + dt
    ' the players: as many as gamepads (1 to 4; antigrav players=n says it for a test)
    npads = PADS: IF npOver > 0 THEN npads = npOver
    IF npads < 1 THEN npads = 1
    IF npads > MAXP THEN npads = MAXP
    IF npads <> lastPads THEN np = npads: lastPads = npads
    IF np > npads THEN np = npads
    k$ = UCASE$(INKEY$)
    p = PAD
    up = k$ = CHR$(0) + "H" OR ((p AND 1) <> 0 AND (padprev AND 1) = 0)
    dn = k$ = CHR$(0) + "P" OR ((p AND 2) <> 0 AND (padprev AND 2) = 0)
    lf = k$ = CHR$(0) + "K" OR ((p AND 4) <> 0 AND (padprev AND 4) = 0)
    rt = k$ = CHR$(0) + "M" OR ((p AND 8) <> 0 AND (padprev AND 8) = 0)
    go = k$ = CHR$(13) OR ((p AND 16) <> 0 AND (padprev AND 16) = 0) OR ((p AND 8192) <> 0 AND (padprev AND 8192) = 0)
    padprev = p
    IF k$ = "F" THEN fs = NOT fs: IF fs THEN FULLSCREEN ON ELSE FULLSCREEN OFF
    IF k$ = CHR$(27) THEN Leave
    IF up THEN menuSel = (menuSel + 5) MOD 6: Sfx 12, 700, 1, 90, .05
    IF dn THEN menuSel = (menuSel + 1) MOD 6: Sfx 12, 700, 1, 90, .05
    IF lf OR rt THEN
      d = 1: IF lf THEN d = -1
      IF menuSel = 0 THEN np = np + d: IF np < 1 THEN np = npads ELSE IF np > npads THEN np = 1
      IF menuSel = 1 THEN circ = (circ - 1 + d + NCIRC) MOD NCIRC + 1: BuildCircuit circ: SetupGrid: state = 2: racet = 0
      IF menuSel = 2 THEN klass = 1 - klass: vmax = 95: IF klass = 1 THEN vmax = 122
      IF menuSel = 3 THEN laps = laps + 2 * d: IF laps > 5 THEN laps = 1 ELSE IF laps < 1 THEN laps = 5
      Sfx 12, 900, 1, 90, .05
    END IF
    IF go THEN
      IF menuSel = 5 THEN Leave
      demo = INSTR(UCASE$(COMMAND$), "DEMO") <> 0
      EXIT DO
    END IF
    Controls
    Physics
    Shots
    MusicTick
    ' the camera: slowly around the leader
    lead = 0
    FOR n = 1 TO NSHIP - 1: IF sd(n) > sd(lead) THEN lead = n
    NEXT
    TrackAt sd(lead) - 11 + 4 * SIN(t * .3), 3.5 * SIN(t * .4), 4 + 1.5 * SIN(t * .23)
    cex = wx: cey = wy: cez = wz
    TrackAt sd(lead) + 6, lx(lead), .8
    ctx = wx: cty = wy: ctz = wz
    DrawWorld sd(lead) - 10
    ' the menu
    BigText T$("ANTIGRAV"), 320, 34, 8, RGB(120, 220, 255)
    LINE (150, 176)-(490, 410), RGB(8, 10, 26), BF: LINE (150, 176)-(490, 410), RGB(90, 120, 200), B
    MenuLine 0, T$("PLAYERS"), LTRIM$(STR$(np))
    MenuLine 1, T$("CIRCUIT"), cname$(circ)
    IF klass = 0 THEN k$ = "VENOM" ELSE k$ = "RAPIER"
    MenuLine 2, T$("CLASS"), k$
    MenuLine 3, T$("LAPS"), LTRIM$(STR$(laps))
    MenuLine 4, T$("START"), ""
    MenuLine 5, T$("QUIT"), ""
    c = RGB(150, 160, 200)
    IF best(circ, klass) > 0 THEN DRAWTEXT 170, 392, T$("Best lap") + "  " + Clock$(best(circ, klass)), RGB(150, 200, 255)
    a$ = LTRIM$(STR$(PADS)) + " " + T$("gamepads"): DRAWTEXT 480 - LEN(a$) * 8, 392, a$, c
    LINE (0, 426)-(639, 476), RGB(8, 10, 26), BF
    a$ = T$("Up/Down choose  Left/Right change  Enter start  F full screen  Esc quit"): DRAWTEXT 320 - LEN(a$) * 4, 432, a$, c
    a$ = T$("Arrows steer  Up/X thrust  Z/C air brakes  Space fire  P pause"): DRAWTEXT 320 - LEN(a$) * 4, 454, a$, c
    ShowRate
    SfxTick
    PCOPY 1, 0
    PAUSE 1
  LOOP
END SUB

SUB Leave
  SoundsOff
  IF fs THEN FULLSCREEN OFF
  END
END SUB

SUB MenuLine (k, a$, b$)
  y = 190 + k * 33
  IF menuSel = k THEN c = RGB(255, 220, 80): LINE (160, y - 6)-(480, y + 21), RGB(30, 40, 90), BF ELSE c = RGB(170, 180, 210)
  IF b$ = "" THEN
    BigText a$, 320, y, 2, c
  ELSE
    BigText a$, 240, y, 2, c
    IF menuSel = k THEN b$ = "< " + b$ + " >"
    DRAWTEXT 330, y + 1, b$, c
  END IF
END SUB

' ================================================================================================
' The best laps (SD:/apps/antigrav.app/times.txt: a line a circuit, Venom then Rapier)
' ================================================================================================
SUB LoadTimes
  ON ERROR GOTO 0
  IF NOT FILEEXISTS("SD:/apps/antigrav.app/times.txt") THEN EXIT SUB
  f = FREEFILE
  OPEN "SD:/apps/antigrav.app/times.txt" FOR INPUT AS #f
  FOR c = 1 TO NCIRC
    IF EOF(f) THEN EXIT FOR
    INPUT #f, best(c, 0), best(c, 1)
  NEXT
  CLOSE #f
END SUB

SUB SaveTimes
  IF demo THEN EXIT SUB
  bl = 0
  FOR n = 0 TO np - 1: IF bestlap(n) > 0 AND (bl = 0 OR bestlap(n) < bl) THEN bl = bestlap(n)
  NEXT
  IF bl > 0 AND (best(circ, klass) = 0 OR bl < best(circ, klass)) THEN
    best(circ, klass) = bl: recordNew = -1
    IF FILEEXISTS("SD:/apps/antigrav.app/app.txt") THEN
      f = FREEFILE
      OPEN "SD:/apps/antigrav.app/times.txt" FOR OUTPUT AS #f
      FOR c = 1 TO NCIRC: PRINT #f, best(c, 0); ","; best(c, 1): NEXT
      CLOSE #f
    END IF
  END IF
END SUB

' ================================================================================================
' Sound: the music on the General MIDI synthesizer, the engine and the effects on the voices
' ================================================================================================
SUB MusicTick
  IF NOT musOn THEN EXIT SUB
  IF mnext = 0 THEN
    mnext = TIMER: mstep = 0: mbar = 0: mseed = INT(RND * 1000)
    MIDIPROGRAM 0, 38: MIDIPROGRAM 1, 81: MIDIPROGRAM 2, 89
    MIDICONTROL 0, 7, 100: MIDICONTROL 1, 7, 60: MIDICONTROL 2, 7, 70: MIDICONTROL 9, 7, 110
    FOR i = 0 TO 15: mlead(i) = INT(RND * 4) + 4 * INT(RND * 2): IF RND < .25 THEN mlead(i) = -1
    NEXT
  END IF
  now = TIMER
  IF now < mnext - 1 OR now > mnext + 1 THEN mnext = now
  DO WHILE now >= mnext
    ' A minor: Am F C G, a bar each
    sel = mbar MOD 4
    IF sel = 0 THEN root = 45 ELSE IF sel = 1 THEN root = 41 ELSE IF sel = 2 THEN root = 48 ELSE root = 43
    s = mstep
    IF s = 0 THEN
      FOR i = 0 TO 2: IF mpad(i) THEN MIDINOTE 2, mpad(i), 0
      NEXT
      third = 3: IF sel >= 1 THEN third = 4
      mpad(0) = root + 24: mpad(1) = root + 24 + third: mpad(2) = root + 31
      FOR i = 0 TO 2: MIDINOTE 2, mpad(i), 55: NEXT
    END IF
    ' drums
    IF s MOD 4 = 0 THEN MIDINOTE 9, 36, 120
    IF s = 4 OR s = 12 THEN MIDINOTE 9, 38, 100: MIDINOTE 9, 39, 70
    IF s MOD 4 = 2 THEN MIDINOTE 9, 42, 70
    IF s = 14 AND (mbar MOD 2) = 1 THEN MIDINOTE 9, 46, 70
    ' the bass: eighths, the octave on the off-beats
    IF s MOD 2 = 0 THEN
      IF mbass THEN MIDINOTE 0, mbass, 0
      mbass = root - 12 + 12 * ((s \ 2) MOD 2)
      MIDINOTE 0, mbass, 105
    END IF
    ' the lead: an arpeggio on the chord, from the second round of four bars on
    IF mleadOn THEN MIDINOTE 1, mleadOn, 0: mleadOn = 0
    IF (mbar MOD 8) >= 4 AND mlead(s) >= 0 THEN
      third = 3: IF sel >= 1 THEN third = 4
      a = mlead(s) MOD 4
      IF a = 0 THEN nn = 0 ELSE IF a = 1 THEN nn = third ELSE IF a = 2 THEN nn = 7 ELSE nn = 10
      mleadOn = root + 24 + nn + 12 * (mlead(s) \ 4)
      MIDINOTE 1, mleadOn, 80
    END IF
    mstep = mstep + 1
    IF mstep >= 16 THEN mstep = 0: mbar = mbar + 1
    mnext = mnext + 60 / 138 / 4
  LOOP
END SUB

SUB Engine
  IF NOT sndOn OR demo THEN EXIT SUB
  f = 45 + v(0) * .9 + thrust(0) * 12 + turbo(0) * 40
  IF ABS(f - engF) > 3 THEN
    engF = f
    NOTEON 14, f, 3, 26 + thrust(0) * 24
  END IF
END SUB

' An effect on voice vc: frequency, wave (0 square, 1 sine, 2 triangle, 3 saw, 4 noise), volume, seconds.
SUB Sfx (vc, f, w, vol, dur)
  IF NOT sndOn THEN EXIT SUB
  NOTEON vc, f, w, vol
  sfxEnd(vc) = TIMER + dur
END SUB

SUB SfxTick
  IF NOT sndOn THEN EXIT SUB
  now = TIMER
  FOR vc = 9 TO 13
    IF sfxEnd(vc) > 0 AND now > sfxEnd(vc) THEN NOTEOFF vc: sfxEnd(vc) = 0
  NEXT
END SUB

SUB SoundsOff
  IF sndOn THEN NOTEOFF: engF = 0
  IF musOn THEN MIDIOFF: mnext = 0: mbass = 0: mleadOn = 0: mpad(0) = 0: mpad(1) = 0: mpad(2) = 0
END SUB
