#!/usr/bin/env python3
"""mockup_mail.py -- the first mock-ups of Onyx's mail client (Mail): Gmail, Outlook, any IMAP or POP3 / SMTP account,
the conversations, a message read, one written, the account's wizard (Gmail's app password, Outlook's sign-in with a
code), the contacts as a Cardfile form. See docs/mail/README.md.

    python3 tools/screenshot/mockup_mail.py  -> docs/mail/mockups/mail-*.png

On the real desktop (screenshots/desktop.png, 1024 x 768); the drawing helpers are mockup_archiver.py's. The people,
their messages and addresses are made up.
"""
import os, sys, math, random
from PIL import Image, ImageDraw

HERE = os.path.dirname (os.path.abspath (__file__))
sys.path.insert (0, HERE)
import mockup_archiver as M

M.W, M.H = 1024, 768
M.OUT = os.path.join (M.ROOT, "docs", "mail", "mockups")
K = M.K
DESK = Image.open (os.path.join (M.ROOT, "screenshots", "desktop.png")).convert ("RGB")
TEXT, DIM, FACE, SEL, MENU, WHITE, LIST, LINE2, FAINT = M.TEXT, M.DIM, M.FACE, M.SEL, M.MENU, (255, 255, 255), M.LIST, M.LINE2, M.FAINT
SIDE = (226, 216, 209)
SOFT = M.SEL_SOFT
RED, GREEN, AMBER, BLUE = M.RED, M.GREEN, M.AMBER, M.BLUE
M.F["huge"] = M._f ("DejaVuSans-Bold.ttf", 24)
M.F["h2"] = M._f ("DejaVuSans-Bold.ttf", 17)
M.F["mid"] = M._f ("DejaVuSans.ttf", 15)
M.F["code"] = M._f ("DejaVuSansMono-Bold.ttf", 34)

def screen (app = "Mail", menus = ("File", "Edit", "View", "Message", "Help")):
	c = M.Canvas ()
	c.img.paste (DESK.resize ((M.W * K, M.H * K), Image.LANCZOS), (0, 0)); c.d = ImageDraw.Draw (c.img, "RGBA")
	c.rect (0, 0, 520, 27, (230, 222, 217))
	x = 18; c.text_l (x, 0, 27, "Onyx", "menu"); x += c.tw ("Onyx", "menu") + 18
	c.text_l (x, 0, 27, app, "menub"); x += c.tw (app, "menub") + 20
	for m in menus: c.text_l (x, 0, 27, m, "menu"); x += c.tw (m, "menu") + 18
	return c

# ---- the people (made up) ---------------------------------------------------------------------------------------------
PEOPLE = { "Marie Dubois": (200, 90, 110), "Atelier Lumen": (73, 146, 167), "Jonas Peeters": (90, 140, 80),
	   "Brasserie De Klok": (190, 130, 50), "Proximus": (110, 90, 170), "Sofia Rinaldi": (60, 110, 180),
	   "Onyx Packages": (40, 40, 48), "Lucas Martin": (170, 80, 60), "Bank": (40, 110, 120), "Claire Lambert": (150, 100, 160) }
def avatar (c, x, y, d, name):
	col = PEOPLE.get (name, ((sum (map (ord, name)) * 37) % 160 + 50, (sum (map (ord, name)) * 61) % 120 + 60, (sum (map (ord, name)) * 13) % 140 + 70))
	c.ellipse (x + d / 2, y + d / 2, d / 2, col)
	ini = "".join (w[0] for w in name.split ()[:2]).upper ()
	c.text_c (x, y, d, d, ini, "smallb" if d < 34 else "uib", WHITE)

# ---- the icons --------------------------------------------------------------------------------------------------------
def ic_inbox (c, x, y, s, col):
	c.line ([(x + 2, y + s * 0.55), (x + s * 0.3, y + s * 0.55), (x + s * 0.38, y + s * 0.72), (x + s * 0.62, y + s * 0.72), (x + s * 0.7, y + s * 0.55), (x + s - 2, y + s * 0.55)], col, 1.6)
	c.rect (x + 2, y + s * 0.2, s - 4, s * 0.68, None, r = 2, outline = col, width = 1.6)
def ic_star (c, x, y, s, col, fill = True):
	pts = []
	for k in range (10):
		a = -math.pi / 2 + k * math.pi / 5; rr = s * (0.48 if k % 2 == 0 else 0.2)
		pts.append ((x + s / 2 + math.cos (a) * rr, y + s / 2 + math.sin (a) * rr))
	if fill: c.poly (pts, col)
	else: c.line (pts + [pts[0]], col, 1.4)
def ic_send (c, x, y, s, col): c.poly ([(x + 1, y + s * 0.15), (x + s - 1, y + s * 0.5), (x + 1, y + s * 0.85), (x + s * 0.18, y + s * 0.5)], col)
def ic_draft (c, x, y, s, col):
	c.rect (x + 2, y + 1, s * 0.62, s - 2, None, r = 1, outline = col, width = 1.5)
	c.line ([(x + s * 0.45, y + s * 0.75), (x + s - 2, y + s * 0.2)], col, 2.2)
def ic_archive (c, x, y, s, col):
	c.rect (x + 1, y + 3, s - 2, s * 0.25, col, r = 1); c.rect (x + 3, y + s * 0.38, s - 6, s * 0.55, None, r = 1, outline = col, width = 1.5)
	c.rect (x + s * 0.38, y + s * 0.5, s * 0.24, 2, col)
def ic_trash (c, x, y, s, col):
	c.rect (x + s * 0.22, y + s * 0.28, s * 0.56, s * 0.66, None, r = 2, outline = col, width = 1.5)
	c.rect (x + s * 0.12, y + s * 0.17, s * 0.76, 2, col); c.rect (x + s * 0.38, y + s * 0.06, s * 0.24, 2, col)
def ic_junk (c, x, y, s, col):
	c.ellipse (x + s / 2, y + s / 2, s * 0.42, None, col, 1.6); c.line ([(x + s * 0.2, y + s * 0.8), (x + s * 0.8, y + s * 0.2)], col, 1.6)
