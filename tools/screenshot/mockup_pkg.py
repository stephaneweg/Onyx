#!/usr/bin/env python3
"""mockup_pkg.py -- the first mock-ups of Onyx's package manager and updates: the Control Panel's
"Software" applet (updates, installed, available), the restart after a system update, the update
notification and the `pkg` command. See docs/pkg/README.md.

    python3 tools/screenshot/mockup_pkg.py  -> docs/pkg/mockups/pkg-*.png

1024 x 768 (the Pi's usual screen); the helpers are mockup_archiver.py's, the icons the card's own
(sdcard/apps/<name>.app/icon.bmp, the magenta key made clear).
"""
import os, sys
from PIL import Image

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_archiver as M
from mockup_archiver import A, lighten, shade, mix, button, field, dropdown, checkbox, scrollbar, progress, cursor, menu_popup

M.W, M.H = 1024, 768
M.OUT = os.path.join (M.ROOT, "docs", "pkg", "mockups")
K = M.K
TEXT, DIM, FAINT, FACE, LIST, LINE, LINE2, SEL = M.TEXT, M.DIM, M.FAINT, M.FACE, M.LIST, M.LINE, M.LINE2, M.SEL
GREEN, AMBER, RED, BLUE, WHITE = M.GREEN, M.AMBER, M.RED, M.BLUE, (255, 255, 255)

_icons = {}
def icon (c, name, x, y, s = 40):
	"""The app's own icon from the card (40 x 40, magenta = clear), scaled to s."""
	if name not in _icons:
		p = os.path.join (M.ROOT, "sdcard", "apps", name + ".app", "icon.bmp")
		im = Image.open (p).convert ("RGBA")
		px = im.load ()
		for j in range (im.size[1]):
			for i in range (im.size[0]):
				if px[i, j][:3] == (255, 0, 255): px[i, j] = (0, 0, 0, 0)
		_icons[name] = im
	im = _icons[name].resize ((int (s * K), int (s * K)), Image.NEAREST if s % 40 == 0 else Image.LANCZOS)
	c.img.paste (im, (int (x * K), int (y * K)), im)

def ic_onyx (c, x, y, s = 40):
	"""The system package: a dark gem (Onyx) on a chip."""
	c.rect (x + 2, y + 2, s - 4, s - 4, (54, 60, 72), r = 8)
	m = s / 2
	c.poly ([(x + m, y + 7), (x + s - 9, y + m - 3), (x + m, y + s - 7), (x + 9, y + m - 3)], (24, 26, 32))
	c.poly ([(x + m, y + 7), (x + s - 9, y + m - 3), (x + m, y + m - 1)], (120, 128, 146))
	c.poly ([(x + m, y + 7), (x + 9, y + m - 3), (x + m, y + m - 1)], (86, 94, 110))

def badge (c, x, y, s, bg, fg = WHITE):
	w = c.tw (s, "smallb") + 12
	c.rect (x, y, w, 17, bg, r = 8.5); c.text_c (x, y, w, 17, s, "smallb", fg)
	return w

