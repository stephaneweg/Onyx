#!/usr/bin/env python3
#
# fakemail.py -- a small mail server for Mail's tests and screenshots (docs/mail/README.md): IMAP4rev1 (LOGIN,
# AUTHENTICATE PLAIN / XOAUTH2 with SASL-IR, LIST with SPECIAL-USE, SELECT / EXAMINE, UID FETCH -- ENVELOPE,
# BODYSTRUCTURE, sections and partials --, UID STORE, UID MOVE / COPY, UID EXPUNGE, APPEND, IDLE), POP3 (CAPA, USER /
# PASS, AUTH PLAIN, STAT, LIST, UIDL, TOP, RETR, DELE), SMTP submission (EHLO, AUTH PLAIN / LOGIN / XOAUTH2, MAIL, RCPT,
# DATA -- what is sent lands in the INBOX: IDLE sees it) and Microsoft's device code endpoints (HTTP). One account
# (user "me@onyx.test", password "secret"; OAuth: the token "tok-1" after the second poll). Plain TCP (and IMAP over a
# self-signed TLS with --cert). Its messages: samples (sample_messages ()) or a folder of .eml (--eml DIR).
#
#   python3 tools/tests/mail/fakemail.py [--imap 10143] [--pop 10110] [--smtp 10587] [--http 10080] [--eml DIR]
#
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
#
import argparse, base64, email, email.utils, json, os, re, select, socket, socketserver, ssl, sys, threading, time
from email import policy

USER, PASSWORD = "me@onyx.test", "secret"
TOKENS = {"tok-1", "tok-2", "tok-L" + "x" * 2500}	# (tok-L: as long as Microsoft's tokens)

# ---- the store -------------------------------------------------------------------------------------------------------------
class Msg:
    def __init__(self, raw, uid, flags=(), date=None):
        self.raw = raw.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
        self.uid = uid; self.flags = set(flags); self.date = date or time.time()
        self.m = email.message_from_bytes(self.raw, policy=policy.compat32)

class Folder:
    def __init__(self, name, special=None):
        self.name, self.special, self.msgs, self.uidnext, self.uidvalidity = name, special, [], 1, 1700000000 + len(name)
    def add(self, raw, flags=(), date=None):
        m = Msg(raw, self.uidnext, flags, date); self.uidnext += 1; self.msgs.append(m); return m

class Store:
    def __init__(self):
        self.lock = threading.Condition(); self.version = 0
        self.folders = {}
        for n, sp in (("INBOX", None), ("[Gmail]", "\\Noselect"), ("[Gmail]/Sent Mail", "\\Sent"), ("[Gmail]/Drafts", "\\Drafts"),
                      ("[Gmail]/Trash", "\\Trash"), ("[Gmail]/Spam", "\\Junk"), ("[Gmail]/All Mail", "\\All"), ("Projets &AOk-t&AOk-", None)):
            self.folders[n] = Folder(n, sp)
    def changed(self):
        with self.lock: self.version += 1; self.lock.notify_all()

STORE = Store()