def ic_reply (c, x, y, s, col, all_ = False):
	o = s * 0.12 if all_ else 0
	c.d.arc ([(x + s * 0.25 + o) * K, (y + s * 0.3) * K, (x + s * 1.15) * K, (y + s * 1.2) * K], 200, 270, fill = col, width = int (1.8 * K))
	c.poly ([(x + o, y + s * 0.4), (x + s * 0.35 + o, y + s * 0.15), (x + s * 0.35 + o, y + s * 0.65)], col)
	if all_: c.line ([(x + s * 0.3, y + s * 0.15), (x + 1, y + s * 0.4), (x + s * 0.3, y + s * 0.65)], col, 1.5)
def ic_forward (c, x, y, s, col):
	c.d.arc ([(x - s * 0.15) * K, (y + s * 0.3) * K, (x + s * 0.75) * K, (y + s * 1.2) * K], 270, 340, fill = col, width = int (1.8 * K))
	c.poly ([(x + s, y + s * 0.4), (x + s * 0.65, y + s * 0.15), (x + s * 0.65, y + s * 0.65)], col)
def ic_clip (c, x, y, s, col):
	c.d.rounded_rectangle ([(x + s * 0.3) * K, (y + 1) * K, (x + s * 0.72) * K, (y + s - 1) * K], s * 0.21 * K, outline = col, width = int (1.5 * K))
	c.line ([(x + s * 0.51, y + s * 0.3), (x + s * 0.51, y + s * 0.75)], col, 1.4)
def ic_person (c, x, y, s, col):
	c.ellipse (x + s / 2, y + s * 0.32, s * 0.2, col); c.d.pieslice ([(x + s * 0.12) * K, (y + s * 0.58) * K, (x + s * 0.88) * K, (y + s * 1.3) * K], 180, 360, fill = col)
def ic_gear (c, x, y, s, col):
	for k in range (8):
		a = k * math.pi / 4
		c.line ([(x + s / 2 + math.cos (a) * s * 0.28, y + s / 2 + math.sin (a) * s * 0.28), (x + s / 2 + math.cos (a) * s * 0.46, y + s / 2 + math.sin (a) * s * 0.46)], col, 2.4)
	c.ellipse (x + s / 2, y + s / 2, s * 0.3, None, col, 2); c.ellipse (x + s / 2, y + s / 2, s * 0.1, col)
def ic_search (c, x, y, s, col): c.ellipse (x + s * 0.42, y + s * 0.42, s * 0.3, None, col, 1.8); c.line ([(x + s * 0.64, y + s * 0.64), (x + s * 0.92, y + s * 0.92)], col, 2)
def ic_sync (c, x, y, s, col):
	c.d.arc ([(x + 2) * K, (y + 2) * K, (x + s - 2) * K, (y + s - 2) * K], 30, 300, fill = col, width = int (1.8 * K))
	c.poly ([(x + s - 2, y + s * 0.15), (x + s - 1, y + s * 0.5), (x + s * 0.68, y + s * 0.38)], col)
def ic_pen (c, x, y, s, col):
	c.line ([(x + s * 0.2, y + s * 0.8), (x + s * 0.8, y + s * 0.2)], col, 3); c.poly ([(x + s * 0.12, y + s * 0.88), (x + s * 0.18, y + s * 0.66), (x + s * 0.34, y + s * 0.82)], col)
def ic_pdf (c, x, y, s):
	c.poly ([(x + s * 0.18, y), (x + s * 0.64, y), (x + s * 0.84, y + s * 0.2), (x + s * 0.84, y + s), (x + s * 0.18, y + s)], WHITE)
	c.line ([(x + s * 0.18, y), (x + s * 0.64, y), (x + s * 0.84, y + s * 0.2), (x + s * 0.84, y + s), (x + s * 0.18, y + s), (x + s * 0.18, y)], (150, 140, 134), 1)
	c.rect (x + s * 0.06, y + s * 0.5, s * 0.66, s * 0.3, (200, 60, 50), r = 2)
def ic_img (c, x, y, s):
	c.rect (x + 1, y + 2, s - 2, s - 4, (240, 246, 250), r = 2, outline = (150, 160, 170))
	c.poly ([(x + 3, y + s - 5), (x + s * 0.4, y + s * 0.45), (x + s * 0.6, y + s * 0.7), (x + s * 0.75, y + s * 0.55), (x + s - 3, y + s - 5)], (90, 150, 110))
	c.ellipse (x + s * 0.7, y + s * 0.32, s * 0.1, (230, 180, 60))
def ic_lock (c, x, y, s, col):
	c.d.arc ([(x + s * 0.28) * K, (y + 1) * K, (x + s * 0.72) * K, (y + s * 0.6) * K], 180, 360, fill = col, width = int (1.8 * K))
	c.rect (x + s * 0.15, y + s * 0.42, s * 0.7, s * 0.52, col, r = 2)

# the providers' marks (drawn: their colours, not their logos)
def mark_gmail (c, x, y, s):
	c.rect (x, y + s * 0.18, s, s * 0.66, WHITE, r = 3, outline = (210, 210, 210))
	c.line ([(x + 2, y + s * 0.22), (x + s / 2, y + s * 0.55), (x + s - 2, y + s * 0.22)], (219, 68, 55), 3)
	c.rect (x + 1, y + s * 0.22, 3, s * 0.6, (66, 133, 244)); c.rect (x + s - 4, y + s * 0.22, 3, s * 0.6, (15, 157, 88))
def mark_outlook (c, x, y, s):
	c.rect (x + s * 0.35, y + s * 0.2, s * 0.62, s * 0.6, (40, 120, 215), r = 3)
	c.rect (x, y + s * 0.1, s * 0.55, s * 0.8, (0, 90, 180), r = 3); c.ellipse (x + s * 0.275, y + s / 2, s * 0.15, None, WHITE, 2.4)
def mark_icloud (c, x, y, s):
	col = (90, 160, 230)
	c.ellipse (x + s * 0.35, y + s * 0.58, s * 0.22, col); c.ellipse (x + s * 0.6, y + s * 0.5, s * 0.28, col); c.rect (x + s * 0.12, y + s * 0.58, s * 0.76, s * 0.22, col, r = 6)