# ---- the Control Panel's frame, its breadcrumb, the applet's tabs -------------------------------------
WX, WY, WW, WH = 62, 44, 900, 660
def panel_window (c, tab, counts = (3, 41, 23)):
	M.desktop (c, "Control Panel", ["File", "View", "Help"])
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, "Control Panel")
	# the breadcrumb (as the applets')
	c.rect (cx + 10, cy + 8, cw - 20, 32, A (WHITE, 110), r = 6, outline = LINE)
	c.text_l (cx + 22, cy + 8, 32, "Control Panel", "uib")
	tx = cx + 22 + c.tw ("Control Panel", "uib") + 10
	c.line ([(tx, cy + 19), (tx + 4, cy + 24), (tx, cy + 29)], DIM, 1.2)
	c.text_l (tx + 12, cy + 8, 32, "Software", "uib", M.LINK); c.hline (tx + 12, tx + 12 + c.tw ("Software", "uib"), cy + 33, M.LINK, 2)
	c.hline (cx, cx + cw, cy + 48, shade (FACE, 0.85))
	# the tabs
	labels = ["Updates", "Installed", "Available"]
	x = cx + 14
	for i, (s, n) in enumerate (zip (labels, counts)):
		on = s == tab
		w = c.tw (s, "uib") + 22 + (c.tw (str (n), "smallb") + 16 if n else 0)
		if on: c.rect (x, cy + 58, w, 30, SEL, r = 15)
		c.text_l (x + 12, cy + 58, 30, s, "uib", WHITE if on else TEXT)
		if n:
			bx = x + 12 + c.tw (s, "uib") + 6
			bw = c.tw (str (n), "smallb") + 10
			c.rect (bx, cy + 65, bw, 16, (WHITE if on else (RED if s == "Updates" else shade (FACE, 0.8))), r = 8)
			c.text_c (bx, cy + 65, bw, 16, str (n), "smallb", SEL if on else WHITE)
		x += w + 6
	# the search, on the right
	field (c, cx + cw - 244, cy + 58, 230, 30, "", "Search the packages")
	c.ellipse (cx + cw - 36, cy + 71, 5, None, DIM, 1.5); c.line ([(cx + cw - 32, cy + 75), (cx + cw - 27, cy + 80)], DIM, 1.5)
	return cx, cy + 98, cw, ch - 98

def row_card (c, x, y, w, h, hot = False):
	c.rect (x, y, w, h, WHITE if not hot else mix (WHITE, M.SEL_SOFT, 0.35), r = 8, outline = LINE2)

# ---- 1. the updates ---------------------------------------------------------------------------------------
UPD = [
	("onyx", "Onyx system", "2026.10.1", "2026.11.0", "Kernel v71, the shell, bin/ -- faster SD writes, Milk theme fixes", "7.4 MB", True, "restart"),
	("jet", "Jet Browser", "3.2.0", "3.3.0", "Find in page, context menu, text-shadow", "4.1 MB", True, None),
	("archiver", "Archiver", "1.0.0", "1.1.0", "7z and tar.gz, read-only RAR", "612 KB", True, None),
	("koton", "Koton", "0.9.4", "0.10.0", "The arranger's new tracks; MIDI learn", "2.3 MB", False, None),
]
def updates_list (c, x, y, w, rows, state = None):
	for i, (ic, name, old, new, what, size, on, flag) in enumerate (rows):
		ry = y + i * 74
		row_card (c, x, ry, w, 66)
		st = state[i] if state else None
		if st is None: checkbox (c, x + 14, ry + 25, "", on)
		if ic == "onyx": ic_onyx (c, x + 42, ry + 13)
		else: icon (c, ic, x + 42, ry + 13)
		c.text (x + 94, ry + 12, name, "uib")
		vx = x + 100 + c.tw (name, "uib")
		c.text (vx, ry + 13, old, "small", DIM); ax = vx + c.tw (old, "small") + 6
		c.line ([(ax, ry + 20), (ax + 12, ry + 20)], DIM, 1.2); c.poly ([(ax + 12, ry + 16.5), (ax + 17, ry + 20), (ax + 12, ry + 23.5)], DIM)
		c.text (ax + 22, ry + 13, new, "smallb", GREEN)
		bx = ax + 30 + c.tw (new, "smallb")
		if flag == "restart": badge (c, bx, ry + 12, "restart", AMBER)
		c.text (x + 94, ry + 36, what, "small", DIM)
		if st is None:
			c.text_r (x + w - 110, ry, 66, size, "small", DIM)
			c.text_r (x + w - 16, ry, 66, "Notes", "small", M.LINK)
		elif st == "done":
			c.ellipse (x + 26, ry + 33, 9, GREEN); c.line ([(x + 21, ry + 33), (x + 25, ry + 37), (x + 31, ry + 29)], WHITE, 2)
			c.text_r (x + w - 16, ry, 66, "Updated", "smallb", GREEN)
		elif st == "restart":
			c.ellipse (x + 26, ry + 33, 9, AMBER); c.rect (x + 25, ry + 27, 2.4, 7, WHITE); c.rect (x + 25, ry + 36, 2.4, 2.4, WHITE)
			c.text_r (x + w - 16, ry, 66, "Ready -- at the restart", "smallb", AMBER)
		elif isinstance (st, float):
			c.ellipse (x + 26, ry + 33, 9, None, SEL, 2.5)
			progress (c, x + w - 236, ry + 26, 150, 12, st)
			c.text_r (x + w - 16, ry, 66, "%d %%" % int (st * 100), "smallb", SEL)
		elif st == "wait":
			c.ellipse (x + 26, ry + 33, 9, None, FAINT, 2)
			c.text_r (x + w - 16, ry, 66, "Waiting", "small", FAINT)
		elif st == "skip":
			c.text_r (x + w - 16, ry, 66, "Not selected", "small", FAINT)