def sample_messages():
    now = time.time()
    def mk(frm, to, subj, body, ago, extra="", ctype='text/plain; charset="utf-8"', cte="8bit", mid=None, irt=None):
        mid = mid or "<%x.%d@onyx.test>" % (abs(hash(subj)) & 0xffffffff, ago)
        h = "From: %s\r\nTo: %s\r\nSubject: %s\r\nDate: %s\r\nMessage-ID: %s\r\n" % (frm, to, subj, email.utils.formatdate(now - ago, localtime=False), mid)
        if irt: h += "In-Reply-To: %s\r\nReferences: %s\r\n" % (irt, irt)
        h += "MIME-Version: 1.0\r\n" + extra
        if ctype: h += "Content-Type: %s\r\nContent-Transfer-Encoding: %s\r\n" % (ctype, cte)
        return (h + "\r\n" + body).encode("utf-8"), now - ago
    out = []
    out.append(mk('"Anna Lefèvre" <anna@example.org>', USER, "Weekend in the Ardennes", "Hi!\r\n\r\nAre we still on for Saturday? I booked the cabin.\r\n\r\nAnna\r\n", 600, mid="<ardennes-1@example.org>"))
    out.append(mk("=?UTF-8?Q?Bj=C3=B6rn_M=C3=BCller?= <bjorn@example.de>", USER, "=?UTF-8?B?UmVwb3J0IOKAkyBRMyByw6lzdWx0YXRz?=",
                  "Hall=C3=B6, the report is attached as soon as it is ready.=\r\n Cheers\r\n", 3600, cte="quoted-printable"))
    b = "=_b1"
    html = ('<html><head><style>p{color:#333} .big{font-size:18px;font-weight:bold}</style></head><body>'
            '<p class="big">Your order has shipped</p><table border="1" cellpadding="4"><tr><th>Item</th><th>Qty</th></tr>'
            '<tr><td>Raspberry Pi 4</td><td>1</td></tr></table><p>Track it <a href="https://example.com/t/1">here</a>.</p>'
            '<img src="cid:logo@shop"></body></html>')
    png = base64.b64encode(bytes.fromhex("89504e470d0a1a0a0000000d4948445200000001000000010806000000"
                                         "1f15c4890000000d49444154789c6360f8cfc0f01f0005000201a5f6"
                                         "ad4d0000000049454e44ae426082")).decode()
    body = ("--%s\r\nContent-Type: multipart/alternative; boundary=\"=_b2\"\r\n\r\n--=_b2\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n"
            "Your order has shipped. Track it: https://example.com/t/1\r\n--=_b2\r\nContent-Type: text/html; charset=utf-8\r\n\r\n%s\r\n--=_b2--\r\n"
            "--%s\r\nContent-Type: image/png\r\nContent-ID: <logo@shop>\r\nContent-Transfer-Encoding: base64\r\n\r\n%s\r\n--%s--\r\n") % (b, html, b, png, b)
    out.append(mk('"Pi Shop" <orders@shop.example>', USER, "Your order has shipped", body, 86400, ctype='multipart/related; boundary="%s"' % b, cte="7bit"))
    pdf = base64.b64encode(b"%PDF-1.4\n%fake\n").decode()
    body = ("--=_m\r\nContent-Type: text/plain; charset=iso-8859-1\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\nVoici la facture de septembre. Merci =E0 vous.\r\n"
            "--=_m\r\nContent-Type: application/pdf; name=\"facture.pdf\"\r\nContent-Disposition: attachment; filename*=utf-8''facture%%20sept.pdf\r\n"
            "Content-Transfer-Encoding: base64\r\n\r\n%s\r\n--=_m--\r\n") % pdf
    out.append(mk("Comptabilité <compta@example.be>", USER, "Facture septembre", body, 2 * 86400, ctype='multipart/mixed; boundary="=_m"', cte="7bit"))
    out.append(mk('"Anna Lefèvre" <anna@example.org>', USER, "Re: Weekend in the Ardennes", "> Are we still on?\r\nYes, see you there.\r\n", 300,
                  irt="<ardennes-1@example.org>"))
    return out

def load_store(eml_dir):
    inbox = STORE.folders["INBOX"]
    if eml_dir:
        for f in sorted(os.listdir(eml_dir)):
            if f.endswith(".eml"):
                with open(os.path.join(eml_dir, f), "rb") as fh: inbox.add(fh.read(), date=os.path.getmtime(os.path.join(eml_dir, f)))
    else:
        for i, (raw, d) in enumerate(sorted(sample_messages(), key=lambda x: x[1])):
            inbox.add(raw, flags=("\\Seen",) if i < 2 else (), date=d)
        STORE.folders["[Gmail]/Sent Mail"].add(b"From: me@onyx.test\r\nTo: anna@example.org\r\nSubject: Re: Weekend\r\nDate: Mon, 1 Sep 2026 10:00:00 +0200\r\n\r\nSee you!\r\n", ("\\Seen",))

# ---- IMAP --------------------------------------------------------------------------------------------------------------------
def q(s):
    if s is None: return "NIL"
    if isinstance(s, bytes): s = s.decode("utf-8", "replace")
    s = s.encode("utf-8", "surrogateescape").decode("utf-8", "replace")   # (the raw header's 8-bit bytes: UTF-8)
    if any(c in s for c in '\r\n') or len(s) > 200 or any(ord(c) > 126 for c in s):
        b = s.encode("utf-8"); return "{%d}\r\n" % len(b) + b.decode("utf-8")
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

def addrs(v):
    if not v: return "NIL"
    out = []
    for name, a in email.utils.getaddresses([v]):
        if "@" not in a: continue
        mb, host = a.split("@", 1)
        out.append("(%s NIL %s %s)" % (q(name) if name else "NIL", q(mb), q(host)))
    return "(" + "".join(out) + ")" if out else "NIL"

def raw_header(raw, name):
    # a header from the message's bytes (8-bit UTF-8 kept: what a real server sends)
    head = raw.split(b"\r\n\r\n", 1)[0].decode("utf-8", "replace").replace("\r\n ", " ").replace("\r\n\t", " ")
    for l in head.split("\r\n"):
        k, _, v = l.partition(":")
        if k.strip().lower() == name.lower(): return v.strip()
    return None

