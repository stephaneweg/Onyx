#!/usr/bin/env python3
#
# demo_mail.py -- the made-up mailboxes Mail's screenshots show (fakemail.py --demo personal | work): the people and
# their messages of the mock-ups (docs/mail/mockups), dated from now; HTML newsletters, a bill, a long conversation,
# photos and an invoice attached. Nobody here is real.
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
import base64, calendar, email.utils, struct, time, zlib

def png(w, h, f):
    # a PNG of w x h from f(x, y) -> (r, g, b)
    raw = b"".join(b"\0" + bytes(c for x in range(w) for c in f(x, y)) for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")

def photo(seed):
    # a little landscape: a sky, hills, a river
    def f(x, y):
        h1 = 70 + int(12 * __import__("math").sin((x + seed * 40) / 23.0))
        if y < h1: return (120 + y // 2, 170 + y // 3, 230)
        if abs(y - 110 - (x // 9) % 7) < 6: return (90, 140, 200)
        return (60 + (x * 7 + seed * 13) % 40, 120 + (y * 3) % 50, 60)
    return png(200, 140, f)

def logo(r, g, b):
    return png(96, 32, lambda x, y: (255, 255, 255) if (12 < y < 20 and 10 < x < 86) else (r, g, b))

PDF = b"%PDF-1.4\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj 2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj 3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 595 842]>>endobj\ntrailer<</Root 1 0 R>>\n%%EOF\n"

class Box:
    def __init__(self, me):
        self.me = me; self.now = time.time(); self.msgs = []; self.n = 0
    TZ = 120			# (Onyx's zone in the stand-in card: sdcard/etc/system.ini)
    def at(self, days, hh, mm):
        # a moment `days` ago at hh:mm in Onyx's zone
        t = time.gmtime(self.now + self.TZ * 60 - days * 86400)
        return calendar.timegm((t.tm_year, t.tm_mon, t.tm_mday, hh, mm, 0, 0, 0, 0)) - self.TZ * 60
    def add(self, folder, frm, to, subject, when, text=None, html=None, files=(), inline=(), cc=None, irt=None, refs=None, mid=None, flags=()):
        self.n += 1
        mid = mid or "<demo-%d-%d@example.net>" % (self.n, int(when))
        h = "From: %s\r\nTo: %s\r\n" % (frm, to)
        if cc: h += "Cc: %s\r\n" % cc
        z = "%s%02d%02d" % ("+" if self.TZ >= 0 else "-", abs(self.TZ) // 60, abs(self.TZ) % 60)
        h += "Subject: %s\r\nDate: %s\r\nMessage-ID: %s\r\nMIME-Version: 1.0\r\n" % (subject, time.strftime("%a, %d %b %Y %H:%M:%S ", time.gmtime(when + self.TZ * 60)) + z, mid)
        if irt: h += "In-Reply-To: %s\r\nReferences: %s\r\n" % (irt, refs or irt)
        enc = lambda s: base64.encodebytes(s).decode().replace("\n", "\r\n")
        alt = None
        if html is not None:
            alt = ("Content-Type: multipart/alternative; boundary=\"=_alt\"\r\n\r\n--=_alt\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n%s\r\n"
                   "--=_alt\r\nContent-Type: text/html; charset=utf-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n%s\r\n--=_alt--\r\n") % (text or "", html)
            if inline:
                rel = "Content-Type: multipart/related; boundary=\"=_rel\"\r\n\r\n--=_rel\r\n" + alt
                for cid, data in inline: rel += "--=_rel\r\nContent-Type: image/png\r\nContent-ID: <%s>\r\nContent-Transfer-Encoding: base64\r\n\r\n%s" % (cid, enc(data))
                alt = rel + "--=_rel--\r\n"
        else:
            alt = "Content-Type: text/plain; charset=utf-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n%s\r\n" % text
        if files:
            body = "Content-Type: multipart/mixed; boundary=\"=_mix\"\r\n\r\n--=_mix\r\n" + alt
            for name, ctype, data in files:
                body += "--=_mix\r\nContent-Type: %s; name=\"%s\"\r\nContent-Disposition: attachment; filename=\"%s\"\r\nContent-Transfer-Encoding: base64\r\n\r\n%s" % (ctype, name, name, enc(data))
            body += "--=_mix--\r\n"
        else: body = alt
        self.msgs.append((folder, (h + body).encode("utf-8"), when, set(flags)))
        return mid

NEWS_CSS = ("<style>body{margin:0;background:#eef1f5} .card{background:#fff;border-radius:8px} h1{font-family:Georgia,serif;color:#1b2a41;font-size:24px;margin:0 0 10px}"
            " p{font-family:Arial,sans-serif;font-size:14px;line-height:21px;color:#3c4043;margin:0 0 12px} .btn{background:#1a73e8;color:#fff;padding:9px 20px;text-decoration:none;font-weight:bold;font-family:Arial}"
            " .small{font-size:11px;color:#80868b}</style>")

def personal():
    b = Box('"Stéphane" <me@example.com>'); me = b.me
    # Marie: Saturday's lunch (3)
    m1 = b.add("INBOX", '"Marie Dubois" <marie.dubois@example.org>', me, "Saturday's lunch", b.at(1, 19, 2), "Hello Stéphane,\n\nWould Saturday suit you for lunch at ours? Around 12:30. Bring the kids!\n\nMarie", flags=("\\Seen",))
    m2 = b.add("[Gmail]/Sent Mail", me, '"Marie Dubois" <marie.dubois@example.org>', "Re: Saturday's lunch", b.at(1, 20, 15), "Lovely! We'll come. Shall I bring a dessert?\n\nOn Friday, Marie Dubois wrote:\n> Would Saturday suit you for lunch at ours?", irt=m1, flags=("\\Seen",))
    b.add("INBOX", '"Marie Dubois" <marie.dubois@example.org>', me, "Re: Saturday's lunch", b.at(0, 11, 42),
          "Perfect, see you at 12:30 then — I'm making a tarte tatin, so no dessert needed!\n\nThe address: 14 avenue des Tilleuls, Uccle. Park in the street.\n\nMarie", irt=m2, refs=m1 + " " + m2, flags=("\\Flagged",))
    # Onyx Packages: an HTML newsletter
    html = (NEWS_CSS + "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" bgcolor=\"#eef1f5\"><tr><td align=\"center\" style=\"padding:20px 0\">"
            "<table width=\"480\" class=\"card\" cellpadding=\"0\" cellspacing=\"0\"><tr><td style=\"padding:18px 24px;border-bottom:1px solid #e3e6ea\"><img src=\"cid:onyx\" width=\"96\" height=\"32\" alt=\"Onyx\"></td></tr>"
            "<tr><td style=\"padding:22px 24px\"><h1>3 updates for your Onyx</h1><p>New versions are ready in the Package Manager:</p>"
            "<table width=\"100%\" cellpadding=\"6\" style=\"border-collapse:collapse;font-family:Arial;font-size:13px\">"
            "<tr style=\"background:#f1f3f4\"><th align=\"left\">Package</th><th align=\"right\">Version</th></tr>"
            "<tr><td style=\"border-bottom:1px solid #eee\">PDF Viewer</td><td align=\"right\" style=\"border-bottom:1px solid #eee\">1.0.1</td></tr>"
            "<tr><td style=\"border-bottom:1px solid #eee\">Media Player</td><td align=\"right\" style=\"border-bottom:1px solid #eee\">1.0.1</td></tr>"
            "<tr><td>Jet Browser</td><td align=\"right\">1.0.6</td></tr></table>"
            "<p style=\"text-align:center;margin:22px 0 6px\"><a class=\"btn\" href=\"https://onyx.example/packages\">See what is new</a></p></td></tr></table>"
            "<p class=\"small\" style=\"margin-top:14px\">You get this because updates are on. <a href=\"https://onyx.example/unsubscribe\" style=\"color:#80868b\">Turn them off</a></p>"
            "<img src=\"https://tracker.example/o.gif\" width=\"1\" height=\"1\"></td></tr></table>")
    b.add("INBOX", '"Onyx Packages" <packages@onyx.example>', me, "3 updates for your Onyx", b.at(0, 8, 30),
          "PDF Viewer 1.0.1, Media Player 1.0.1 and Jet Browser 1.0.6 are ready.", html=html, inline=[("onyx", logo(40, 44, 52))])
    # Jonas: the brewery's quote (5)
    j1 = b.add("INBOX", '"Jonas Peeters" <jonas@brouwerij-peeters.example>', me, "The quote for the brewery", b.at(3, 9, 12), "Hi Stéphane,\n\nCould you send us a quote for 3 label designs (Blond, Tripel, Winter)?\n\nThanks,\nJonas", flags=("\\Seen",))
    j2 = b.add("[Gmail]/Sent Mail", me, '"Jonas Peeters" <jonas@brouwerij-peeters.example>', "Re: The quote for the brewery", b.at(3, 14, 40), "Hello Jonas,\n\nHere it is: 2,150 € for the three, two rounds of proofs included.\n\nStéphane", irt=j1, flags=("\\Seen",))
    j3 = b.add("INBOX", '"Jonas Peeters" <jonas@brouwerij-peeters.example>', me, "Re: The quote for the brewery", b.at(2, 10, 5), "Looks good. Can the Winter one be ready by November?", irt=j2, refs=j1 + " " + j2, flags=("\\Seen",))
    j4 = b.add("[Gmail]/Sent Mail", me, '"Jonas Peeters" <jonas@brouwerij-peeters.example>', "Re: The quote for the brewery", b.at(2, 16, 20), "Yes, mid-November at the latest.", irt=j3, refs=j1 + " " + j2 + " " + j3, flags=("\\Seen",))
    b.add("INBOX", '"Jonas Peeters" <jonas@brouwerij-peeters.example>', me, "Re: The quote for the brewery", b.at(1, 17, 48),
          "That works for us. Could you add the bottle-neck labels too? Same style, smaller.\n\nCheers,\nJonas\n\n> Yes, mid-November at the latest.", irt=j4, refs=" ".join([j1, j2, j3, j4]))
    # Sofia: photos (2 attachments)
    b.add("INBOX", '"Sofia Rinaldi" <sofia.rinaldi@example.it>', me, "Photos from Ghent", b.at(1, 14, 12),
          "Here they are! The one at the Graslei is my favourite.\n\nCiao,\nSofia", files=[("graslei.png", "image/png", photo(1)), ("gravensteen.png", "image/png", photo(2))], flags=("\\Seen", "\\Flagged"))
    # Proximus: a bill
    html = (NEWS_CSS + "<div style=\"padding:18px\"><table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" class=\"card\"><tr><td style=\"background:#5c2d91;padding:16px 22px\">"
            "<font color=\"#ffffff\" size=\"5\" face=\"Arial\"><b>Your bill</b></font></td></tr><tr><td style=\"padding:20px 22px\">"
            "<p>Dear customer,</p><p>Your September bill is available in MyProximus.</p>"
            "<table cellpadding=\"4\" style=\"font-family:Arial;font-size:14px\"><tr><td>Amount</td><td><b>59,90 &euro;</b></td></tr><tr><td>Due</td><td>15 October</td></tr></table>"
            "<p style=\"margin-top:16px\"><a href=\"https://proximus.example/bill\" style=\"color:#5c2d91;font-weight:bold\">See my bill &rarr;</a></p></td></tr></table></div>")
    b.add("INBOX", '"Proximus" <noreply@proximus.example>', me, "Your September bill", b.at(1, 9, 20), "Your bill of 59,90 € is available in MyProximus.", html=html, flags=("\\Seen",))
    # Anna, Pi Shop, a club
    b.add("INBOX", '"Anna Lefèvre" <anna@example.org>', me, "Weekend in the Ardennes", b.at(4, 18, 30), "Are we still on for the weekend of the 18th? I booked the cabin near La Roche.\n\nAnna", flags=("\\Seen",))
    html = (NEWS_CSS + "<table width=\"100%\" bgcolor=\"#eef1f5\"><tr><td align=\"center\" style=\"padding:16px\"><table width=\"460\" class=\"card\"><tr><td style=\"padding:20px\">"
            "<h1>Your order has shipped</h1><p>The <b>Raspberry Pi 4</b> you ordered is on its way and should reach you by Thursday.</p>"
            "<p style=\"text-align:center\"><a class=\"btn\" href=\"https://shop.example/track\">Track my parcel</a></p></td></tr></table></td></tr></table>")
    b.add("INBOX", '"Pi Shop" <orders@shop.example>', me, "Your order has shipped", b.at(5, 10, 2), "Your Raspberry Pi 4 is on its way.", html=html, flags=("\\Seen",))
    b.add("INBOX", '"Tennis Club Uccle" <info@tcuccle.example>', me, "Saturday's tournament: the draw", b.at(6, 20, 45), "The draw for Saturday is out: your first match is at 10:00 on court 3.", flags=("\\Seen",))
    b.add("[Gmail]/Drafts", me, '"Anna Lefèvre" <anna@example.org>', "Re: Weekend in the Ardennes", b.at(0, 7, 55), "Yes! We'll drive down on Friday evening.", flags=("\\Seen", "\\Draft"))
    return b

def work():
    b = Box('"Stéphane" <steph@atelier-lumen.example>'); me = b.me
    text = ("Hello Stéphane,\n\nPlease find attached the invoice for the beer labels — the design and the two rounds of proofs — as agreed in the quote of 12 September.\n\n"
            "Amount: 2.528,90 € (VAT included), within 30 days.\nStructured communication: +++412/2026/04123+++\n\nThank you for your trust,\n\nMarie Dubois\nAtelier Lumen SRL — Rue de la Loi 12, 1000 Brussels")
    b.add("INBOX", '"Atelier Lumen" <factures@atelier-lumen.example>', me, "Invoice 2026-0412", b.at(0, 10, 5), text,
          files=[("Invoice-2026-0412.pdf", "application/pdf", PDF * 40), ("labels-final.png", "image/png", photo(3))])
    b.add("INBOX", '"Brouwerij Peeters" <info@brouwerij-peeters.example>', me, "Labels approved!", b.at(2, 15, 33), "The whole team loves them. We go to print on Monday.\n\nJonas")
    b.add("INBOX", '"Printshop Mechelen" <hello@printshop.example>', me, "Proof ready for your check", b.at(3, 11, 0), "Your proof is ready: please check the colours before Friday.", flags=("\\Seen",))
    return b