def mark_yahoo (c, x, y, s): c.rect (x, y + s * 0.1, s, s * 0.8, (100, 30, 190), r = 6); c.text_c (x, y + s * 0.1, s, s * 0.8, "Y!", "uib", WHITE)
def mark_imap (c, x, y, s): c.rect (x, y + s * 0.15, s, s * 0.7, (120, 110, 104), r = 4); ic_inbox (c, x + s * 0.18, y + s * 0.15, s * 0.64, WHITE)
def mark_pop (c, x, y, s):
	c.rect (x, y + s * 0.15, s, s * 0.7, (150, 120, 90), r = 4)
	c.line ([(x + s / 2, y + s * 0.28), (x + s / 2, y + s * 0.66)], WHITE, 2.4); c.poly ([(x + s * 0.32, y + s * 0.55), (x + s * 0.68, y + s * 0.55), (x + s / 2, y + s * 0.75)], WHITE)

# ---- the window's parts -----------------------------------------------------------------------------------------------
WX, WY, WW, WH = 14, 32, 996, 640
TB_H, SIDE_W, LIST_W = 50, 198, 300

def tbtn (c, x, y, icon, label = None, w = None, hot = False, accent = False):
	w = w or (34 if not label else c.tw (label) + 44)
	if accent: M.button (c, x, y, w, 34, "", accent = True)
	elif hot: c.rect (x, y, w, 34, M.A (WHITE, 120), r = 6, outline = M.A (M.shade (FACE, 0.7), 255))
	icon (c, x + 9, y + 8, 18, WHITE if accent else TEXT)
	if label: c.text_l (x + 34, y, 34, label, "uib" if accent else "ui", WHITE if accent else TEXT)
	return x + w + 4

def toolbar (c, x, y, w, compose_hot = False):
	c.rect (x, y, w, TB_H, FACE)
	bx = x + 10
	bx = tbtn (c, bx, y + 8, ic_pen, "New message", accent = True) + 10
	bx = tbtn (c, bx, y + 8, ic_reply, "Reply")
	bx = tbtn (c, bx, y + 8, lambda c, a, b, s, col: ic_reply (c, a, b, s, col, True), "Reply all")
	bx = tbtn (c, bx, y + 8, ic_forward, "Forward")
	c.vline (bx + 3, y + 12, y + 38, M.shade (FACE, 0.82)); bx += 12
	bx = tbtn (c, bx, y + 8, ic_archive); bx = tbtn (c, bx, y + 8, ic_trash); bx = tbtn (c, bx, y + 8, ic_junk)
	bx = tbtn (c, bx, y + 8, lambda c, a, b, s, col: ic_star (c, a, b, s, col, False))
	sx = x + w - 10 - 34 - 8 - 230
	c.rect (sx, y + 11, 230, 28, WHITE, r = 14, outline = M.LINE)
	ic_search (c, sx + 10, y + 17, 16, DIM); c.text_l (sx + 34, y + 11, 28, "Search the mail", "ui", FAINT)
	tbtn (c, x + w - 44, y + 8, ic_sync)
	c.hline (x, x + w, y + TB_H, M.shade (FACE, 0.84))

def sidebar (c, x, y, h, sel = "All inboxes"):
	c.rect (x, y, SIDE_W, h, SIDE); c.vline (x + SIDE_W, y, y + h, M.shade (FACE, 0.82))
	yy = y + 10
	def item (label, icon, count = None, indent = 0, bold = False):
		nonlocal yy
		on = label == sel
		if on: c.rect (x + 8, yy, SIDE_W - 16, 26, SEL, r = 6)
		col = WHITE if on else TEXT
		icon (c, x + 18 + indent, yy + 5, 16, col)
		c.text_l (x + 44 + indent, yy, 26, label, "uib" if (on or bold) else "ui", col)
		if count: M.badge (c, x + SIDE_W - 16 - c.tw (str (count), "smallb") - 14, yy + 4, str (count), WHITE if on else SEL, SEL if on else WHITE)
		yy += 28
	item ("All inboxes", ic_inbox, 14, bold = True)
	item ("Starred", lambda c, a, b, s, col: ic_star (c, a, b, s, AMBER if col != WHITE else WHITE))
	yy += 6
	def account (name, mail, mark, open_):
		nonlocal yy
		c.poly ([(x + 14, yy + 9), (x + 22, yy + 9), (x + 18, yy + 15)] if open_ else [(x + 15, yy + 8), (x + 20, yy + 12), (x + 15, yy + 16)], DIM)
		mark (c, x + 26, yy + 3, 18)
		c.text (x + 50, yy + 1, name, "smallb", TEXT); c.text (x + 50, yy + 15, mail, "small", DIM)
		yy += 36
	account ("Gmail", "steph.w@gmail.com", mark_gmail, True)
	for lab, ic, n in (("Inbox", ic_inbox, 9), ("Sent", ic_send, None), ("Drafts", ic_draft, 1), ("Archive", ic_archive, None), ("Junk", ic_junk, None), ("Trash", ic_trash, None)):
		item (lab, ic, n, indent = 10)
	yy += 4
	account ("Outlook", "s.wegener@outlook.com", mark_outlook, False)
	account ("Atelier (IMAP)", "steph@atelier-lumen.be", mark_imap, False)
	by = y + h - 64
	c.hline (x + 12, x + SIDE_W - 12, by - 6, M.shade (SIDE, 0.92))
	yy = by
	item ("Contacts", ic_person)
	item ("Accounts and settings", ic_gear)

MSGS = [  # (group, who, subject, preview, time, unread, thread, clip, star, account)
	("TODAY", "Marie Dubois", "Saturday's lunch", "Perfect, see you at 12:30 then — I booked the table by the window…", "11:42", True, 3, False, True, "G"),
	(None, "Atelier Lumen", "Invoice 2026-0412", "Please find attached the invoice for the beer labels (design and proofs)…", "10:05", True, 0, True, False, "A"),
	(None, "Onyx Packages", "3 updates for your Onyx", "PDF Viewer 1.0.1, Media Player 1.0.1 and GeneralUser GS are ready…", "08:30", False, 0, False, False, "G"),
	("YESTERDAY", "Jonas Peeters", "Re: The quote for the brewery", "That works for us. Could you add the delivery note to the next one?", "17:48", False, 5, False, False, "A"),
	(None, "Sofia Rinaldi", "Photos from Ghent", "Here they are! The one at the Gravensteen is my favourite…", "14:12", False, 0, True, True, "O"),
	(None, "Proximus", "Your September bill", "Your bill of 59,90 € is available in MyProximus. It will be…", "09:20", False, 0, False, False, "O"),
	("THIS WEEK", "Lucas Martin", "Koton — a new song", "I made a bossa with the chord co-pilot, listen to the bridge…", "Mon", False, 2, True, False, "G"),
	(None, "Claire Lambert", "Book club, October", "We read « L'Anomalie » this month. Next meeting on the 14th at…", "Mon", False, 0, False, False, "G"),
]