def envelope(msg):
    g = lambda k: raw_header(msg.raw, k)
    frm = g("From")
    return "(%s %s %s %s %s %s %s %s %s %s)" % (q(g("Date")), q(g("Subject")), addrs(frm), addrs(g("Sender") or frm), addrs(g("Reply-To") or frm),
                                               addrs(g("To")), addrs(g("Cc")), addrs(g("Bcc")), q(g("In-Reply-To")), q(g("Message-ID")))

def part_bytes(p):
    pl = p.get_payload(decode=False)
    if isinstance(pl, list): return b""
    return pl.encode("utf-8", "surrogateescape") if isinstance(pl, str) else pl

def params(p):
    ps = p.get_params() or []
    out = [(k, v) for k, v in ps[1:]]
    if not out: return "NIL"
    return "(" + " ".join("%s %s" % (q(k), q(email.utils.collapse_rfc2231_value(v) if isinstance(v, tuple) else v)) for k, v in out) + ")"

def disposition(p):
    d = p.get("Content-Disposition")
    if not d: return "NIL"
    kind = d.split(";")[0].strip()
    fn = p.get_param("filename", header="Content-Disposition")
    if fn is not None:
        fn = email.utils.collapse_rfc2231_value(fn)
        return "(%s (\"filename\" %s))" % (q(kind), q(fn))
    return "(%s NIL)" % q(kind)

def bodystructure(p):
    if p.is_multipart():
        kids = "".join(bodystructure(k) for k in p.get_payload())
        return "(%s %s %s NIL NIL NIL)" % (kids, q(p.get_content_subtype()), params(p))
    b = part_bytes(p)
    s = "(%s %s %s %s NIL %s %d" % (q(p.get_content_maintype()), q(p.get_content_subtype()), params(p),
                                   q(p.get("Content-ID")) if p.get("Content-ID") else "NIL", q(p.get("Content-Transfer-Encoding", "7bit")), len(b))
    if p.get_content_maintype() == "text": s += " %d" % b.count(b"\n")
    return s + " NIL %s NIL NIL)" % disposition(p)

def section(msg, sec):
    m = msg.m
    if sec == "": return msg.raw
    mh = re.match(r"HEADER\.FIELDS \(([^)]*)\)", sec, re.I)
    if mh:
        want = [w.lower() for w in mh.group(1).split()]
        hdr = msg.raw.split(b"\r\n\r\n", 1)[0].decode("utf-8", "replace")
        lines, keep = [], False
        for l in hdr.split("\r\n"):
            if l[:1] in (" ", "\t"):
                if keep: lines.append(l)
                continue
            keep = l.split(":", 1)[0].lower() in want
            if keep: lines.append(l)
        return ("\r\n".join(lines) + "\r\n\r\n").encode() if lines else b"\r\n"
    if sec.upper() == "HEADER": return msg.raw.split(b"\r\n\r\n", 1)[0] + b"\r\n\r\n"
    if sec.upper() == "TEXT": return msg.raw.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in msg.raw else b""
    p = m
    for n in sec.split("."):
        if not n.isdigit(): break
        if p.is_multipart(): p = p.get_payload()[int(n) - 1]
        elif int(n) != 1: return b""
    return part_bytes(p).replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")

def uidset(spec, msgs):
    top = max([m.uid for m in msgs], default=0)
    sel = set()
    for r in spec.split(","):
        if ":" in r:
            a, b = r.split(":")
            a = top if a == "*" else int(a); b = top if b == "*" else int(b)
            if a > b: a, b = b, a
            sel.update(range(a, b + 1))
        else: sel.add(top if r == "*" else int(r))
    return [m for m in msgs if m.uid in sel]

def mutf7(name):
    return name   # (the store keeps the names as IMAP sends them)

