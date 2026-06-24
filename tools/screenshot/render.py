#!/usr/bin/env python3
# render.py -- generate faithful simulated screenshots of the Zircon OS / apps.
#
# It does NOT imagine the look: it loads the REAL skin bitmaps (wings/button/closebgs.bmp),
# the REAL 8x16 console font (circle Font8x16), and reproduces each app's own draw routine
# with the exact colours/coords from user/<app>.c. The wallpaper Voronoi field and the
# Mandelbrot escape-time are ported verbatim. Output PNGs land in <repo>/screenshots/.
#
# App screenshots are window-only on a transparent background (rounded skin corners stay
# transparent). One desktop overview composites the wallpaper + panel + a few windows.

import os, re
import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SD   = os.path.join(ROOT, "sdcard")
OUT  = os.path.join(ROOT, "screenshots")
os.makedirs(OUT, exist_ok=True)

def C(v): return ((v >> 16) & 255, (v >> 8) & 255, v & 255)   # 0xRRGGBB -> (r,g,b)
MAGENTA = (255, 0, 255)                                        # GIMAGE_TRANSPARENT key

# theme.txt tints
TINT_ACTIVE   = C(0xFFC878)
TINT_INACTIVE = C(0x8090A0)
TITLE_TEXT    = C(0xFFFFFF)

WIN_TITLEBAR_H = 32
WIN_BORDER     = 7
FW, FH = 8, 19            # glyph advance 8; cell height 19 (16 glyph rows + 3 leading)