def shot_updates ():
	c = M.Canvas ()
	x, y, w, h = panel_window (c, "Updates")
	# the state: last check, the auto ones
	c.rect (x + 14, y + 4, w - 28, 52, A (WHITE, 90), r = 8, outline = LINE2)
	c.ellipse (x + 36, y + 30, 11, AMBER); c.text_c (x + 25, y + 19, 22, 22, "3", "uib", WHITE)
	c.text (x + 56, y + 12, "3 updates selected out of 4", "uib")
	c.text (x + 56, y + 32, "Checked today at 12:30 on github.com/stephaneweg/onyx-packages  -  index signed, OK", "small", DIM)
	button (c, x + w - 150, y + 15, 122, 30, "Check Now")
	updates_list (c, x + 14, y + 70, w - 28, UPD)
	# the automatic ones (already done in the background)
	ay = y + 70 + 4 * 74 + 8
	c.text (x + 18, ay, "UPDATED AUTOMATICALLY", "smallb", DIM)
	for i, (ic, n, v, when) in enumerate ([("tetris", "Tetris", "1.2.1", "yesterday"), ("2048", "2048", "1.0.3", "28 Sep")]):
		ix = x + 18 + i * 260
		icon (c, ic, ix, ay + 22, 24)
		c.text (ix + 32, ay + 22, n + "  " + v, "smallb"); c.text (ix + 32, ay + 37, when, "small", DIM)
	# the foot
	fy = y + h - 52
	c.hline (x, x + w, fy - 6, shade (FACE, 0.85))
	checkbox (c, x + 18, fy + 9, "Check every day, update the automatic ones")
	c.text_r (x + w - 236, fy, 34, "12.1 MB to download", "small", DIM)
	button (c, x + w - 222, fy + 2, 208, 32, "Install 3 Updates", accent = True)
	c.save ("pkg-updates.png")

# ---- 2. installing, then the restart ------------------------------------------------------------------------
def shot_installing ():
	c = M.Canvas ()
	x, y, w, h = panel_window (c, "Updates")
	c.rect (x + 14, y + 4, w - 28, 52, A (WHITE, 90), r = 8, outline = LINE2)
	c.text (x + 22, y + 12, "Installing 2 of 3: Jet Browser 3.3.0", "uib")
	c.text (x + 22, y + 32, "Downloading 2.7 MB of 4.1 MB  -  SHA-256 checked before anything is written", "small", DIM)
	button (c, x + w - 120, y + 15, 92, 30, "Cancel")
	updates_list (c, x + 14, y + 70, w - 28, UPD, ["restart", 0.66, "wait", "skip"])
	c.save ("pkg-installing.png")