class Imap(socketserver.StreamRequestHandler):
    def out(self, s):
        if isinstance(s, str): s = s.encode("utf-8")
        self.wfile.write(s); self.wfile.flush()
    def readline(self):
        l = self.rfile.readline()
        if not l: raise EOFError
        return l.decode("utf-8", "replace").rstrip("\r\n")
    def caps(self):
        return "IMAP4rev1 LITERAL+ SASL-IR SPECIAL-USE UIDPLUS MOVE IDLE X-GM-EXT-1 AUTH=PLAIN AUTH=XOAUTH2"
    def handle(self):
        self.auth = False; self.sel = None
        try:
            self.out("* OK [CAPABILITY %s] fakemail ready\r\n" % self.caps())
            while True:
                line = self.readline()
                m = re.match(r"(\S+) (\S+)\s*(.*)", line)
                if not m: self.out("* BAD what?\r\n"); continue
                tag, cmd, rest = m.group(1), m.group(2).upper(), m.group(3)
                # a literal at the end: read it (synchronising or not)
                lit = re.search(r"\{(\d+)(\+?)\}$", rest)
                data = None
                if lit:
                    if not lit.group(2): self.out("+ go ahead\r\n")
                    data = self.rfile.read(int(lit.group(1))); self.rfile.readline()
                    rest = rest[:lit.start()].strip()
                getattr(self, "c_" + cmd.lower(), self.c_bad)(tag, rest, data)
                if cmd == "LOGOUT": return
        except (EOFError, ConnectionError, OSError):
            return
    def c_bad(self, tag, rest, data): self.out("%s BAD unknown command\r\n" % tag)
    def c_capability(self, tag, rest, data): self.out("* CAPABILITY %s\r\n%s OK done\r\n" % (self.caps(), tag))
    def c_noop(self, tag, rest, data): self.out("%s OK done\r\n" % tag)
    def c_logout(self, tag, rest, data): self.out("* BYE\r\n%s OK bye\r\n" % tag)
    def c_login(self, tag, rest, data):
        args = re.findall(r'"((?:[^"\\]|\\.)*)"|(\S+)', rest)
        args = [a or b for a, b in args]; args = [a.replace('\\"', '"').replace("\\\\", "\\") for a in args]
        if len(args) == 2 and args[0] == USER and args[1] == PASSWORD: self.auth = True; self.out("%s OK [CAPABILITY %s] logged in\r\n" % (tag, self.caps()))
        else: self.out("%s NO [AUTHENTICATIONFAILED] Invalid credentials\r\n" % tag)
    def c_authenticate(self, tag, rest, data):
        parts = rest.split()
        mech = parts[0].upper(); ir = parts[1] if len(parts) > 1 else None
        if ir is None: self.out("+ \r\n"); ir = self.readline()
        try: dec = base64.b64decode(ir)
        except Exception: dec = b""
        ok = False
        if mech == "PLAIN":
            f = dec.split(b"\0"); ok = len(f) == 3 and f[1].decode() == USER and f[2].decode() == PASSWORD
        elif mech == "XOAUTH2":
            m = re.match(rb"user=([^\x01]*)\x01auth=Bearer ([^\x01]*)\x01\x01", dec)
            ok = bool(m) and m.group(1).decode() == USER and m.group(2).decode() in TOKENS
            if not ok:
                self.out("+ %s\r\n" % base64.b64encode(b'{"status":"401","schemes":"bearer","scope":"https://mail.google.com/"}').decode()); self.readline()
        if ok: self.auth = True; self.out("%s OK [CAPABILITY %s] authenticated\r\n" % (tag, self.caps()))
        else: self.out("%s NO [AUTHENTICATIONFAILED] Invalid credentials (Failure)\r\n" % tag)
    def need_auth(self, tag):
        if not self.auth: self.out("%s NO not authenticated\r\n" % tag); return False
        return True
    def c_list(self, tag, rest, data):
        if not self.need_auth(tag): return
        for f in STORE.folders.values():
            fl = [f.special] if f.special else []
            if not any(g.name.startswith(f.name + "/") for g in STORE.folders.values()): fl.append("\\HasNoChildren")
            else: fl.append("\\HasChildren")
            self.out('* LIST (%s) "/" %s\r\n' % (" ".join(fl), q(f.name)))
        self.out("%s OK LIST done\r\n" % tag)
    def folder_arg(self, s):
        s = s.strip()
        if s.startswith('"'): return re.match(r'"((?:[^"\\]|\\.)*)"', s).group(1).replace('\\"', '"').replace("\\\\", "\\")
        return s.split()[0]
    def c_select(self, tag, rest, data, ro=False):
        if not self.need_auth(tag): return
        n = self.folder_arg(rest)
        if n.upper() == "INBOX": n = "INBOX"
        f = STORE.folders.get(n)
        if not f or f.special == "\\Noselect": self.out("%s NO [NONEXISTENT] no such folder\r\n" % tag); return
        self.sel = f; self.seen_n = len(f.msgs)
        self.out("* FLAGS (\\Answered \\Flagged \\Draft \\Deleted \\Seen $Forwarded)\r\n* %d EXISTS\r\n* 0 RECENT\r\n"
                 "* OK [UIDVALIDITY %d] UIDs valid\r\n* OK [UIDNEXT %d] Predicted next UID\r\n%s OK [%s] %s completed\r\n"
                 % (len(f.msgs), f.uidvalidity, f.uidnext, tag, "READ-ONLY" if ro else "READ-WRITE", "EXAMINE" if ro else "SELECT"))
    def c_examine(self, tag, rest, data): self.c_select(tag, rest, data, True)
    def c_uid(self, tag, rest, data):
        if not self.need_auth(tag): return
        if not self.sel: self.out("%s BAD no folder selected\r\n" % tag); return
        sub, _, args = rest.partition(" ")
        getattr(self, "u_" + sub.lower(), lambda t, a: self.out("%s BAD\r\n" % t))(tag, args)
    def seq(self, m): return self.sel.msgs.index(m) + 1
    def u_fetch(self, tag, args):
        spec, _, items = args.partition(" ")
        items = items.strip()
        if items.startswith("(") and items.endswith(")"): items = items[1:-1]
        toks = re.findall(r"BODY(?:\.PEEK)?\[[^\]]*\](?:<\d+\.\d+>)?|\S+", items, re.I)
        for m in uidset(spec, self.sel.msgs):
            out = ["UID %d" % m.uid]
            for t in toks:
                T = t.upper()
                if T == "UID": continue
                if T == "FLAGS": out.append("FLAGS (%s)" % " ".join(sorted(m.flags)))
                elif T == "INTERNALDATE": out.append('INTERNALDATE "%s"' % time.strftime("%d-%b-%Y %H:%M:%S +0000", time.gmtime(m.date)))
                elif T == "RFC822.SIZE": out.append("RFC822.SIZE %d" % len(m.raw))
                elif T == "ENVELOPE": out.append("ENVELOPE " + envelope(m))
                elif T == "BODYSTRUCTURE": out.append("BODYSTRUCTURE " + bodystructure(m.m))
                elif T == "X-GM-THRID": out.append("X-GM-THRID %d" % (1000 + (abs(hash(m.m.get("Subject", "").replace("Re: ", ""))) % 100000)))
                elif T.startswith("BODY"):
                    mm = re.match(r"BODY(?:\.PEEK)?\[([^\]]*)\](?:<(\d+)\.(\d+)>)?", t, re.I)
                    sec = mm.group(1); b = section(m, sec)
                    name = "BODY[%s]" % sec
                    if mm.group(2): o, n = int(mm.group(2)), int(mm.group(3)); b = b[o:o + n]; name += "<%d>" % o
                    out.append("%s {%d}\r\n" % (name, len(b)) + b.decode("utf-8", "surrogateescape"))
                    if "PEEK" not in T and sec != "HEADER.FIELDS": m.flags.add("\\Seen")
            self.out(("* %d FETCH (%s)\r\n" % (self.seq(m), " ".join(out))).encode("utf-8", "surrogateescape"))
        self.out("%s OK UID FETCH done\r\n" % tag)
    def c_fetch(self, tag, rest, data):
        # by sequence numbers: turned into the uids, then as UID FETCH
        if not self.need_auth(tag): return
        if not self.sel: self.out("%s BAD no folder selected\r\n" % tag); return
        spec, _, items = rest.partition(" ")
        n = len(self.sel.msgs); seqs = set()
        for r in spec.split(","):
            if ":" in r:
                a, b = r.split(":"); a = n if a == "*" else int(a); b = n if b == "*" else int(b)
                seqs.update(range(min(a, b), max(a, b) + 1))
            else: seqs.add(n if r == "*" else int(r))
        uids = ",".join(str(self.sel.msgs[i - 1].uid) for i in sorted(seqs) if 1 <= i <= n)
        if not uids: self.out("%s OK FETCH done\r\n" % tag); return
        self.u_fetch(tag, uids + " " + items)
    def u_store(self, tag, args):
        spec, op, fl = args.split(" ", 2)
        flags = set(re.findall(r"[\\$]?\w+", fl))
        for m in uidset(spec, self.sel.msgs):
            if op.upper().startswith("+"): m.flags |= flags
            elif op.upper().startswith("-"): m.flags -= flags
            else: m.flags = set(flags)
            if "SILENT" not in op.upper(): self.out("* %d FETCH (UID %d FLAGS (%s))\r\n" % (self.seq(m), m.uid, " ".join(sorted(m.flags))))
        STORE.changed(); self.out("%s OK STORE done\r\n" % tag)
    def u_copy(self, tag, args, move=False):
        spec, _, dest = args.partition(" ")
        d = STORE.folders.get(self.folder_arg(dest))
        if not d: self.out("%s NO [TRYCREATE] no such folder\r\n" % tag); return
        ms = uidset(spec, self.sel.msgs); src, dst = [], []
        for m in ms:
            n = d.add(m.raw, m.flags, m.date); src.append(m.uid); dst.append(n.uid)
        if move:
            for m in ms:
                self.out("* %d EXPUNGE\r\n" % self.seq(m)); self.sel.msgs.remove(m)
        STORE.changed()
        cu = "[COPYUID %d %s %s] " % (d.uidvalidity, ",".join(map(str, src)), ",".join(map(str, dst))) if ms else ""
        self.out("%s OK %s%s done\r\n" % (tag, cu, "MOVE" if move else "COPY"))
    def u_move(self, tag, args): self.u_copy(tag, args, True)
    def u_expunge(self, tag, args):
        for m in uidset(args, self.sel.msgs):
            if "\\Deleted" in m.flags: self.out("* %d EXPUNGE\r\n" % self.seq(m)); self.sel.msgs.remove(m)
        STORE.changed(); self.out("%s OK EXPUNGE done\r\n" % tag)
    def c_expunge(self, tag, rest, data):
        for m in [m for m in self.sel.msgs if "\\Deleted" in m.flags]: self.out("* %d EXPUNGE\r\n" % self.seq(m)); self.sel.msgs.remove(m)
        self.out("%s OK EXPUNGE done\r\n" % tag)
    def c_append(self, tag, rest, data):
        if not self.need_auth(tag): return
        name = self.folder_arg(rest)
        f = STORE.folders.get(name)
        if not f: self.out("%s NO [TRYCREATE] no such folder\r\n" % tag); return
        fl = re.search(r"\(([^)]*)\)", rest[len(name):] if not rest.startswith('"') else rest[rest.index('"', 1) + 1:])
        m = f.add(data or b"", fl.group(1).split() if fl else ())
        STORE.changed(); self.out("%s OK [APPENDUID %d %d] APPEND done\r\n" % (tag, f.uidvalidity, m.uid))
    def c_idle(self, tag, rest, data):
        self.out("+ idling\r\n")
        v0 = STORE.version; n0 = len(self.sel.msgs) if self.sel else 0
        while True:
            r, _, _ = select.select([self.connection], [], [], 0.1)
            if r:
                l = self.readline()
                if l.upper() == "DONE": break
            if self.sel and len(self.sel.msgs) != n0:
                n0 = len(self.sel.msgs); self.out("* %d EXISTS\r\n" % n0)
        self.out("%s OK IDLE terminated\r\n" % tag)