def msg_list (c, x, y, w, h, sel = 0, hot = None):
	c.rect (x, y, w, h, LIST); c.vline (x + w, y, y + h, M.shade (FACE, 0.86))
	# its head: the folder, the filter
	c.text (x + 14, y + 12, "All inboxes", "h2")
	M.segmented (c, x + w - 14 - 124, y + 10, 24, ["All", "Unread"], 0)
	yy = y + 46
	for k, (grp, who, subj, prev, t, unread, thr, clip, star, acc) in enumerate (MSGS):
		if grp:
			c.text (x + 14, yy + 6, grp, "smallb", DIM); yy += 24
		if yy + 70 > y + h: break
		on = k == sel
		if on: c.rect (x + 6, yy, w - 12, 68, SOFT, r = 6)
		elif k == hot: c.rect (x + 6, yy, w - 12, 68, (238, 232, 228), r = 6)
		if unread: c.ellipse (x + 12, yy + 22, 4, SEL)
		avatar (c, x + 20, yy + 10, 34, who)
		tx = x + 64
		c.text (tx, yy + 8, who, "uib" if unread else "ui", TEXT)
		if thr: M.badge (c, tx + c.tw (who, "uib" if unread else "ui") + 8, yy + 7, str (thr), (200, 190, 182), WHITE)
		c.text_r (x + w - 14, yy + 6, 18, t, "small", SEL if unread else DIM)
		s2 = subj if c.tw (subj, "smallb") < w - 110 else subj[:36] + "…"
		c.text (tx, yy + 28, s2, "smallb" if unread else "small", TEXT)
		p2 = prev if c.tw (prev, "small") < w - 90 else prev[:int (len (prev) * (w - 100) / c.tw (prev, "small"))] + "…"
		c.text (tx, yy + 46, p2, "small", DIM)
		if star: ic_star (c, x + w - 30, yy + 26, 14, AMBER)
		if clip: ic_clip (c, x + w - 30 - (18 if star else 0), yy + 26, 14, DIM)
		# the account it came to: a stripe of its colour
		col = { "G": (219, 68, 55), "O": (0, 90, 180), "A": (120, 110, 104) }[acc]
		c.rect (x + w - 10, yy + 12, 3, 44, col, r = 1)
		yy += 72

def app (c, title = "Mail — All inboxes"):
	cx, cy, cw, ch = M.window (c, WX, WY, WW, WH, title)
	return cx, cy, cw, ch

def attachment (c, x, y, kind, name, size, w = 230):
	c.rect (x, y, w, 44, WHITE, r = 8, outline = M.LINE2)
	(ic_pdf if kind == "pdf" else ic_img) (c, x + 10, y + 9, 26) if kind == "pdf" else ic_img (c, x + 10, y + 9, 26)
	c.text (x + 46, y + 7, name, "smallb"); c.text (x + 46, y + 24, size, "small", DIM)
	return x + w + 10

# ---- 1. the inbox, a conversation read ----------------------------------------------------------------------------------
def shot_inbox ():
	c = screen ()
	cx, cy, cw, ch = app (c)
	toolbar (c, cx, cy, cw)
	by = cy + TB_H + 1; bh = ch - TB_H - 1
	sidebar (c, cx, by, bh)
	msg_list (c, cx + SIDE_W + 1, by, LIST_W, bh, sel = 1, hot = 3)
	rx = cx + SIDE_W + 1 + LIST_W + 1; rw = cw - (rx - cx)
	c.rect (rx, by, rw, bh, WHITE)
	px = rx + 24; pw = rw - 48
	c.text (px, by + 16, "Invoice 2026-0412", "huge")
	M.badge (c, px, by + 52, "Atelier (IMAP)", (120, 110, 104), WHITE); M.badge (c, px + 116, by + 52, "Inbox", (200, 190, 182), WHITE)
	# the message
	y = by + 86
	avatar (c, px, y, 40, "Atelier Lumen")
	c.text (px + 52, y + 2, "Atelier Lumen", "uib"); c.text (px + 52, y + 21, "factures@atelier-lumen.be  ·  to me", "small", DIM)
	c.text_r (px + pw, y, 18, "Today, 10:05", "small", DIM)
	for k, ic in enumerate ((ic_reply, lambda c, a, b, s, col: ic_reply (c, a, b, s, col, True), ic_forward)):
		ic (c, px + pw - 76 + k * 28, y + 22, 16, DIM)
	body = ["Hello Stéphane,", "",
		"Please find attached the invoice for the beer labels —",
		"the design and the two rounds of proofs — as agreed",
		"in the quote of 12 September.", "",
		"Amount: 2.528,90 € (VAT included), within 30 days.",
		"Structured communication: +++412/2026/04123+++", "",
		"Thank you for your trust,", "", "Marie Dubois", "Atelier Lumen SRL — Rue de la Loi 12, 1000 Brussels"]
	yy = y + 58
	for line in body: c.text (px + 52, yy, line, "ui", TEXT); yy += 21
	yy += 10
	c.text (px + 52, yy, "2 attachments  ·  1.2 MB  ·  Save all", "smallb", DIM); yy += 20
	nx = attachment (c, px + 52, yy, "pdf", "Invoice-2026-0412.pdf", "184 KB", w = 200)
	attachment (c, nx, yy, "img", "labels-final.png", "1.0 MB", w = 170)
	# the quick reply
	qy = by + bh - 56
	c.hline (rx, rx + rw, qy - 10, LINE2)
	c.rect (px, qy, pw, 40, (248, 246, 244), r = 20, outline = M.LINE)
	c.text_l (px + 18, qy, 40, "Reply to Atelier Lumen…", "ui", FAINT)
	c.ellipse (px + pw - 22, qy + 20, 15, SEL); ic_send (c, px + pw - 30, qy + 12, 16, WHITE)
	M.cursor (c, cx + SIDE_W + 160, by + 150 + 72 * 2 + 40)
	c.save ("mail-inbox.png")