def shot_restart ():
	c = M.Canvas ()
	x, y, w, h = panel_window (c, "Updates", (1, 41, 23))
	# the banner: the system waits for the restart
	c.rect (x + 14, y + 4, w - 28, 84, mix (WHITE, AMBER, 0.16), r = 8, outline = mix (LINE2, AMBER, 0.6))
	ic_onyx (c, x + 28, y + 14, 40)
	c.text (x + 80, y + 14, "Restart to finish the system update", "uib")
	c.text (x + 80, y + 34, "Onyx 2026.11.0 is ready: it starts at the next boot. If it does not start, the Pi", "small", DIM)
	c.text (x + 80, y + 50, "goes back to 2026.10.1 by itself (the firmware's tryboot) and tells you so.", "small", DIM)
	button (c, x + w - 260, y + 46, 120, 30, "Later")
	button (c, x + w - 132, y + 46, 110, 30, "Restart", accent = True)
	updates_list (c, x + 14, y + 102, w - 28, UPD, ["restart", "done", "done", "skip"])
	fy = y + h - 52
	c.hline (x, x + w, fy - 6, shade (FACE, 0.85))
	c.text_l (x + 18, fy, 34, "2 apps updated: they open with their new version next time (Jet Browser was closed first).", "small", DIM)
	c.save ("pkg-restart.png")

# ---- 3. what is installed: manual or automatic, remove --------------------------------------------------------
INST = [
	("onyx", "Onyx system", "2026.10.1", "21.8 MB", "System", "Manual", "required"),
	("archiver", "Archiver", "1.0.0", "1.1 MB", "Productivity", "Automatic", None),
	("calendar", "Calendar", "1.4.0", "1.9 MB", "Productivity", "Automatic", None),
	("courier", "Courier", "0.8.2", "2.6 MB", "Internet", "Manual", None),
	("gamelib", "Game Library", "2.1.0", "3.8 MB", "Games", "Automatic", None),
	("jet", "Jet Browser", "3.2.0", "9.4 MB", "Internet", "Manual", None),
	("koton", "Koton", "0.9.4", "5.2 MB", "Productivity", "Manual", None),
	("lisa", "Lisa", "1.1.0", "880 KB", "Productivity", "Automatic", "local"),
]
def shot_installed ():
	c = M.Canvas ()
	x, y, w, h = panel_window (c, "Installed")
	lx, ly, lw, lh = x + 14, y + 4, w - 28, h - 64
	c.rect (lx, ly, lw, lh, LIST, r = 6, outline = LINE)
	# the head
	cols = [("Package", 58), ("Version", 300), ("Size", 400), ("Category", 480), ("Updates", 610)]
	c.rect (lx + 1, ly + 1, lw - 2, 28, M.HEAD, r = 5, corners = (True, True, False, False))
	for s, cx_ in cols: c.text_l (lx + cx_, ly, 30, s, "smallb", DIM)
	for i, (ic, name, ver, size, cat, mode, flag) in enumerate (INST):
		ry = ly + 30 + i * 52
		sel = i == 5
		if sel: c.rect (lx + 3, ry + 2, lw - 16, 48, SEL, r = 6)
		elif i % 2: c.rect (lx + 1, ry, lw - 12, 52, A ((0, 0, 0), 6))
		fg, dim = (WHITE, (226, 240, 244)) if sel else (TEXT, DIM)
		if ic == "onyx": ic_onyx (c, lx + 12, ry + 8, 36)
		else: icon (c, ic, lx + 12, ry + 8, 36)
		c.text (lx + 58, ry + 9, name, "uib", fg)
		sub = { "required": "kernel, shell, bin/  -  cannot be removed", "local": "installed by hand (not in the repository)" }.get (flag, "by Onyx")
		c.text (lx + 58, ry + 28, sub, "small", dim)
		c.text_l (lx + 300, ry, 52, ver, "ui", fg); c.text_l (lx + 400, ry, 52, size, "ui", fg); c.text_l (lx + 480, ry, 52, cat, "ui", fg)
		if flag == "local": c.text_l (lx + 610, ry, 52, "--", "ui", dim)
		else: dropdown (c, lx + 606, ry + 11, 132, 30, mode)
		if flag != "required": button (c, lx + lw - 118, ry + 11, 96, 30, "Remove")
	scrollbar (c, lx + lw - 12, ly + 32, lh - 36, 0.0, 0.42)
	# the open drop-down
	menu_popup (c, lx + 606, ly + 30 + 5 * 52 + 42, 200, [("Automatic", ""), ("Manual", ""), None, ("Never (keep this version)", "")], hot = "Automatic")
	cursor (c, lx + 680, ly + 30 + 5 * 52 + 60)
	fy = y + h - 52
	c.hline (x, x + w, fy - 6, shade (FACE, 0.85))
	c.text_l (x + 18, fy, 34, "41 packages  -  168 MB on SD:  -  Automatic: updated in the background; Manual: you are asked", "small", DIM)
	c.save ("pkg-installed.png")