# ---- POP3 --------------------------------------------------------------------------------------------------------------------
class Pop(socketserver.StreamRequestHandler):
    def out(self, s): self.wfile.write(s.encode("utf-8", "surrogateescape") if isinstance(s, str) else s); self.wfile.flush()
    def handle(self):
        inbox = STORE.folders["INBOX"]; msgs = list(inbox.msgs); deleted = set(); user = None; auth = False
        self.out("+OK fakemail POP3 ready\r\n")
        while True:
            l = self.rfile.readline()
            if not l: return
            l = l.decode().rstrip("\r\n"); c, _, a = l.partition(" "); c = c.upper()
            if c == "CAPA": self.out("+OK\r\nUSER\r\nUIDL\r\nTOP\r\nSASL PLAIN XOAUTH2\r\n.\r\n")
            elif c == "USER": user = a; self.out("+OK\r\n")
            elif c == "PASS":
                if user == USER and a == PASSWORD: auth = True; self.out("+OK logged in\r\n")
                else: self.out("-ERR [AUTH] Invalid login\r\n")
            elif c == "AUTH" and a.upper().startswith("XOAUTH2"):
                f = a.split()
                if len(f) > 1: b = f[1]
                else: self.out("+ \r\n"); b = self.rfile.readline().decode().strip()
                m = re.match(rb"user=([^\x01]*)\x01auth=Bearer ([^\x01]*)\x01\x01", base64.b64decode(b))
                if m and m.group(1).decode() == USER and m.group(2).decode() in TOKENS: auth = True; self.out("+OK logged in\r\n")
                else: self.out("+ eyJzdGF0dXMiOiI0MDEifQ==\r\n"); self.rfile.readline(); self.out("-ERR [AUTH] Token refused\r\n")
            elif c == "AUTH":
                f = base64.b64decode(a.split()[1]).split(b"\0") if len(a.split()) > 1 else []
                if len(f) == 3 and f[1].decode() == USER and f[2].decode() == PASSWORD: auth = True; self.out("+OK logged in\r\n")
                else: self.out("-ERR [AUTH] Invalid login\r\n")
            elif c == "QUIT":
                for i in sorted(deleted):
                    if msgs[i - 1] in inbox.msgs: inbox.msgs.remove(msgs[i - 1])
                self.out("+OK bye\r\n"); return
            elif not auth: self.out("-ERR log in first\r\n")
            elif c == "STAT": live = [m for i, m in enumerate(msgs, 1) if i not in deleted]; self.out("+OK %d %d\r\n" % (len(live), sum(len(m.raw) for m in live)))
            elif c == "LIST": self.out("+OK\r\n" + "".join("%d %d\r\n" % (i, len(m.raw)) for i, m in enumerate(msgs, 1) if i not in deleted) + ".\r\n")
            elif c == "UIDL": self.out("+OK\r\n" + "".join("%d u%05d\r\n" % (i, m.uid) for i, m in enumerate(msgs, 1) if i not in deleted) + ".\r\n")
            elif c in ("RETR", "TOP"):
                p = a.split(); i = int(p[0])
                if i < 1 or i > len(msgs) or i in deleted: self.out("-ERR no such message\r\n"); continue
                raw = msgs[i - 1].raw
                if c == "TOP":
                    h, _, b = raw.partition(b"\r\n\r\n"); raw = h + b"\r\n\r\n" + b"\r\n".join(b.split(b"\r\n")[:int(p[1])])
                lines = [(b"." + x if x.startswith(b".") else x) for x in raw.split(b"\r\n")]
                self.out(b"+OK\r\n" + b"\r\n".join(lines) + (b"" if raw.endswith(b"\r\n") else b"\r\n") + b".\r\n")
            elif c == "DELE": deleted.add(int(a)); self.out("+OK deleted\r\n")
            elif c == "NOOP": self.out("+OK\r\n")
            else: self.out("-ERR unknown\r\n")