# ---- 2. a conversation (its messages, the last open) ---------------------------------------------------------------------
def shot_thread ():
	c = screen ()
	cx, cy, cw, ch = app (c, "Mail — Re: The quote for the brewery")
	toolbar (c, cx, cy, cw)
	by = cy + TB_H + 1; bh = ch - TB_H - 1
	sidebar (c, cx, by, bh)
	msg_list (c, cx + SIDE_W + 1, by, LIST_W, bh, sel = 3)
	rx = cx + SIDE_W + 1 + LIST_W + 1; rw = cw - (rx - cx)
	c.rect (rx, by, rw, bh, (243, 240, 238))
	px = rx + 18; pw = rw - 36
	c.text (px + 6, by + 14, "The quote for the brewery", "huge"); c.text (px + 8, by + 48, "5 messages  ·  Jonas Peeters, you", "small", DIM)
	y = by + 74
	thread = [("Jonas Peeters", "Could you send us a quote for 3 000 labels, two colours…", "15 Sep"),
		  ("Stéphane Wegener", "Here is the quote: 1.850,00 € excl. VAT for the design…", "15 Sep"),
		  ("Jonas Peeters", "Thanks! One question about the proofs: how many rounds…", "16 Sep"),
		  ("Stéphane Wegener", "Two rounds are included; a third one is 120 €.", "16 Sep")]
	for who, snip, d in thread:
		c.rect (px, y, pw, 42, WHITE, r = 8, outline = LINE2)
		avatar (c, px + 10, y + 7, 28, who if who in PEOPLE else "Claire Lambert") if who != "Stéphane Wegener" else (c.ellipse (px + 24, y + 21, 14, SEL), c.text_c (px + 10, y + 7, 28, 28, "SW", "smallb", WHITE))
		c.text (px + 48, y + 6, who, "smallb"); c.text (px + 48, y + 22, snip if c.tw (snip, "small") < pw - 130 else snip[:48] + "…", "small", DIM)
		c.text_r (px + pw - 12, y + 6, 18, d, "small", DIM)
		y += 48
	# the last, open
	c.rect (px, y, pw, bh - (y - by) - 64, WHITE, r = 8, outline = LINE2)
	avatar (c, px + 12, y + 12, 36, "Jonas Peeters")
	c.text (px + 58, y + 12, "Jonas Peeters", "uib"); c.text (px + 58, y + 30, "to me, Marie Dubois", "small", DIM)
	c.text_r (px + pw - 12, y + 12, 18, "Yesterday, 17:48", "small", DIM)
	for k, line in enumerate (["Hi Stéphane,", "", "That works for us. Could you add the delivery note", "to the next one? We will pick the labels up on Friday.",
				   "", "Cheers,", "Jonas", "", "> Two rounds are included; a third one is 120 €."]):
		c.text (px + 58, y + 62 + k * 20, line, "ui", DIM if line.startswith (">") else TEXT)
	qy = by + bh - 52
	M.button (c, px, qy, 110, 34, "Reply"); M.button (c, px + 118, qy, 110, 34, "Reply all"); M.button (c, px + 236, qy, 110, 34, "Forward")
	c.save ("mail-thread.png")

# ---- 3. writing: the address completed from the contacts, the attachments --------------------------------------------------
def shot_compose ():
	c = screen ()
	cx, cy, cw, ch = app (c)
	toolbar (c, cx, cy, cw)
	by = cy + TB_H + 1; bh = ch - TB_H - 1
	sidebar (c, cx, by, bh)
	msg_list (c, cx + SIDE_W + 1, by, LIST_W, bh, sel = 0)
	c.rect (0, 0, M.W, M.H, M.A ((0, 0, 0), 30))
	x, y, w, h = M.window (c, 190, 70, 700, 560, "New message")
	c.rect (x, y, w, h, WHITE)
	def row (yy, label, content = None):
		c.text_l (x + 16, yy, 38, label, "ui", DIM)
		c.hline (x + 12, x + w - 12, yy + 38, LINE2)
		return yy
	yy = y + 4
	row (yy, "From"); M.dropdown (c, x + 80, yy + 5, 330, 28, "Stéphane Wegener <steph.w@gmail.com>"); yy += 40
	row (yy, "To")
	# a chip, the typing, the completion
	cx0 = x + 80
	c.rect (cx0, yy + 7, 150, 24, SOFT, r = 12); c.ellipse (cx0 + 12, yy + 19, 9, PEOPLE["Marie Dubois"]); c.text_c (cx0 + 3, yy + 10, 18, 18, "MD", "tiny", WHITE)
	c.text_l (cx0 + 26, yy + 7, 24, "Marie Dubois", "ui"); c.text_l (cx0 + 132, yy + 7, 24, "×", "ui", DIM)
	c.text_l (cx0 + 160, yy, 38, "jon", "ui"); c.vline (cx0 + 160 + c.tw ("jon") + 1, yy + 10, yy + 28, TEXT)
	c.text_r (x + w - 16, yy, 38, "Cc  Bcc", "ui", M.LINK)
	yy += 40
	row (yy, "Subject"); c.text_l (x + 80, yy, 38, "Saturday — and the brewery's labels", "ui"); yy += 40
	# the formatting
	fx = x + 14
	for lab in ("B", "I", "U"):
		c.text_c (fx, yy + 4, 26, 28, lab, "uib" if lab == "B" else "ui"); fx += 28
	c.vline (fx + 4, yy + 9, yy + 27, LINE2); fx += 12
	for k in range (3): c.rect (fx + 4, yy + 11 + k * 5, 14, 2, TEXT)
	fx += 30; c.text_c (fx, yy + 4, 26, 28, "🔗" if False else "∞", "ui"); fx += 30
	ic_img (c, fx + 2, yy + 9, 18); fx += 30; ic_clip (c, fx + 2, yy + 9, 18, TEXT)
	c.hline (x + 12, x + w - 12, yy + 36, LINE2); yy += 44
	body = ["Hi Marie,", "", "Perfect for Saturday — 12:30 at the window table, I'll be there.", "",
		"Jonas asked for the delivery note with the next labels; I put the", "two of them in copy so we can agree on Friday's pick-up.", "", "See you,", "Stéphane"]
	for k, line in enumerate (body): c.text (x + 18, yy + k * 21, line, "ui")
	# the completion popup, under the To field
	px, py = cx0 + 150, y + 4 + 40 + 36
	people = [("Jonas Peeters", "jonas@deklok.be", "Brasserie De Klok"), ("Jonas Van Damme", "jonas.vd@gmail.com", ""), ("Johanna Smets", "johanna@smets.be", "")]
	c.rect (px, py, 330, 12 + len (people) * 44, MENU, r = 8, outline = M.shade (FACE, 0.62))
	for k, (n, m, co) in enumerate (people):
		ry = py + 6 + k * 44
		if k == 0: c.rect (px + 5, ry, 320, 42, SEL, r = 6)
		avatar (c, px + 12, ry + 6, 30, n)
		fg = WHITE if k == 0 else TEXT
		c.text (px + 52, ry + 5, n, "uib", fg); c.text (px + 52, ry + 23, m + ("  ·  " + co if co else ""), "small", (226, 240, 244) if k == 0 else DIM)
	# the attachments, the bottom
	ay = y + h - 104
	c.hline (x + 12, x + w - 12, ay - 10, LINE2)
	nx = attachment (c, x + 16, ay, "pdf", "Delivery-note-0412.pdf", "96 KB", w = 220)
	attachment (c, nx, ay, "img", "labels-final.png", "1.0 MB", w = 190)
	M.button (c, x + 16, y + h - 46, 110, 34, "", accent = True); ic_send (c, x + 30, y + h - 37, 16, WHITE); c.text_l (x + 54, y + h - 46, 34, "Send", "uib", WHITE)
	c.text_l (x + 140, y + h - 46, 34, "Ctrl+Enter", "small", FAINT)
	c.text_r (x + w - 56, y + h - 46, 34, "Draft saved 11:58", "small", DIM)
	ic_trash (c, x + w - 40, y + h - 38, 18, DIM)
	c.save ("mail-compose.png")