# ---- 4. the available ones: the store ------------------------------------------------------------------------------
AVAIL = [
	("tetris", "Tetris", "Onyx", "The falling blocks, with a ghost piece and the 7-bag", "312 KB", "installed"),
	("snesemu", "SNES Emulator", "Onyx", "Super Nintendo: the Game Library finds its games", "1.6 MB", "install"),
	("n64emu", "N64 Emulator", "Onyx", "Nintendo 64 on the V3D GPU", "3.4 MB", 0.42),
	("irc", "IRC", "Onyx", "Chat on Libera and others: channels, nick list, TLS", "690 KB", "install"),
	("paint", "Paint", "Onyx", "Draw: brushes, layers, selections, PNG and BMP", "1.2 MB", "update"),
	("sheet", "Spreadsheet", "Onyx", "Formulas, charts, .xlsx and .ods", "2.8 MB", "installed"),
]
def shot_available ():
	c = M.Canvas ()
	x, y, w, h = panel_window (c, "Available")
	# the categories
	cats = [("All", 23), ("Productivity", 6), ("Internet", 4), ("Graphics", 3), ("Games", 5), ("Emulators", 4), ("Tools", 1)]
	c.text (x + 22, y + 6, "CATEGORIES", "smallb", DIM)
	for i, (s, n) in enumerate (cats):
		ry = y + 26 + i * 32
		if s == "All": c.rect (x + 14, ry, 168, 28, SEL, r = 6)
		fg = WHITE if s == "All" else TEXT
		c.text_l (x + 26, ry, 28, s, "ui", fg); c.text_r (x + 170, ry, 28, str (n), "small", (226, 240, 244) if s == "All" else DIM)
	c.text (x + 22, y + 270, "THE REPOSITORY", "smallb", DIM)
	c.text (x + 22, y + 290, "onyx-packages", "smallb"); c.text (x + 22, y + 306, "23 packages, 4 new", "small", DIM)
	c.text (x + 22, y + 322, "index of today 06:00", "small", DIM)
	# the cards, 2 columns
	gx, gy, gw = x + 196, y + 4, (w - 196 - 14 - 12) / 2
	for i, (ic, name, auth, what, size, st) in enumerate (AVAIL):
		cx_, cy_ = gx + (i % 2) * (gw + 12), gy + (i // 2) * 128
		row_card (c, cx_, cy_, gw, 118, hot = i == 1)
		icon (c, ic, cx_ + 14, cy_ + 14, 40)
		c.text (cx_ + 66, cy_ + 14, name, "uib"); c.text (cx_ + 66, cy_ + 33, auth + "  -  " + size, "small", DIM)
		# the description, two lines at most
		words, line, ly = what.split (), "", cy_ + 60
		for wd in words:
			if c.tw (line + " " + wd, "small") > gw - 28 and line: c.text (cx_ + 14, ly, line, "small"); ly += 16; line = wd
			else: line = (line + " " + wd).strip ()
		if line: c.text (cx_ + 14, ly, line, "small")
		bx = cx_ + gw - 110
		if st == "installed":
			c.text_r (cx_ + gw - 14, cy_ + 14, 20, "Installed", "smallb", GREEN)
			c.ellipse (cx_ + gw - 14 - c.tw ("Installed", "smallb") - 12, cy_ + 24, 5, GREEN)
		elif st == "install": button (c, bx, cy_ + 12, 96, 28, "Install", accent = i == 1)
		elif st == "update": button (c, bx, cy_ + 12, 96, 28, "Update")
		else:
			progress (c, bx - 20, cy_ + 20, 116, 10, st); c.text_r (cx_ + gw - 14, cy_ + 34, 16, "42 %", "smallb", SEL)
	cursor (c, gx + gw * 2 + 12 - 70, gy + 22)
	c.save ("pkg-available.png")

# ---- 5. the notification -----------------------------------------------------------------------------------------
def shot_notify ():
	c = M.Canvas ()
	c.img.paste (Image.open (os.path.join (M.ROOT, "screenshots", "desktop.png")).convert ("RGB").resize ((M.W * K, M.H * K), Image.LANCZOS), (0, 0))
	x, y, w, h = M.W - 360, 40, 344, 106
	c.rect (x, y, w, h, M.MENU, r = 10, outline = shade (FACE, 0.62))
	ic_onyx (c, x + 14, y + 14, 36)
	c.text (x + 62, y + 14, "3 updates available", "uib")
	c.text (x + 62, y + 34, "Onyx system, Jet Browser, Archiver", "small", DIM)
	c.text (x + 62, y + 50, "Tetris and 2048 were updated.", "small", DIM)
	button (c, x + w - 196, y + h - 38, 90, 28, "Later")
	button (c, x + w - 100, y + h - 38, 86, 28, "Show", accent = True)
	c.save ("pkg-notify.png")

# ---- 6. the pkg command ---------------------------------------------------------------------------------------------
TERM = (24, 60, 66); TERMFG = (226, 236, 232)
def shot_cli ():
	c = M.Canvas ()
	M.desktop (c, "Terminal", ["File", "Edit", "View"])
	cx, cy, cw, ch = M.window (c, 70, 44, 884, 680, "terminal")
	c.rect (cx, cy, cw, ch, TERM)
	lines = [
		("p", "/ $ pkg list"),
		("o", "archiver      1.0.0        auto"),
		("o", "jet           3.2.0        manual   (3.3.0 available)"),
		("o", "onyx          2026.10.1    manual   (2026.11.0 available)"),
		("o", "tetris        1.2.1        auto"),
		("o", "... 41 packages"),
		("p", "/ $ pkg add snesemu"),
		("o", "snesemu 1.6.0: 1.6 MB, needs onyx >= 2026.9 (kapi 66): ok"),
		("o", "downloading ....................... 1.6 MB  sha256 ok"),
		("o", "installing 3 files to SD:/apps/snesemu.app ... done"),
		("p", "/ $ pkg add snesemu"),
		("o", "snesemu is already installed (1.6.0)"),
		("p", "/ $ pkg update -a"),
		("o", "index: 23 packages, signed by onyx-packages (ok)"),
		("o", "jet 3.2.0 -> 3.3.0 ...................... done"),
		("o", "archiver 1.0.0 -> 1.1.0 ................. done"),
		("o", "onyx 2026.10.1 -> 2026.11.0 ............. staged: restart to finish"),
		("o", "koton 0.9.4 -> 0.10.0 ................... done"),
		("p", "/ $ pkg delete irc"),
		("o", "irc is not installed"),
		("p", "/ $ pkg list -a emu"),
		("o", "gbemu         1.3.0        installed"),
		("o", "n64emu        0.7.1        -"),
		("o", "snesemu       1.6.0        installed"),
		("p", "/ $ _"),
	]
	ly = cy + 10
	for kind, s in lines:
		c.text (cx + 10, ly, s, "mono", (150, 230, 190) if kind == "p" else TERMFG); ly += 22
	c.save ("pkg-cli.png")

if __name__ == "__main__":
	shot_updates (); shot_installing (); shot_restart (); shot_installed (); shot_available (); shot_notify (); shot_cli ()