# ---- SMTP --------------------------------------------------------------------------------------------------------------------
SENT = []
class Smtp(socketserver.StreamRequestHandler):
    def out(self, s): self.wfile.write(s.encode()); self.wfile.flush()
    def line(self):
        l = self.rfile.readline()
        if not l: raise EOFError
        return l.decode("utf-8", "replace").rstrip("\r\n")
    def handle(self):
        try: self.run()
        except (EOFError, ConnectionError): return
    def run(self):
        auth = False; frm = None; rcpt = []
        self.out("220 fakemail ESMTP ready\r\n")
        while True:
            l = self.line(); c = l[:4].upper()
            if c in ("EHLO", "HELO"): self.out("250-fakemail hello\r\n250-SIZE 35882577\r\n250-8BITMIME\r\n250-AUTH LOGIN PLAIN XOAUTH2\r\n250 SMTPUTF8\r\n")
            elif l.upper().startswith("AUTH PLAIN"):
                f = base64.b64decode(l.split()[2]).split(b"\0")
                if len(f) == 3 and f[1].decode() == USER and f[2].decode() == PASSWORD: auth = True; self.out("235 2.7.0 Accepted\r\n")
                else: self.out("535 5.7.8 Username and Password not accepted\r\n")
            elif l.upper().startswith("AUTH LOGIN"):
                self.out("334 VXNlcm5hbWU6\r\n"); u = base64.b64decode(self.line()).decode()
                self.out("334 UGFzc3dvcmQ6\r\n"); p = base64.b64decode(self.line()).decode()
                if u == USER and p == PASSWORD: auth = True; self.out("235 2.7.0 Accepted\r\n")
                else: self.out("535 5.7.8 Username and Password not accepted\r\n")
            elif l.upper().startswith("AUTH XOAUTH2"):
                f = l.split()		# (the answer on the line, or after the 334 as Microsoft does it)
                if len(f) > 2: a = f[2]
                else: self.out("334 \r\n"); a = self.line()
                d = base64.b64decode(a); m = re.match(rb"user=([^\x01]*)\x01auth=Bearer ([^\x01]*)\x01\x01", d)
                if m and m.group(2).decode() in TOKENS: auth = True; self.out("235 2.7.0 Accepted\r\n")
                else: self.out("334 eyJzdGF0dXMiOiI0MDEifQ==\r\n"); self.line(); self.out("535 5.7.8 Token refused\r\n")
            elif c == "MAIL":
                if not auth: self.out("530 5.7.0 Authentication Required\r\n"); continue
                frm = re.search(r"<([^>]*)>", l).group(1); rcpt = []; self.out("250 2.1.0 OK\r\n")
            elif c == "RCPT":
                a = re.search(r"<([^>]*)>", l).group(1)
                if a.startswith("nobody@"): self.out("550 5.1.1 No such user\r\n")
                else: rcpt.append(a); self.out("250 2.1.5 OK\r\n")
            elif c == "DATA":
                self.out("354 Go ahead\r\n"); lines = []
                while True:
                    x = self.rfile.readline()
                    if x in (b".\r\n", b".\n", b""): break
                    if x.startswith(b".."): x = x[1:]
                    lines.append(x)
                raw = b"".join(lines); SENT.append((frm, rcpt, raw))
                if USER in rcpt: STORE.folders["INBOX"].add(raw); STORE.changed()
                self.out("250 2.0.0 OK queued\r\n")
            elif c == "RSET": self.out("250 OK\r\n")
            elif c == "NOOP": self.out("250 OK\r\n")
            elif c == "QUIT": self.out("221 2.0.0 closing\r\n"); return
            else: self.out("502 5.5.1 Unrecognized command\r\n")