# ---- 4. the account's wizard: the kind of account -------------------------------------------------------------------------
def wizard_frame (c, title, step):
	c.rect (0, 0, M.W, M.H, M.A ((0, 0, 0), 30))
	x, y, w, h = M.window (c, 212, 80, 600, 560, title)
	c.rect (x, y, w, h, FACE)
	# the steps
	steps = ["Account", "Sign in", "Check"]
	sx = x + 24
	for k, s in enumerate (steps):
		on = k == step; done = k < step
		c.ellipse (sx + 11, y + 26, 11, SEL if on or done else (200, 190, 182))
		c.text_c (sx, y + 15, 22, 22, "✓" if done else str (k + 1), "smallb", WHITE)
		c.text_l (sx + 28, y + 15, 22, s, "uib" if on else "ui", TEXT if on or done else DIM)
		sx += 28 + c.tw (s, "uib") + 40
		if k < 2: c.hline (sx - 34, sx - 8, y + 26, (200, 190, 182), 2)
	return x, y, w, h

def shot_wizard ():
	c = screen ()
	cx, cy, cw, ch = app (c, "Mail")
	toolbar (c, cx, cy, cw)
	by = cy + TB_H + 1
	c.rect (cx, by, cw, ch - TB_H - 1, LIST)
	x, y, w, h = wizard_frame (c, "Add an account", 0)
	c.text (x + 24, y + 62, "Your email address", "uib")
	M.field (c, x + 24, y + 84, w - 48, 32, "steph.w@gmail.com", caret = True)
	c.text (x + 24, y + 126, "Gmail recognised. Or choose the kind of account:", "small", DIM)
	tiles = [("Gmail", "IMAP + SMTP, an app password", mark_gmail, True), ("Outlook.com / Hotmail", "Microsoft sign-in with a code", mark_outlook, False),
		 ("iCloud Mail", "IMAP + SMTP, an app password", mark_icloud, False), ("Yahoo Mail", "IMAP + SMTP, an app password", mark_yahoo, False),
		 ("Another IMAP account", "Your provider, your own server", mark_imap, False), ("A POP3 account", "Downloaded to the card, SMTP to send", mark_pop, False)]
	tw_, th_ = (w - 48 - 12) / 2, 70
	for k, (n, sub, mark, on) in enumerate (tiles):
		tx = x + 24 + (k % 2) * (tw_ + 12); ty = y + 150 + (k // 2) * (th_ + 12)
		c.rect (tx, ty, tw_, th_, (236, 245, 248) if on else WHITE, r = 10, outline = SEL if on else M.LINE2, width = 2 if on else 1)
		mark (c, tx + 16, ty + 18, 34)
		c.text (tx + 64, ty + 17, n, "uib"); c.text (tx + 64, ty + 38, sub, "small", DIM)
		if on: c.ellipse (tx + tw_ - 18, ty + 18, 9, SEL); c.text_c (tx + tw_ - 27, ty + 9, 18, 18, "✓", "smallb", WHITE)
	c.text (x + 24, y + h - 92, "Your name (on what you send)", "small", DIM)
	M.field (c, x + 24, y + h - 72, 260, 28, "Stéphane Wegener")
	M.button (c, x + w - 220, y + h - 48, 96, 32, "Cancel"); M.button (c, x + w - 114, y + h - 48, 96, 32, "Next", accent = True)
	c.save ("mail-wizard.png")

# ---- 5. Gmail: the app password ---------------------------------------------------------------------------------------------
def shot_gmail ():
	c = screen ()
	cx, cy, cw, ch = app (c, "Mail")
	toolbar (c, cx, cy, cw)
	c.rect (cx, cy + TB_H + 1, cw, ch - TB_H - 1, LIST)
	x, y, w, h = wizard_frame (c, "Add an account — Gmail", 1)
	mark_gmail (c, x + 24, y + 58, 32); c.text (x + 66, y + 58, "steph.w@gmail.com", "uib"); c.text (x + 66, y + 76, "Gmail asks for an app password: your usual one no longer works in mail apps.", "small", DIM)
	steps = [("1", "Two-step verification on", "Your Google account must have it (Security ▸ 2-Step Verification)."),
		 ("2", "Make an app password", "At myaccount.google.com/apppasswords: name it “Onyx Mail”, Create."),
		 ("3", "Type it here", "The 16 letters Google shows (the spaces do not matter).")]
	yy = y + 112
	for n, t, sub in steps:
		c.ellipse (x + 38, yy + 14, 13, SOFT); c.text_c (x + 25, yy + 1, 26, 26, n, "uib", SEL)
		c.text (x + 62, yy + 2, t, "uib"); c.text (x + 62, yy + 21, sub, "small", DIM)
		yy += 52
	M.button (c, x + 62, yy - 4, 268, 30, "Open the page in Jet Browser")
	yy += 44
	c.text (x + 24, yy, "App password", "uib")
	M.field (c, x + 24, yy + 22, 300, 32, "•••• •••• •••• ••••", caret = True)
	ic_lock (c, x + 336, yy + 30, 16, DIM); c.text (x + 358, yy + 30, "kept encrypted on the card", "small", DIM)
	yy += 70
	M.group (c, x + 24, yy, w - 48, 92, "THE SERVERS (FILLED IN)")
	c.text (x + 40, yy + 30, "Incoming   imap.gmail.com : 993   SSL/TLS", "mono", TEXT)
	c.text (x + 40, yy + 52, "Outgoing   smtp.gmail.com : 465   SSL/TLS", "mono", TEXT)
	c.text_r (x + w - 40, yy + 30, 20, "Change…", "small", M.LINK)
	M.button (c, x + w - 220, y + h - 48, 96, 32, "Back"); M.button (c, x + w - 114, y + h - 48, 96, 32, "Connect", accent = True)
	c.save ("mail-gmail.png")

# ---- 6. Outlook: signed in with a code, on a phone --------------------------------------------------------------------------
def qr (c, x, y, s, seed = 7):
	n = 25; cell = s / n; r = random.Random (seed)
	c.rect (x - 6, y - 6, s + 12, s + 12, WHITE, r = 6)
	for i in range (n):
		for j in range (n):
			fin = lambda a, b: (a < 7 and b < 7) or (a < 7 and b >= n - 7) or (a >= n - 7 and b < 7)
			if fin (i, j):
				ii, jj = i % (n - 7) if i >= 7 else i, j % (n - 7) if j >= 7 else j
				ii = i if i < 7 else i - (n - 7); jj = j if j < 7 else j - (n - 7)
				on = ii in (0, 6) or jj in (0, 6) or (2 <= ii <= 4 and 2 <= jj <= 4)
			else: on = r.random () < 0.48
			if on: c.rect (x + j * cell, y + i * cell, cell + 0.3, cell + 0.3, (20, 20, 24))

def shot_outlook ():
	c = screen ()
	cx, cy, cw, ch = app (c, "Mail")
	toolbar (c, cx, cy, cw)
	c.rect (cx, cy + TB_H + 1, cw, ch - TB_H - 1, LIST)
	x, y, w, h = wizard_frame (c, "Add an account — Outlook.com", 1)
	mark_outlook (c, x + 24, y + 58, 32); c.text (x + 66, y + 58, "s.wegener@outlook.com", "uib")
	c.text (x + 66, y + 76, "Microsoft signs you in on its own page — on your phone or your PC.", "small", DIM)
	# the code, the address, the QR code
	c.rect (x + 24, y + 108, w - 48, 236, WHITE, r = 12, outline = M.LINE2)
	c.text (x + 48, y + 128, "1.  Go to", "ui", DIM); c.text (x + 120, y + 126, "microsoft.com/link", "h2", M.LINK)
	c.text (x + 48, y + 164, "2.  Type this code", "ui", DIM)
	c.rect (x + 48, y + 188, 300, 64, (243, 247, 252), r = 10, outline = (190, 210, 235))
	c.text_c (x + 48, y + 188, 300, 64, "K7PQ-M2XZ", "code", (0, 70, 150))
	c.text (x + 48, y + 266, "3.  Sign in, allow \u201cOnyx Mail\u201d.", "ui", DIM)
	c.text (x + 48, y + 296, "Or scan this with your phone:", "small", DIM)
	qr (c, x + w - 24 - 24 - 150, y + 132, 150)
	# waiting
	yy = y + 364
	for k in range (12):
		a = k * math.pi / 6; al = 60 + k * 16
		c.line ([(x + 40 + math.cos (a) * 6, yy + 10 + math.sin (a) * 6), (x + 40 + math.cos (a) * 10, yy + 10 + math.sin (a) * 10)], M.A (SEL, min (255, al)), 2)
	c.text (x + 60, yy + 2, "Waiting for you to sign in…", "uib"); c.text (x + 60, yy + 20, "The code is good for 14:32 more. Nothing to type here: Mail goes on by itself.", "small", DIM)
	M.group (c, x + 24, yy + 50, w - 48, 58, "THEN")
	c.text (x + 40, yy + 78, "outlook.office365.com : 993 (IMAP)  ·  smtp-mail.outlook.com : 587 (SMTP)", "small", TEXT)
	M.button (c, x + w - 220, y + h - 48, 96, 32, "Back"); M.button (c, x + w - 114, y + h - 48, 96, 32, "Cancel")
	c.save ("mail-outlook.png")

# ---- 7. another account: IMAP or POP3 by hand -------------------------------------------------------------------------------
def shot_manual ():
	c = screen ()
	cx, cy, cw, ch = app (c, "Mail")
	toolbar (c, cx, cy, cw)
	c.rect (cx, cy + TB_H + 1, cw, ch - TB_H - 1, LIST)
	x, y, w, h = wizard_frame (c, "Add an account — by hand", 1)
	c.text (x + 24, y + 58, "steph@atelier-lumen.be", "uib"); c.text (x + 24, y + 77, "Your provider's settings (found for the known ones: Proximus, Telenet, Orange, GMX…).", "small", DIM)
	yy = y + 106
	M.group (c, x + 24, yy, w - 48, 190, "RECEIVING")
	M.segmented (c, x + 40, yy + 28, 28, ["IMAP", "POP3"], 1)
	def kv (yv, k, v, vw = 250):
		c.text_l (x + 40, yv, 28, k, "ui", DIM); M.field (c, x + 150, yv, vw, 28, v)
	kv (yy + 66, "Server", "pop.atelier-lumen.be"); c.text_l (x + 412, yy + 66, 28, "Port", "ui", DIM); M.field (c, x + 450, yy + 66, 70, 28, "995")
	c.text_l (x + 40, yy + 100, 28, "Security", "ui", DIM); M.dropdown (c, x + 150, yy + 100, 170, 28, "SSL/TLS")
	kv (yy + 134, "User name", "steph@atelier-lumen.be")
	yy += 200
	M.group (c, x + 24, yy, w - 48, 74, "POP3: THE MESSAGES")
	M.checkbox (c, x + 40, yy + 26, "Leave them on the server", on = True)
	c.text_l (x + 270, yy + 22, 24, "deleted there after", "ui", DIM); M.dropdown (c, x + 410, yy + 22, 110, 26, "14 days")
	c.text (x + 40, yy + 50, "Folders are the card's own (POP3 brings the inbox only).", "small", DIM)
	yy += 86
	M.group (c, x + 24, yy, w - 48, 74, "SENDING (SMTP)")
	c.text_l (x + 40, yy + 26, 28, "Server", "ui", DIM); M.field (c, x + 150, yy + 26, 230, 28, "smtp.atelier-lumen.be")
	c.text_l (x + 390, yy + 26, 28, "Port", "ui", DIM); M.field (c, x + 428, yy + 26, 56, 28, "587")
	c.text (x + 40, yy + 56, "STARTTLS  ·  the same user name and password", "small", DIM)
	M.button (c, x + w - 220, y + h - 48, 96, 32, "Back"); M.button (c, x + w - 114, y + h - 48, 96, 32, "Connect", accent = True)
	c.save ("mail-manual.png")

# ---- 8. the contacts: a Cardfile form -----------------------------------------------------------------------------------------
def shot_contacts ():
	c = screen ()
	cx, cy, cw, ch = app (c, "Mail — Contacts")
	toolbar (c, cx, cy, cw)
	by = cy + TB_H + 1; bh = ch - TB_H - 1
	sidebar (c, cx, by, bh, sel = "Contacts")
	# the people's list
	lx, lw = cx + SIDE_W + 1, 280
	c.rect (lx, by, lw, bh, LIST); c.vline (lx + lw, by, by + bh, M.shade (FACE, 0.86))
	c.text (lx + 14, by + 12, "Contacts", "h2"); c.text (lx + 18 + c.tw ("Contacts", "h2"), by + 16, "48", "small", DIM)
	c.text_r (lx + lw - 14, by + 10, 22, "+ New", "ui", M.LINK)
	c.rect (lx + 12, by + 42, lw - 24, 28, WHITE, r = 14, outline = M.LINE); ic_search (c, lx + 22, by + 48, 15, DIM); c.text_l (lx + 44, by + 42, 28, "Search the contacts", "ui", FAINT)
	people = [("B", ["Brasserie De Klok"]), ("C", ["Claire Lambert"]), ("J", ["Jonas Peeters"]), ("L", ["Lucas Martin"]), ("M", ["Marie Dubois"]), ("S", ["Sofia Rinaldi"])]
	yy = by + 82
	for letter, names in people:
		c.text (lx + 16, yy, letter, "smallb", SEL); yy += 18
		for n in names:
			on = n == "Jonas Peeters"
			if on: c.rect (lx + 6, yy, lw - 12, 40, SOFT, r = 6)
			avatar (c, lx + 14, yy + 5, 30, n)
			c.text_l (lx + 54, yy, 40, n, "uib" if on else "ui")
			yy += 42
	# the card: Cardfile's form
	rx = lx + lw + 1; rw = cw - (rx - cx)
	c.rect (rx, by, rw, bh, (236, 230, 225))
	kx, ky, kw, kh = rx + 26, by + 22, rw - 52, bh - 96
	c.rect (kx + 3, ky + 4, kw, kh, M.A ((0, 0, 0), 40), r = 6)
	c.rect (kx, ky, kw, kh, (255, 253, 245), r = 6, outline = (210, 200, 180))
	c.rect (kx, ky, kw, 46, (250, 232, 205), r = 6, corners = (True, True, False, False))
	for k in range (int ((kh - 50) / 24)): c.hline (kx + 12, kx + kw - 12, ky + 70 + k * 24, (226, 236, 246))
	avatar (c, kx + 14, ky + 8, 30, "Jonas Peeters")
	c.text_l (kx + 54, ky, 46, "Jonas Peeters", "h2")
	fields = [("E-mail", "jonas@deklok.be"), ("Second e-mail", "jonas.peeters@gmail.com"), ("Phone", "+32 9 123 45 67"), ("Mobile", "+32 478 12 34 56"),
		  ("Company", "Brasserie De Klok NV"), ("Address", "Kaai 7, 2000 Antwerpen"), ("Birthday", "14/03/1988"), ("Notes", "Labels: two colours, proofs twice.")]
	for k, (f, v) in enumerate (fields):
		fy = ky + 56 + k * 48
		c.text (kx + 18, fy, f, "small", DIM)
		c.text (kx + 18, fy + 16, v, "ui", M.LINK if "@" in v else TEXT)
	M.button (c, rx + 26, by + bh - 58, 150, 34, "", accent = True); ic_pen (c, rx + 40, by + bh - 50, 16, WHITE); c.text_l (rx + 64, by + bh - 58, 34, "Write", "uib", WHITE)
	M.button (c, rx + 186, by + bh - 58, 170, 34, "Open in Cardfile")
	c.text_r (rx + rw - 26, by + 6, 14, "SD:/Documents/Contacts.card  ·  48 cards", "small", DIM)
	c.save ("mail-contacts.png")

if __name__ == "__main__":
	shot_inbox (); shot_thread (); shot_compose (); shot_wizard (); shot_gmail (); shot_outlook (); shot_manual (); shot_contacts ()