# ---- font: parse circle/lib/font8x16.cpp (16 bytes/glyph, MSB = leftmost, first_char 0x21)
def load_font():
    txt = open(os.path.join(ROOT, "circle", "lib", "font8x16.cpp")).read()
    body = re.search(r"font_data\[\]\s*=\s*\{(.*?)\};", txt, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)          # drop "// 0xNN" row comments (not font data!)
    vals = [int(b, 16) for b in re.findall(r"0x[0-9A-Fa-f]{2}", body)]
    glyphs = {}
    for i in range(len(vals) // 16):
        glyphs[0x21 + i] = vals[i*16:i*16+16]
    return glyphs
GLYPH = load_font()

class Canvas:
    def __init__(self, w, h, bg=None):
        self.w, self.h = w, h
        self.img = Image.new("RGBA", (w, h), (bg + (255,)) if bg else (0, 0, 0, 0))
        self.px  = self.img.load()
        self.d   = ImageDraw.Draw(self.img)
    def fill(self, x, y, w, h, c):
        if w <= 0 or h <= 0: return
        self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=c + (255,))
    def frame(self, x, y, w, h, c):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], outline=c + (255,))
    def text(self, x, y, s, c):
        for i, ch in enumerate(s):
            g = GLYPH.get(ord(ch))
            if g is None: continue
            bx = x + i * 8
            for row in range(16):
                bits = g[row]; yy = y + row
                if yy < 0 or yy >= self.h or bits == 0: continue
                for col in range(8):
                    if bits & (1 << (7 - col)):
                        xx = bx + col
                        if 0 <= xx < self.w: self.px[xx, yy] = c + (255,)
    def ctext(self, x, w, y, s, c):                      # horizontally centred text
        self.text(x + (w - len(s) * 8) // 2, y, s, c)

# ---- 9-slice skin (port of kernel/gui/skin.cpp) ------------------------------------
class Skin:
    def __init__(self, path, count, l, r, t, b):
        im = Image.open(path).convert("RGB")
        self.im = im; self.p = im.load()
        self.iw, self.ih = im.size
        self.count = count
        self.sw = self.iw
        self.sh = self.ih // count
        self.l, self.r, self.t, self.b = l, r, t, b
    def colorize(self, tint):
        for y in range(self.ih):
            for x in range(self.iw):
                c = self.p[x, y]
                if c == MAGENTA: continue
                self.p[x, y] = ((c[0]*tint[0])//255, (c[1]*tint[1])//255, (c[2]*tint[2])//255)
    def _blit(self, cv, dx, dy, sx, sy, w, h):           # PutOtherPart, magenta-keyed
        for j in range(h):
            yy = sy + j; ty = dy + j
            if yy < 0 or yy >= self.ih or ty < 0 or ty >= cv.h: continue
            for i in range(w):
                xx = sx + i
                if xx < 0 or xx >= self.iw: continue
                c = self.p[xx, yy]
                if c == MAGENTA: continue
                tx = dx + i
                if 0 <= tx < cv.w: cv.px[tx, ty] = c + (255,)
    def draw_on(self, cv, num, x, y, w, h):
        sw, sh, lw, rw, th, bh = self.sw, self.sh, self.l, self.r, self.t, self.b
        midW, midH = sw - lw - rw, sh - th - bh
        outW, outH = w - lw - rw, h - th - bh
        sy = sh * (num % self.count)
        if w == sw and h == sh:
            self._blit(cv, x, y, 0, sy, sw, sh); return
        if outH > 0 and outW > 0:
            fc = self.p[sw // 2, sy + sh // 2]
            if fc != MAGENTA: cv.fill(x + lw, y + th, outW, outH, fc)
        if midW > 0 and outW > 0:
            fillR = x + w - rw; tx = x + lw
            while tx < fillR:
                tw = min(midW, fillR - tx)
                self._blit(cv, tx, y, lw, sy, tw, th)
                if midH > 0: self._blit(cv, tx, y + th, lw, sy + th, tw, min(midH, outH))
                self._blit(cv, tx, y + h - bh, lw, sy + sh - bh, tw, bh)
                tx += midW
        if midH > 0 and outH > 0:
            fillB = y + h - bh; ty = y + th
            while ty < fillB:
                tht = min(midH, fillB - ty)
                self._blit(cv, x, ty, 0, sy + th, lw, tht)
                self._blit(cv, x + w - rw, ty, sw - rw, sy + th, rw, tht)
                ty += midH
        if lw and th: self._blit(cv, x, y, 0, sy, lw, th)
        if rw and th: self._blit(cv, x + w - rw, y, sw - rw, sy, rw, th)
        if lw and bh: self._blit(cv, x, y + h - bh, 0, sy + sh - bh, lw, bh)
        if rw and bh: self._blit(cv, x + w - rw, y + h - bh, sw - rw, sy + sh - bh, rw, bh)

WIN_ACT = Skin(os.path.join(SD, "skins", "wings.bmp"),    1, 7, 7, 32, 7); WIN_ACT.colorize(TINT_ACTIVE)
WIN_INA = Skin(os.path.join(SD, "skins", "wings.bmp"),    1, 7, 7, 32, 7); WIN_INA.colorize(TINT_INACTIVE)
BTN     = Skin(os.path.join(SD, "skins", "button.bmp"),   3, 6, 6, 6, 6)
CLOSE   = Skin(os.path.join(SD, "skins", "closebgs.bmp"), 3, 5, 5, 5, 5)

def button(cv, x, y, w, h, label):                       # kernel GW_BUTTON w/ skin -> black label
    BTN.draw_on(cv, 0, x, y, w, h)
    cv.ctext(x, w, y + (h - FH) // 2, label, C(0x000000))

def textbox(cv, x, y, w, h, s, focused=False):
    cv.fill(x, y, w, h, C(0xFFFFFF))
    cv.frame(x, y, w, h, C(0x0000FF) if focused else C(0x808080))
    cv.text(x + 4, y + (h - FH) // 2, s, C(0x101010))

# ---- window chrome: skin + title + close box (port of CWindow::Draw) ---------------
def window(client, title, active=True):
    cw, ch = client.w, client.h
    W, H = cw + 2*WIN_BORDER, ch + WIN_TITLEBAR_H + WIN_BORDER
    cv = Canvas(W, H)
    (WIN_ACT if active else WIN_INA).draw_on(cv, 0, 0, 0, W, H)
    cv.text(WIN_BORDER + 2, (WIN_TITLEBAR_H - FH) // 2, title, TITLE_TEXT)   # y0+8
    # close box: 22px square, right edge -4, top +5
    cbx1 = cw + 2*WIN_BORDER - 1 - 4; cbx0 = cbx1 - 22; cby0 = 5; cby1 = cby0 + 22
    CLOSE.draw_on(cv, 0, cbx0, cby0, cbx1 - cbx0 + 1, cby1 - cby0 + 1)
    cv.d.line([cbx0 + 3, cby0 + 3, cbx1 - 3, cby1 - 3], fill=C(0xFFFFFF) + (255,), width=1)
    cv.d.line([cbx1 - 3, cby0 + 3, cbx0 + 3, cby1 - 3], fill=C(0xFFFFFF) + (255,), width=1)
    cv.img.alpha_composite(client.img, (WIN_BORDER, WIN_TITLEBAR_H))
    return cv

# ====================================================================================
# App client renderers (each draws the client canvas exactly like user/<app>.c)
# ====================================================================================

def app_terminal():
    W, H = 620, 400; cv = Canvas(W, H, C(0x101418))
    lines = ["Zircon terminal -- try: ls /bin | grep e",
             "$ ls /bin | grep e", "echo.elf", "sleep.elf", "yes.elf",
             "$ ps", "  1 k R  idle", "  2 k R  gui", " 14 a R  panel",
             " 17 a R  voronoy", " 21 a R  terminal", " 25 a R  fractal",
             "$ echo zircon | wc -c", "7"]
    for i, ln in enumerate(lines):
        cv.text(4, 4 + i * FH, ln, C(0xC8D0C0))
    iy = 4 + 19 * FH
    cv.fill(0, iy, W, FH, C(0x182028))
    inp = "$ cat readme.txt | grep -i pi"
    cv.text(4, iy, inp, C(0xFFFFFF))
    cv.fill(4 + len(inp) * FW, iy, 2, FH, C(0x60FF90))
    return cv

def app_irc():
    # Matches user/irc.c: dark scrollback (status '*' blue, notice '-' amber, msgs
    # grey) with a bottom input row showing the "[channel] " prompt + typed line.
    W, H = 680, 440; cv = Canvas(W, H, C(0x101418))
    lines = [
        ("Onyx IRC -- waiting for network ...", 0xC8D0C0),
        ("* network up (192.168.1.42) -- connecting to irc.libera.chat", 0x80B0FF),
        ("* registered", 0x80B0FF),
        ("-NickServ- This nickname is registered. Please identify.", 0xFFC060),
        ("* alice joined #onyx", 0x80B0FF),
        ("<alice> hey, is this the bare-metal Pi channel?", 0xC8D0C0),
        ("<bob> yep -- Onyx, a hobby OS on Circle", 0xC8D0C0),
        ("<onyx-user> hello from Onyx :)", 0xC8D0C0),
        ("* bob waves", 0x80B0FF),
    ]
    for i, (ln, col) in enumerate(lines):
        cv.text(4, 4 + i * FH, ln, C(col))
    iy = 4 + 21 * FH                      # bottom input row (under the scrollback)
    cv.fill(0, iy - 2, W, 1, C(0x303840))
    prompt = "[#onyx] "
    cv.text(4, iy, prompt, C(0x60FF90))
    px = 4 + len(prompt) * FW
    inp = "what are you all running it on?"
    cv.text(px, iy, inp, C(0xFFFFFF))
    cv.fill(px + len(inp) * FW, iy, 2, FH, C(0x60FF90))
    return cv

def app_filer():
    W, H = 460, 380; LISTY = 34; cv = Canvas(W, H, C(0x202830))
    cv.fill(0, 0, W, LISTY - 2, C(0x303D4D))
    cv.text(8, 9, "SD:/", C(0xE0E0E0))
    cv.text(W - 150, 9, "d:del r:ren n:new", C(0x90A0B0))
    rows = [("apps", -1, 1), ("bin", -1, 1), ("skins", -1, 1),
            ("armstub8-rpi4.bin", 3104, 0), ("bcm2711-rpi-4-b.dtb", 52472, 0),
            ("cmdline.txt", 31, 0), ("config.txt", 184, 0),
            ("kernel8-rpi4.img", 540672, 0), ("README.md", 1182, 0),
            ("start4.elf", 2253544, 0)]
    sel = 0
    for r, (name, size, isdir) in enumerate(rows):
        y = LISTY + r * FH
        if r == sel: cv.fill(0, y, W, FH, C(0x355070))
        if isdir:
            cv.text(10, y + 1, "[" + name + "]", C(0x80C8FF))
        else:
            cv.text(10, y + 1, name, C(0xD8D8D8))
            s = str(size); cv.text(W - 12 - len(s) * FW, y + 1, s, C(0x90A0A8))
    return cv

def app_fractal():
    W, RH = 340, 240; H = RH + 16; cv = Canvas(W, H, C(0x000000))
    cxr, cyr, span = -0.5, 0.0, 3.0
    spanY = span * RH / W; x0 = cxr - span/2; y0 = cyr - spanY/2
    sx = span / W; sy = spanY / RH; maxit = 96
    py_i = np.arange(RH); px_i = np.arange(W)
    cr = (x0 + px_i * sx)[None, :].repeat(RH, 0)
    ci = (y0 + py_i * sy)[:, None].repeat(W, 1)
    zr = np.zeros_like(cr); zi = np.zeros_like(ci); it = np.zeros((RH, W), int)
    alive = np.ones((RH, W), bool)
    for _ in range(maxit):
        zr2 = zr*zr; zi2 = zi*zi; esc = (zr2 + zi2) > 4
        alive &= ~esc
        nzi = 2*zr*zi + ci; nzr = zr2 - zi2 + cr
        zr = np.where(alive, nzr, zr); zi = np.where(alive, nzi, zi)
        it += alive
    r = np.where(it >= maxit, 0, (it*8) & 255)
    g = np.where(it >= maxit, 0, (it*5 + 40) & 255)
    b = np.where(it >= maxit, 0, (it*11 + 80) & 255)
    rgb = np.dstack([r, g, b, np.full((RH, W), 255)]).astype("uint8")
    cv.img.paste(Image.fromarray(rgb, "RGBA"), (0, 0))
    cv.px = cv.img.load(); cv.d = ImageDraw.Draw(cv.img)
    cv.fill(0, RH, W, 16, C(0x181C20))
    cv.text(6, RH + 1, "click:in  o:out  r:reset   z=0 it=96", C(0x90B0A0))
    cv.fill(4, 4, 120, 18, C(0x203040)); cv.frame(4, 4, 120, 18, C(0x708CA8))
    cv.text(10, 4 + (18 - 16)//2, "Mandelbrot", C(0xE6E6E6))
    cv.text(4 + 120 - 14, 4 + (18 - 16)//2, "v", C(0x90B0C8))
    return cv

def app_tinycalc():
    # tinycalc now draws its buttons with the user-side toolkit (uikit.h), not the
    # kernel skin: flat face + border, centred white label, hover = lighter face.
    W, H = 280, 296; cv = Canvas(W, H, C(0x283038))
    def uibtn(x, y, w, h, label, hover=False):
        cv.fill(x, y, w, h, C(0x697690 if hover else 0x566074))
        cv.frame(x, y, w, h, C(0x161C24))
        cv.ctext(x, w, y + (h - FH)//2, label, C(0xFFFFFF))
    DISPX, DISPY, DISPW, DISPH = 8, 8, W - 72, 36
    cv.fill(DISPX, DISPY, DISPW, DISPH, C(0x101820))
    s = "3.14159265"
    cv.text(DISPX + DISPW - len(s)*FW - 8, DISPY + (DISPH - FH)//2, s, C(0x60FF90))
    uibtn(W - 56, DISPY, 48, DISPH, "RAD")
    lbl = ["sin","cos","tan","ln","log","sqrt","x^2","x^y","1/x","e^x",
           "7","8","9","/","C","4","5","6","*","+/-","1","2","3","-","%","0",".","pi","=","+"]
    x0, y0, bw, bh, sxx, syy = 8, 52, 48, 34, 54, 40
    for r in range(6):
        for c in range(5):
            uibtn(x0 + c*sxx, y0 + r*syy, bw, bh, lbl[r*5 + c], hover=(r*5+c == 28))
    return cv

def app_tinypad():
    W, H = 560, 430; TB = 28; cv = Canvas(W, H, C(0x303840))
    cv.fill(0, TB, W, H - TB, C(0xFFFFFF))
    textbox(cv, 4, 4, W - 160, 20, "SD:notes.txt")
    button(cv, W - 152, 4, 70, 20, "Open"); button(cv, W - 78, 4, 70, 20, "Save")
    text = ["Zircon notes", "", "- panel pins to the right edge (config position=3)",
            "- apps talk to the kernel through the kapi ABI table",
            "- fixed-point only: no FP regs saved across switches",
            "- wallpaper is a userland app (voronoy), not the kernel", "",
            "TODO: compositor dirty-rects + async flush"]
    tx, ty = 4, TB + 2
    for r, ln in enumerate(text):
        cv.text(tx, ty + r * FH, ln, C(0x101010))
    cx = tx + len(text[3]) * FW; cy = ty + 3 * FH
    cv.fill(cx, cy, 2, FH, C(0x0060FF))
    return cv

def _dow(y, m, d):
    if m < 3: m += 12; y -= 1
    k = y % 100; j = y // 100
    h = (d + 13*(m+1)//5 + k + k//4 + j//4 + 5*j) % 7
    return (h + 6) % 7
def _dim(y, m):
    d = [31,28,31,30,31,31,30,31,30,31,30,31]; d[1]=31  # placeholder, set below
    d = [31, 29 if (m==2 and ((y%4==0 and y%100!=0) or y%400==0)) else 28,
         31,30,31,30,31,31,30,31,30,31]
    return d[m-1]

def app_calendar():
    W, H = 392, 320; OX, OY, CW, CH = 8, 48, 54, 34; cv = Canvas(W, H, C(0x202830))
    year, month, td, tm, ty_ = 2026, 6, 23, 6, 2026
    MONTHS = ["January","February","March","April","May","June","July","August",
              "September","October","November","December"]
    cv.text(OX, 10, MONTHS[month-1] + " " + str(year), C(0xFFFFFF))
    cv.text(W - 130, 10, "<- -> month  ^v year", C(0x708090))
    for c, w in enumerate(["Su","Mo","Tu","We","Th","Fr","Sa"]):
        cv.text(OX + c*CW + 8, 30, w, C(0x90B0D0))
    first = _dow(year, month, 1); ndays = _dim(year, month); sel = 23; notes = {5, 18, 23}
    for cell in range(42):
        day = cell - first + 1
        if day < 1 or day > ndays: continue
        col, row = cell % 7, cell // 7
        x, y = OX + col*CW, OY + row*CH
        bg = 0x283440
        if day == sel: bg = 0x355070
        elif day == td and month == tm and year == ty_: bg = 0x405028
        cv.fill(x, y, CW - 2, CH - 2, C(bg))
        cv.text(x + 4, y + 3, str(day), C(0xE0E0E0))
        if day in notes: cv.fill(x + CW - 9, y + 4, 4, 4, C(0x60D0FF))
    ny = OY + 6*CH + 6
    cv.text(OX, ny, "note for day 23:", C(0x90A0B0))
    cv.fill(OX, ny + 14, W - 16, 18, C(0x101418))
    note = "RPi4 demo at NSI"
    cv.text(OX + 4, ny + 15, note, C(0xFFFFFF))
    cv.fill(OX + 4 + len(note)*FW, ny + 15, 2, FH, C(0x60FF90))
    cv.text(OX, ny + 36, "type + Enter to save", C(0x607080))
    return cv

def app_paint():
    W, H, PAL_H = 420, 300, 26; cv = Canvas(W, H, C(0xF0F0F0))
    SW = [0x000000,0xFFFFFF,0xE05050,0x50C060,0x4080E0,0xE0C040,0xC060D0,0x40C0C0]
    cur = 0xE05050
    # a small painted scene on the white canvas
    cv.d.ellipse([60, 90, 180, 210], fill=C(0x4080E0) + (255,))
    cv.d.ellipse([95, 120, 150, 175], fill=C(0xE0C040) + (255,))
    cv.d.line([210, 200, 360, 90], fill=C(0xE05050) + (255,), width=8)
    cv.d.line([210, 90, 360, 200], fill=C(0x50C060) + (255,), width=8)
    for i, t in enumerate(range(40)):
        cv.px[260 + i*3 % 90, 230 + (i*7) % 40] = C(0xC060D0) + (255,)
    cv.fill(0, 0, W, PAL_H, C(0x303840))
    for i, col in enumerate(SW):
        x = 4 + i*26; cv.fill(x, 3, 22, PAL_H - 6, C(col))
        if col == cur:
            cv.fill(x, 3, 22, 2, C(0xFFFFFF)); cv.fill(x, PAL_H - 5, 22, 2, C(0xFFFFFF))
    cv.text(8*26 + 12, 8, "brush 3", C(0xD0D0D0))
    cv.text(W - 150, 8, "[ ] size  c clear  s save", C(0x90A0A0))
    return cv

def app_taskman():
    W, H, LISTY = 340, 300, 30; cv = Canvas(W, H, C(0x202830))
    cv.fill(0, 0, W, LISTY - 2, C(0x303D4D))
    tasks = [("R","idle",1),("R","gui",1),("S","fs",1),("R","panel",0),
             ("S","voronoy",0),("R","terminal",0),("R","fractal",0),
             ("R","tinycalc",0),("R","taskman",0)]
    cv.text(8, 9, str(len(tasks)) + " tasks  k:kill ent:raise", C(0xE0E0E0))
    sel = 5
    for i, (st, name, kern) in enumerate(tasks):
        y = LISTY + i * FH
        if i == sel: cv.fill(0, y, W, FH, C(0x355070))
        cv.text(8, y + 1, st, C(0xFFD070))
        cv.text(26, y + 1, name, C(0x808890) if kern else C(0xE8E8E8))
        if kern: cv.text(W - 60, y + 1, "kernel", C(0x606870))
    return cv

def app_memmon():
    W, H = 440, 380; cv = Canvas(W, H, C(0x181C24))
    cv.text(8, 8, "Memory monitor", C(0xFFFFFF))
    bx, by, bw, bh = 8, 30, W - 16, 22                # usage bar (~18% used)
    cv.fill(bx, by, bw, bh, C(0x283040)); cv.frame(bx, by, bw, bh, C(0x404A5A))
    cv.fill(bx + 1, by + 1, int((bw - 2) * 0.18), bh - 2, C(0xD08040))
    cv.ctext(bx, bw, by + (bh - FH)//2, "18%", C(0xFFFFFF))
    y = 62
    for label, val in [("Total: 3936 MB",0),("Used:  118208 KB",0),
                       ("Free:  3912448 KB",0),("Apps:  2304 KB",0),("Page:  64 KB",0)]:
        cv.text(8, y, label, C(0xD0D8E0)); y += FH
    y += 6; cv.text(8, y, "By pages owned:", C(0x90A0B0)); y += FH
    for pg, name, kern in [(9,"fractal",0),(7,"terminal",0),(6,"tinycalc",0),
                           (5,"memmon",0),(4,"panel",0),(3,"taskman",0)]:
        cv.text(8, y, ("%4dp  %s" % (pg, name)), C(0xC8D0C0)); y += FH
    return cv

def app_2048():
    N, CELL, GAP, OX, OY = 4, 68, 6, 8, 44
    W = OX*2 + N*CELL + (N-1)*GAP; H = OY + N*CELL + (N-1)*GAP + 8
    cv = Canvas(W, H, C(0xBBADA0))
    grid = [[2,8,32,2],[4,64,128,16],[2,16,4,8],[0,2,0,4]]
    tc = {2:0xEEE4DA,4:0xEDE0C8,8:0xF2B179,16:0xF59563,32:0xF67C5F,64:0xF65E3B,
          128:0xEDCF72,256:0xEDCC61,512:0xEDC850,1024:0xEDC53F}
    cv.text(8, 14, "score:", C(0xFFFFFF)); cv.text(8 + 7*FW, 14, "1248", C(0xFFF0D0))
    for r in range(N):
        for c in range(N):
            x = OX + c*(CELL+GAP); y = OY + r*(CELL+GAP); v = grid[r][c]
            cv.fill(x, y, CELL, CELL, C(tc.get(v, 0xEDC22E)) if v else C(0xCDC1B4))
            if v:
                t = str(v)
                cv.text(x + (CELL - len(t)*FW)//2, y + (CELL - FH)//2, t,
                        C(0x776E65) if v <= 4 else C(0xF9F6F2))
    return cv

def app_minesweeper():
    GW, GH, CELL, OX, OY, NMINES = 16, 12, 24, 8, 28, 30
    W = OX*2 + GW*CELL; H = OY + GH*CELL + 8; cv = Canvas(W, H, C(0x202830))
    rng = 0x1234abcd
    def rnd():
        nonlocal rng; rng = (rng*1103515245 + 12345) & 0xFFFFFFFF; return rng >> 16
    mine = [[0]*GW for _ in range(GH)]
    placed = 0
    while placed < NMINES:
        c = rnd() % GW; r = rnd() % GH
        if not mine[r][c]: mine[r][c] = 1; placed += 1
    adj = [[0]*GW for _ in range(GH)]
    for r in range(GH):
        for c in range(GW):
            n = 0
            for dr in (-1,0,1):
                for dc in (-1,0,1):
                    rr, cc = r+dr, c+dc
                    if 0 <= rr < GH and 0 <= cc < GW and mine[rr][cc]: n += 1
            adj[r][c] = n
    opened = [[1 if (r < 7 and not mine[r][c]) else 0 for c in range(GW)] for r in range(GH)]
    flag = [[0]*GW for _ in range(GH)]
    fl = 0
    for r in range(7, GH):
        for c in range(GW):
            if mine[r][c] and fl < 2: flag[r][c] = 1; fl += 1
    cv.text(8, 8, "L:reveal  R:flag  r:restart", C(0x90A0B0))
    numcol = [0,0x4090FF,0x40C060,0xFF6060,0xD080FF,0xFFA040,0x40D0D0,0xD0D0D0,0xA0A0A0]
    for r in range(GH):
        for c in range(GW):
            x = OX + c*CELL; y = OY + r*CELL
            if opened[r][c]:
                cv.fill(x, y, CELL-1, CELL-1, C(0x303A44))
                if adj[r][c]:
                    cv.text(x + CELL//2 - 3, y + 4, str(adj[r][c]), C(numcol[adj[r][c]]))
            else:
                cv.fill(x, y, CELL-1, CELL-1, C(0x586472))
                if flag[r][c]: cv.fill(x+6, y+6, CELL-13, CELL-13, C(0xFFD040))
    return cv

def app_eyes():
    W, H = 180, 110; cv = Canvas(W, H, C(0xB8C0C8))
    mx, my = 168, 18
    def disc(cx, cy, r, col):
        cv.d.ellipse([cx-r, cy-r, cx+r, cy+r], fill=col + (255,))
    def eye(ex, ey, R, pr):
        disc(ex, ey, R, C(0xFFFFFF))
        dx, dy = mx-ex, my-ey; maxoff = R - pr - 2
        d = (dx*dx + dy*dy) ** 0.5
        if d > maxoff and d > 0: px, py = ex + dx*maxoff/d, ey + dy*maxoff/d
        else: px, py = ex + dx, ey + dy
        disc(int(px), int(py), pr, C(0x101018))
    eye(50, 55, 34, 12); eye(130, 55, 34, 12)
    return cv

def app_applist():
    W, H = 240, 460; cv = Canvas(W, H, C(0x141C26))
    COLS, LX, VIEW_Y, SB_W, CELLH = 3, 6, 28, 14, 70
    CELLW = (W - SB_W - 4 - LX) // COLS
    cv.text(LX + 2, 6, "Applications", C(0xFFFFFF))
    names = ["2048","calendar","demoA","eyes","filer","inidemo","life","mandelbrot",
             "minesweeper","paint","pong","same","sheet","snake","sokoban","taskman",
             "terminal","tetris","tinycalc","tinypad","voronoy"]
    vis_rows = (H - VIEW_Y - 6) // CELLH
    for i, name in enumerate(names):
        row, col = i // COLS, i % COLS
        if row >= vis_rows: break
        cw, ch = CELLW - 8, CELLH - 8
        x = LX + col*CELLW; y = VIEW_Y + row*CELLH
        ip = os.path.join(SD, "apps", name + ".app", "icon.bmp")
        nlabH = FH + 2
        if os.path.exists(ip):
            im = Image.open(ip).convert("RGB"); iw, ih = im.size
            ix = x + (cw - iw)//2; iy = y + ((ch - nlabH) - ih)//2
            pix = im.load()
            for yy in range(ih):
                for xx in range(iw):
                    c = pix[xx, yy]
                    if c == MAGENTA: continue
                    tx, ty = ix+xx, iy+yy
                    if 0 <= tx < W and 0 <= ty < H: cv.px[tx, ty] = c + (255,)
        else:
            m = 6; cv.fill(x+m, y+m, cw-2*m, ch-2*m-nlabH, C(0x808890))
        disp = name if len(name)*FW <= cw else name[:cw//FW]
        cv.ctext(x, cw, y + ch - FH, disp, C(0xFFFFFF))
    # scrollbar (track + thumb near top)
    sbx = W - SB_W - 2; sby = VIEW_Y; sbh = H - VIEW_Y - 6
    cv.fill(sbx, sby, SB_W, sbh, C(0x303840)); cv.frame(sbx, sby, SB_W, sbh, C(0x000000))
    th = max(20, sbh * vis_rows // ((len(names)+COLS-1)//COLS))
    cv.fill(sbx+1, sby+1, SB_W-2, th, C(0xA0A0C0)); cv.frame(sbx+1, sby+1, SB_W-2, th, C(0x000000))
    return cv   # borderless: no chrome

# ====================================================================================
def voronoi_wallpaper(W, H):
    ADIV, base, npts = 2, 0x4878B0, 28
    mx, my = W // ADIV, H // ADIV
    rng = 0x4D3C2B1A
    px = []; py = []
    for _ in range(npts):
        rng = (rng*1103515245 + 12345) & 0xFFFFFFFF; px.append(rng % mx)
        rng = (rng*1103515245 + 12345) & 0xFFFFFFFF; py.append(rng % my)
    xs = np.arange(mx); ys = np.arange(my)
    best = np.full((my, mx), 1e9)
    for k in range(npts):
        dx = np.abs(xs - px[k]) * 256 // mx; dx = np.where(dx > 128, 256 - dx, dx)
        dy = np.abs(ys - py[k]) * 256 // my; dy = np.where(dy > 128, 256 - dy, dy)
        d = np.floor(np.sqrt(dx[None, :]**2 + dy[:, None]**2)); d = np.minimum(d, 255)
        best = np.minimum(best, d)
    cc = (96 + (best * (255 - 96)).astype(int) // 255); cc = np.minimum(cc, 255)
    br, bg, bb = (base >> 16) & 255, (base >> 8) & 255, base & 255
    r = (br * cc) >> 8; g = (bg * cc) >> 8; b = (bb * cc) >> 8
    small = np.dstack([r, g, b]).astype("uint8")
    full = np.kron(small, np.ones((ADIV, ADIV, 1), "uint8"))[:H, :W]
    a = np.full((H, W, 1), 255, "uint8")
    return Image.fromarray(np.concatenate([full, a], 2), "RGBA")

def render_desktop():
    W, H = 1024, 768
    cv = Canvas(W, H)
    cv.img.paste(voronoi_wallpaper(W, H), (0, 0)); cv.px = cv.img.load(); cv.d = ImageDraw.Draw(cv.img)
    # cascade: fractal + tinycalc inactive (slate chrome), terminal active (gold) on top
    fw = window(app_fractal(),  "fractal",  False); cv.img.alpha_composite(fw.img, (63, 28))
    cw = window(app_tinycalc(), "tinycalc", False); cv.img.alpha_composite(cw.img, (63, 328))
    tw = window(app_terminal(), "terminal", True);  cv.img.alpha_composite(tw.img, (293, 108))
    draw_panel(cv, W, H)
    draw_cursor(cv, 470, 360)
    return cv

def draw_panel(cv, sw, sh):
    BAR, ICON, INSET, STEP = 60, 40, 10, 44
    content = 308; by = (sh - content)//2; bx = sw - BAR - 2     # position=3 (right)
    cv.fill(bx, by, BAR, content, C(0x222A36)); cv.frame(bx, by, BAR, content, C(0x11161E))
    ix = bx + INSET
    def sep(a): cv.fill(bx + 4, by + a, BAR - 8, 2, C(0x425068))
    def icon(name, a):
        ip = os.path.join(SD, "apps", name)
        if not os.path.exists(ip): return
        im = Image.open(ip).convert("RGB"); iw, ih = im.size; p = im.load()
        ox = ix + (ICON - iw)//2; oy = by + a + (ICON - ih)//2
        for yy in range(ih):
            for xx in range(iw):
                c = p[xx, yy]
                if c == MAGENTA: continue
                cv.px[ox+xx, oy+yy] = c + (255,)
    def badge(a):
        for t in range(9):
            cv.d.line([ix+2, by+a+ICON-2-t, ix+2+(8-t), by+a+ICON-2-t], fill=C(0x40E060)+(255,))
    icon("panel.app/apps.bmp", 4)
    sep(48)
    icon("terminal.app/icon.bmp", 54);  badge(54)
    icon("filer.app/icon.bmp", 98)
    icon("tinypad.app/icon.bmp", 142)
    icon("tinycalc.app/icon.bmp", 186); badge(186)
    sep(232)
    icon("mandelbrot.app/icon.bmp", 236); badge(236)
    sep(281)
    cv.text(bx + 6, by + 290, "13:37", C(0xFFFFFF))

def draw_cursor(cv, x, y):
    pts = [(0,0),(0,16),(4,12),(7,18),(9,17),(6,11),(11,11)]
    cv.d.polygon([(x+a, y+b) for a, b in pts], fill=C(0xFFFFFF)+(255,), outline=C(0x000000)+(255,))

# ---- more kernel widgets (ports of CWindow::Draw cases) ----------------------------
def label(cv, x, y, w, h, s):
    cv.text(x, y + (h - FH) // 2, s, C(0xFFFFFF))
def checkbox(cv, x, y, w, h, s, checked):
    box = min(h, 16); by = y + (h - box) // 2
    cv.fill(x, by, box, box, C(0xDDDDDD)); cv.frame(x, by, box, box, C(0x000000))
    if checked:
        cv.d.line([x+3, by+box//2, x+box//2, by+box-3], fill=C(0x007000)+(255,))
        cv.d.line([x+box//2, by+box-3, x+box-3, by+3], fill=C(0x007000)+(255,))
    cv.text(x + box + 6, y + (h - FH) // 2, s, C(0xFFFFFF))
def slider(cv, x, y, w, h, val):
    midy = y + h // 2; cv.d.line([x+2, midy, x+w-2, midy], fill=C(0xC0C0C0)+(255,))
    thumb = x + max(0, min(100, val)) * (w - 8) // 100
    cv.fill(thumb, y, 8, h, C(0xA0A0C0)); cv.frame(thumb, y, 8, h, C(0x000000))
def progress(cv, x, y, w, h, val):
    cv.fill(x, y, w, h, C(0x303030))
    fw = (w - 2) * max(0, min(100, val)) // 100
    if fw > 0: cv.fill(x + 1, y + 1, fw, h - 2, C(0x30C030))
    cv.frame(x, y, w, h, C(0x000000))
def textarea(cv, x, y, w, h, lines, focused=False):
    cv.fill(x, y, w, h, C(0xFFFFFF)); cv.frame(x, y, w, h, C(0x0000FF) if focused else C(0x808080))
    for i, ln in enumerate(lines): cv.text(x + 4, y + 3 + i * FH, ln, C(0x101010))
    if focused and lines:
        cv.fill(x + 4 + len(lines[-1]) * FW, y + 3 + (len(lines)-1) * FH, 1, FH, C(0x000000))
def scrollbar_v(cv, x, y, w, h, val):
    cv.fill(x, y, w, h, C(0x303840)); cv.frame(x, y, w, h, C(0x000000))
    th = max(16, h // 4); ty = y + max(0, min(100, val)) * (h - th) // 100
    cv.fill(x + 1, ty, w - 2, th, C(0xA0A0C0)); cv.frame(x + 1, ty, w - 2, th, C(0x000000))
def scrollbar_h(cv, x, y, w, h, val):
    cv.fill(x, y, w, h, C(0x303840)); cv.frame(x, y, w, h, C(0x000000))
    tw = max(16, w // 4); tx = x + max(0, min(100, val)) * (w - tw) // 100
    cv.fill(tx, y + 1, tw, h - 2, C(0xA0A0C0)); cv.frame(tx, y + 1, tw, h - 2, C(0x000000))

def blit_bmp(cv, path, x, y):                # PutOther, magenta-keyed (icons / apps.bmp)
    if not os.path.exists(path): return
    im = Image.open(path).convert("RGB"); iw, ih = im.size; p = im.load()
    for yy in range(ih):
        for xx in range(iw):
            c = p[xx, yy]
            if c == MAGENTA: continue
            if 0 <= x+xx < cv.w and 0 <= y+yy < cv.h: cv.px[x+xx, y+yy] = c + (255,)

# ====================================================================================
# remaining apps
# ====================================================================================
def app_demoA():
    W, H = 240, 180; cv = Canvas(W, H, C(0x102030))
    x, y, s = 120, 84, 36
    cv.fill(x, y, s+1, s+1, C(0xFFC040)); cv.fill(x+6, y+6, s-11, s-11, C(0x804000))
    return cv

def app_demoB():
    W, H, t = 260, 200, 64
    xs = np.arange(W); ys = np.arange(H)
    r = np.broadcast_to((xs[None, :] + t) & 0xFF, (H, W))
    g = np.broadcast_to((ys[:, None] + (t >> 1)) & 0xFF, (H, W))
    b = (xs[None, :] + ys[:, None] + t) >> 1 & 0xFF
    a = np.full((H, W), 255)
    arr = np.dstack([r, g, b, a]).astype("uint8")
    cv = Canvas(W, H); cv.img.paste(Image.fromarray(arr, "RGBA"), (0, 0)); cv.px = cv.img.load()
    return cv

def app_demoC():
    W, H, FWF, FHF, FPX = 320, 240, 80, 100, 200
    field = [0] * (FWF * (FHF + 2)); pal = [0]*256; seed = [0x1234]
    for i in range(256):
        hot = min(255, i*3); mid = 0 if i < 85 else min(255, (i-85)*3)
        cold = 0 if i < 170 else min(255, (i-170)*3)
        pal[i] = (hot << 16) | (mid << 8) | cold        # red fire
    for _ in range(140):
        base = (FHF-1)*FWF
        for c in range(FWF):
            prod = seed[0] * 0x8405; seed[0] = (prod + 1) & 0xFFFFFFFF
            field[base + c] = (prod >> 32) & 0xFF
        for r in range(1, FHF):
            row = r*FWF
            for c in range(FWF):
                idx = row + c
                v = (field[idx] + field[idx+1] + field[idx+2] + field[idx+FWF+1]) >> 2
                if v > 0: v -= 1
                field[idx-FWF] = v
    cv = Canvas(W, H, C(0x202028))
    for dy in range(FPX):
        sr = (dy >> 1) * FWF
        for dx in range(W):
            cv.px[dx, dy] = C(pal[field[sr + (dx >> 2)]]) + (255,)
    button(cv, 10, FPX + 5, 80, 26, "Color")
    return cv

def app_demoD():
    W, H = 300, 220; cv = Canvas(W, H, C(0x283848))
    label(cv, 10, 10, 280, 16, "slider: 60%")
    textbox(cv, 10, 38, 220, 22, "hello world")
    checkbox(cv, 10, 74, 220, 18, "enable feature", True)
    button(cv, 10, 108, 90, 30, "OK")
    slider(cv, 10, 150, 220, 18, 60)
    progress(cv, 10, 178, 220, 16, 60)
    return cv

def app_demoE():
    W, H = 320, 240; cv = Canvas(W, H, C(0x283848))
    VP_X, VP_Y, VP_W, VP_H, CONTENT, CELL = 10, 88, 252, 110, 400, 40
    val = 25; sX = val*(CONTENT-VP_W)//100; sY = val*(CONTENT-VP_H)//100
    for vy in range(VP_H):
        cy = sY + vy
        for vx in range(VP_W):
            cx = sX + vx
            if cx % CELL == 0 or cy % CELL == 0: col = 0xFFFFFF
            else: col = (((cy//CELL)*25+30) << 16) | (((cx//CELL)*25+30) << 8) | 0x50
            cv.px[VP_X+vx, VP_Y+vy] = C(col) + (255,)
    label(cv, 10, 8, 290, 14, "multi-line editable + scroll view:")
    textarea(cv, 10, 26, 290, 52, ["The quick brown fox", "jumps over the lazy dog."], True)
    scrollbar_v(cv, 266, VP_Y, 12, VP_H, val)
    scrollbar_h(cv, VP_X, 202, VP_W, 12, val)
    return cv

def app_demoF():
    W, H = 52, 220; cv = Canvas(W, H, C(0x303848))
    for i, lab in enumerate("ABCDE"):
        button(cv, 6, 6 + i*42, 40, 36, lab)
    return cv   # borderless

def app_inidemo():
    W, H = 380, 250; cv = Canvas(W, H, C(0x202832)); fh = FH; y, x = 12, 12
    cv.text(x, y, "config.ini values:", C(0x80D0FF)); y += fh + 6
    cv.text(x, y, "greeting = Hello from the .ini file!", C(0xE0E0E0)); y += fh + 3
    cv.text(x, y, "[app] name = INI Demo", C(0xE0E0E0)); y += fh + 3
    cv.text(x, y, "[app] version = 1.0", C(0xE0E0E0)); y += fh + 3
    cv.text(x, y, "[app] author = Zircon OS", C(0xE0E0E0)); y += fh + 8
    cv.text(x, y, "[display] barwidth (int) = 240", C(0xFFD070)); y += fh + 4
    cv.fill(x, y, 240, 18, C(0x40A0FF))
    return cv

def app_life():
    GW, GH, CELL, OX, OY = 48, 34, 12, 8, 24
    W, H = OX*2 + GW*CELL, OY + GH*CELL + 8; cv = Canvas(W, H, C(0x101418))
    rng = [0xC0FFEE21]
    def rnd():
        rng[0] = (rng[0]*1103515245 + 12345) & 0xFFFFFFFF; return rng[0] >> 16
    cell = [[1 if rnd() % 4 == 0 else 0 for _ in range(GW)] for _ in range(GH)]
    for _ in range(10):
        nxt = [[0]*GW for _ in range(GH)]
        for r in range(GH):
            for c in range(GW):
                n = 0
                for dr in (-1,0,1):
                    for dc in (-1,0,1):
                        if dr or dc:
                            rr, cc = r+dr, c+dc
                            if 0 <= rr < GH and 0 <= cc < GW and cell[rr][cc]: n += 1
                nxt[r][c] = 1 if (cell[r][c] and n in (2,3)) or (not cell[r][c] and n == 3) else 0
        cell = nxt
    cv.text(8, 6, "running  space:pause s c r", C(0x90A0B0))
    for r in range(GH):
        for c in range(GW):
            if cell[r][c]: cv.fill(OX + c*CELL, OY + r*CELL, CELL-1, CELL-1, C(0x60E090))
    return cv

def app_pong():
    W, H, PW, PH, BS = 480, 320, 8, 56, 8; cv = Canvas(W, H, C(0x101814))
    for y in range(0, H, 16): cv.fill(W//2 - 1, y, 2, 8, C(0x304038))
    ly, ry, bx, by = 120, 96, 300, 150
    cv.fill(16, ly, PW, PH, C(0xFFFFFF)); cv.fill(W-16-PW, ry, PW, PH, C(0xFFFFFF))
    cv.fill(bx, by, BS, BS, C(0x60FF90))
    cv.text(W//2 - 40, 10, "3", C(0xFFFFFF)); cv.text(W//2 + 32, 10, "5", C(0xFFFFFF))
    return cv

def app_snake():
    GW, GH, CELL, OX, OY = 24, 18, 18, 12, 30
    W, H = OX*2 + GW*CELL, OY + GH*CELL + 12; cv = Canvas(W, H, C(0x141820))
    cv.text(OX, 8, "score:", C(0x90A0B0)); cv.text(OX + 7*FW, 8, "60", C(0xFFFFFF))
    cv.fill(OX-2, OY-2, GW*CELL+3, GH*CELL+3, C(0x404858)); cv.fill(OX, OY, GW*CELL, GH*CELL, C(0x0C0E12))
    def cell(x, y, c): cv.fill(OX + x*CELL, OY + y*CELL, CELL-1, CELL-1, C(c))
    cell(18, 5, 0xFF4040)
    body = [(14,8),(13,8),(12,8),(11,8),(11,9),(11,10),(12,10)]
    for i, (x, y) in enumerate(body): cell(x, y, 0x80FF80 if i == 0 else 0x40C040)
    return cv

def app_tetris():
    COLS, ROWS, CELL, FX, FY = 10, 20, 18, 12, 12
    W, H = FX + COLS*CELL + 132, FY + ROWS*CELL + 12; cv = Canvas(W, H, C(0x181C24))
    COLORS = [0,0x00FFFF,0xFFE000,0xC000FF,0x00E000,0xFF3030,0x4070FF,0xFF9000]
    cv.fill(FX-2, FY-2, COLS*CELL+3, ROWS*CELL+3, C(0x404858)); cv.fill(FX, FY, COLS*CELL, ROWS*CELL, C(0x101014))
    stack = {16:[1,1,0,2,2,3,3,0,4,4], 17:[5,5,5,0,6,6,2,2,0,1],
             18:[3,0,4,4,4,1,1,6,6,2], 19:[2,2,3,3,0,5,5,5,1,1]}
    def dc(gx, gy, col): cv.fill(FX + gx*CELL, FY + gy*CELL, CELL-1, CELL-1, C(col))
    for r, row in stack.items():
        for c, v in enumerate(row):
            if v: dc(c, r, COLORS[v])
    mask = 0x0E40                          # falling T piece (colour idx 3), rot 0, at (4,2)
    for yy in range(4):
        for xx in range(4):
            if (mask >> (15 - (yy*4 + xx))) & 1: dc(4 + xx, 2 + yy, COLORS[3])
    sx = FX + COLS*CELL + 12
    cv.text(sx, FY+4, "SCORE", C(0x90A0B0));  cv.text(sx, FY+18, "1200", C(0xFFFFFF))
    cv.text(sx, FY+44, "LINES", C(0x90A0B0)); cv.text(sx, FY+58, "12", C(0xFFFFFF))
    for i, t in enumerate(["left/right","up: rotate","dn: soft","spc: drop","r: restart"]):
        cv.text(sx, FY+92 + i*12, t, C(0x708090))
    return cv

def app_same():
    GW, GH, CELL, OX, OY = 18, 13, 24, 12, 30
    W, H = OX*2 + GW*CELL, OY + GH*CELL + 12; cv = Canvas(W, H, C(0x141820))
    COLORS = [0, 0xE05050, 0x50B060, 0x4080E0, 0xE0C040]
    rng = [0xBADC0DE5]
    def rnd():
        rng[0] = (rng[0]*1103515245 + 12345) & 0xFFFFFFFF; return rng[0] >> 16
    grid = [[1 + rnd() % 4 for _ in range(GW)] for _ in range(GH)]
    cv.text(OX, 8, "score:", C(0x90A0B0)); cv.text(OX + 7*FW, 8, "0", C(0xFFFFFF))
    cv.text(W - 150, 8, "click groups  r:new", C(0x708090))
    for r in range(GH):
        for c in range(GW):
            cv.fill(OX + c*CELL, OY + r*CELL, CELL-1, CELL-1, C(COLORS[grid[r][c]]))
    return cv

def app_sokoban():
    GW, GH, CELL, OX, OY = 24, 18, 22, 8, 26
    W, H = OX*2 + GW*CELL, OY + GH*CELL + 8; cv = Canvas(W, H, C(0x181C20))
    lvl = ["#######", "# .   #", "# $@  #", "#  $ .#", "#     #", "#######"]
    wall = [[0]*GW for _ in range(GH)]; tgt = [[0]*GW for _ in range(GH)]
    box = [[0]*GW for _ in range(GH)]; px = py = 0
    for r, line in enumerate(lvl):
        for c, ch in enumerate(line):
            if ch == '#': wall[r][c] = 1
            if ch in ".*+": tgt[r][c] = 1
            if ch in "$*": box[r][c] = 1
            if ch in "@+": px, py = c, r
    cv.text(8, 8, "arrows move  r:reset n:next", C(0x90A0B0))
    for r in range(GH):
        for c in range(GW):
            x, y = OX + c*CELL, OY + r*CELL
            if wall[r][c]: cv.fill(x, y, CELL-1, CELL-1, C(0x586070)); continue
            if tgt[r][c]: cv.fill(x + CELL//2 - 3, y + CELL//2 - 3, 6, 6, C(0xC06060))
            if box[r][c]:
                cv.fill(x+2, y+2, CELL-5, CELL-5, C(0x60C060) if tgt[r][c] else C(0xB07840))
    cv.fill(OX + px*CELL + 4, OY + py*CELL + 4, CELL-9, CELL-9, C(0x4090FF))
    return cv

def app_sheet():
    COLS, ROWS, CW, CH, GX0, GY0 = 8, 16, 62, 20, 30, 44
    W, H = GX0 + COLS*CW + 4, GY0 + ROWS*CH + 4; cv = Canvas(W, H, C(0x202830))
    cells = {                                       # (r,c): (display, kind)  1/2 = number
        (0,0):("10",1),(1,0):("20",1),(2,0):("30",2),(4,0):("Total",0),
        (0,1):("1.5",1),(1,1):("2.5",1),(2,1):("4",2),(2,2):("120",2),(4,2):("auto",0),
    }
    sr, sc = 2, 0
    ref = chr(ord('A') + sc) + str(sr + 1) + ":"
    cv.fill(0, 0, W, 20, C(0x303D4D))
    cv.text(4, 3, ref, C(0xFFD070)); cv.text(4 + len(ref)*FW + 4, 3, "=A1+A2", C(0xFFFFFF))
    cx = 4 + len(ref)*FW + 4 + len("=A1+A2")*FW; cv.fill(cx, 3, 2, FH, C(0x60FF90))
    for c in range(COLS):
        cv.text(GX0 + c*CW + CW//2 - 3, 26, chr(ord('A') + c), C(0x90B0D0))
    for r in range(ROWS):
        cv.text(4, GY0 + r*CH + 3, str(r + 1), C(0x90B0D0))
        for c in range(COLS):
            x, y = GX0 + c*CW, GY0 + r*CH
            cv.fill(x, y, CW-1, CH-1, C(0x355070) if (r == sr and c == sc) else C(0x283440))
            v = cells.get((r, c))
            if not v: continue
            disp, kind = v
            if kind in (1, 2): cv.text(x + CW - 4 - len(disp)*FW, y + 3, disp, C(0xE8E8E8))
            else: cv.text(x + 3, y + 3, disp, C(0xD0D0C0))
    return cv

def app_panel():
    BAR, ICON, INSET = 60, 40, 10
    content = 308; cv = Canvas(BAR, content, C(0x222A36)); cv.frame(0, 0, BAR, content, C(0x11161E))
    ix = INSET
    def sep(a): cv.fill(4, a, BAR-8, 2, C(0x425068))
    def badge(a):
        for t in range(9): cv.d.line([ix+2, a+ICON-2-t, ix+2+(8-t), a+ICON-2-t], fill=C(0x40E060)+(255,))
    def ic(name, a):
        ip = os.path.join(SD, "apps", name)
        if os.path.exists(ip):
            iw, ih = Image.open(ip).size
            blit_bmp(cv, ip, ix + (ICON-iw)//2, a + (ICON-ih)//2)
    ic("panel.app/apps.bmp", 4); sep(48)
    ic("terminal.app/icon.bmp", 54);  badge(54)
    ic("filer.app/icon.bmp", 98)
    ic("tinypad.app/icon.bmp", 142)
    ic("tinycalc.app/icon.bmp", 186); badge(186)
    sep(232); ic("mandelbrot.app/icon.bmp", 236); badge(236); sep(281)
    cv.text(6, 290, "13:37", C(0xFFFFFF))
    return cv

def swatch(cv, x, y, w, h, color):           # applib ax_colorpick (closed)
    cv.fill(x, y, w, h, C(color)); cv.frame(x, y, w, h, C(0xFFFFFF)); cv.frame(x-1, y-1, w+2, h+2, C(0x000000))
def dropdown(cv, x, y, w, h, text, arrow="v"):  # applib ax_dropdown (closed)
    cv.fill(x, y, w, h, C(0x203040)); cv.frame(x, y, w, h, C(0x708CA8))
    cv.text(x+6, y+(h-16)//2, text, C(0xE6E6E6)); cv.text(x+w-14, y+(h-16)//2, arrow, C(0x90B0C8))
def parse_ini_flat(path):                     # config.c's flat key=value reader
    rows = []
    if os.path.exists(path):
        for line in open(path, encoding="utf-8", errors="replace"):
            s = line.strip()
            if not s or s[0] in ";#[": continue
            if "=" in s:
                k, v = s.split("=", 1); rows.append((k.strip(), v.strip()))
    return rows

def app_config():
    W, H, LW, LISTY, KEYX, VALX, BTN_Y = 560, 400, 150, 28, 158, 332, 368
    pitch = FH + 2                            # config.c: g_fh = font_height + 2 = 21
    cv = Canvas(W, H, C(0x202830))
    cv.fill(0, 0, LW, H, C(0x283440)); cv.text(8, 6, "Apps", C(0x90C0FF))
    apps = sorted(d[:-4] for d in os.listdir(os.path.join(SD, "apps")) if d.endswith(".app"))
    cur = "inidemo"; sel = apps.index(cur) if cur in apps else 0
    vis = (BTN_Y - LISTY) // pitch
    for r in range(min(vis, len(apps))):
        y = LISTY + r * pitch
        if r == sel: cv.fill(0, y, LW, pitch, C(0x355070))
        cv.text(8, y + 1, apps[r], C(0xE0E0E0))
    cv.text(KEYX, 6, cur, C(0xFFD070))
    rows = parse_ini_flat(os.path.join(SD, "apps", cur + ".app", "config.ini"))
    for r, (k, v) in enumerate(rows):
        if r >= vis: break
        y = LISTY + r * pitch
        cv.fill(KEYX, y, VALX - KEYX - 4, pitch - 2, C(0x303D4D))
        cv.fill(VALX, y, W - VALX - 6, pitch - 2, C(0x303D4D))
        cv.text(KEYX + 4, y + 1, k, C(0xC8E0C8)); cv.text(VALX + 4, y + 1, v, C(0xE8E8C0))
    if rows:                                  # caret in the first value cell (focused)
        cv.fill(VALX + 4 + len(rows[0][1]) * FW, LISTY, 2, pitch - 2, C(0x60FF90))
    cv.fill(380, BTN_Y, 80, 24, C(0x306030)); cv.frame(380, BTN_Y, 80, 24, C(0xC0C0C0))
    cv.text(404, BTN_Y + 4, "Apply", C(0xFFFFFF))
    cv.fill(470, BTN_Y, 80, 24, C(0x603030)); cv.frame(470, BTN_Y, 80, 24, C(0xC0C0C0))
    cv.text(486, BTN_Y + 4, "Discard", C(0xFFFFFF))
    return cv

def app_theme():
    W, H, BTN_Y = 380, 340, 300; cv = Canvas(W, H, C(0x202833))
    cv.text(12, 8, "Theme editor", C(0xFFD070))
    labels = ["Active tint", "Inactive tint", "Title text", "Wallpaper"]
    colors = [0xFFC878, 0x8090A0, 0xFFFFFF, 0x4878B0]
    for i in range(4):
        y = 70 + i * 30
        cv.text(12, y + 1, labels[i], C(0xD0D8E0)); swatch(cv, 130, y, 70, 20, colors[i])
    cv.text(12, 37, "Keymap", C(0xD0D8E0)); dropdown(cv, 130, 36, 90, 18, "FR")
    cv.fill(130, BTN_Y, 80, 24, C(0x306030)); cv.frame(130, BTN_Y, 80, 24, C(0xC0C0C0))
    cv.text(144, BTN_Y + 4, "Apply", C(0xFFFFFF))
    cv.fill(220, BTN_Y, 80, 24, C(0x603030)); cv.frame(220, BTN_Y, 80, 24, C(0xC0C0C0))
    cv.text(234, BTN_Y + 4, "Discard", C(0xFFFFFF))
    return cv

if __name__ == "__main__":
    # windowed apps: (folder, title, client builder) -> wrapped in skinned chrome.
    WINAPPS = [
        ("terminal","terminal",app_terminal), ("filer","filer",app_filer),
        ("mandelbrot","fractal",app_fractal), ("tinycalc","tinycalc",app_tinycalc),
        ("tinypad","tinypad",app_tinypad), ("calendar","calendar",app_calendar),
        ("config","config",app_config), ("theme","theme",app_theme),
        ("paint","paint",app_paint), ("taskman","taskman",app_taskman),
        ("2048","2048",app_2048), ("minesweeper","minesweeper",app_minesweeper),
        ("eyes","eyes",app_eyes), ("inidemo","inidemo",app_inidemo),
        ("irc","irc",app_irc), ("memmon","memmon",app_memmon),
        ("life","life",app_life), ("pong","pong",app_pong), ("snake","snake",app_snake),
        ("tetris","tetris",app_tetris), ("same","same",app_same),
        ("sokoban","sokoban",app_sokoban), ("sheet","sheet",app_sheet),
        ("demoA","demo A: bouncing box",app_demoA), ("demoB","demo B: colour field",app_demoB),
        ("demoC","Fire demo",app_demoC), ("demoD","widget gallery",app_demoD),
        ("demoE","textarea + scrollview",app_demoE),
    ]
    for fname, title, fn in WINAPPS:
        window(fn(), title, True).img.save(os.path.join(OUT, fname + ".png"))
        print("wrote", fname + ".png")
    # borderless apps (no chrome): app drawer, demo sidebar, the panel itself
    for fname, fn in [("applist", app_applist), ("demoF", app_demoF), ("panel", app_panel)]:
        fn().img.save(os.path.join(OUT, fname + ".png")); print("wrote", fname + ".png")
    # voronoy is windowless: its "screenshot" is the wallpaper it paints
    voronoi_wallpaper(1024, 768).save(os.path.join(OUT, "voronoy.png")); print("wrote voronoy.png")
    render_desktop().img.save(os.path.join(OUT, "desktop.png")); print("wrote desktop.png")