# ---- Microsoft's device code endpoints -----------------------------------------------------------------------------------
from http.server import BaseHTTPRequestHandler
from urllib.parse import parse_qs
POLLS = {"n": 0}
class Http(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def reply(self, code, obj):
        b = json.dumps(obj).encode(); self.send_response(code)
        self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_POST(self):
        f = {k: v[0] for k, v in parse_qs(self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()).items()}
        if f.get("client_id") != "test-client":
            return self.reply(400, {"error": "unauthorized_client", "error_description": "AADSTS700016: Application not found.\r\nTrace ID: x"})
        if self.path.endswith("/devicecode"):
            POLLS["n"] = 0
            return self.reply(200, {"user_code": "ONYX-7Q2K", "device_code": "dev-123", "verification_uri": "https://microsoft.com/devicelogin",
                                    "expires_in": 900, "interval": 1, "message": "To sign in, use a web browser..."})
        if self.path.endswith("/token"):
            g = f.get("grant_type", "")
            if g.endswith("device_code"):
                POLLS["n"] += 1
                if POLLS["n"] < 2: return self.reply(400, {"error": "authorization_pending", "error_description": "AADSTS70016: pending"})
                return self.reply(200, {"token_type": "Bearer", "access_token": "tok-1", "refresh_token": "ref-1", "expires_in": 3600})
            if g == "refresh_token":
                if f.get("refresh_token") == "ref-1": return self.reply(200, {"access_token": "tok-2", "refresh_token": "ref-2", "expires_in": 3600})
                return self.reply(400, {"error": "invalid_grant", "error_description": "AADSTS70000: expired"})
        self.reply(404, {"error": "not_found"})

class TS(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True; daemon_threads = True

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--imap", type=int, default=10143); ap.add_argument("--pop", type=int, default=10110)
    ap.add_argument("--smtp", type=int, default=10587); ap.add_argument("--http", type=int, default=10080)
    ap.add_argument("--imaps", type=int, default=0); ap.add_argument("--cert", default=None)
    ap.add_argument("--eml", default=None); ap.add_argument("--dump-sent", default=None)
    ap.add_argument("--demo", default=None); ap.add_argument("--user", default=None)
    a = ap.parse_args()
    global USER
    if a.user: USER = a.user
    if a.demo:
        # the screenshots' mailboxes (demo_mail.py)
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import demo_mail
        box = getattr(demo_mail, a.demo)()
        for folder, raw, when, flags in sorted(box.msgs, key=lambda m: m[2]):
            STORE.folders[folder].add(raw, flags, when)
    else:
        load_store(a.eml)
    servers = [TS(("127.0.0.1", a.imap), Imap), TS(("127.0.0.1", a.pop), Pop), TS(("127.0.0.1", a.smtp), Smtp), TS(("127.0.0.1", a.http), Http)]
    if a.imaps and a.cert:
        s = TS(("127.0.0.1", a.imaps), Imap)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(a.cert)
        s.socket = ctx.wrap_socket(s.socket, server_side=True); servers.append(s)
    for s in servers: threading.Thread(target=s.serve_forever, daemon=True).start()
    print("fakemail: ready", flush=True)
    try:
        while True: time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        if a.dump_sent:
            with open(a.dump_sent, "wb") as f:
                for frm, rc, raw in SENT: f.write(raw + b"\r\n----\r\n")

if __name__ == "__main__":
    main()
