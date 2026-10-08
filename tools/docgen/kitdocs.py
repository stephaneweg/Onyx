#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
#
# kitdocs.py -- one reference document per kit (docs/1x-<KIT>.md), made from the kits' headers.
#
# A kit's header is its reference: every function is declared there with a comment that says what
# it does. This tool turns each header into a readable document -- its sections as headings, its
# comments as text, its declarations as code -- preceded by how the kit is used (the chapter of
# docs/06-KITS-GUIDE.md) and by an index of everything it exposes. Run it after a kit's header
# changes, then docs/build_docs.py for the Word and PDF exports:
#
#     python tools/docgen/kitdocs.py
#
import os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
K = os.path.join(ROOT, "user", "Kits")
DOCS = os.path.join(ROOT, "docs")

def rd(p):
    with open(p, encoding="utf-8") as f: return f.read().replace("\r\n", "\n")

KITS = [
  # (file, kit title, guide chapter number, library, include, link, [headers], what it is)
  ("10-APPKIT", "AppKit", 3, "appkit", '#include "appkit/appkit.h"', "nothing to link: the kernel binds AppKit to every program",
   ["appkit/appkit.h"],
   "AppKit is what makes a program run: its one link to the system. Every call a program makes to the "
   "kernel is a function of AppKit (`kapi_*`), and AppKit carries the small services every program needs "
   "— strings, the console, `.ini` files, the keyboard layout, the starting of programs. Only AppKit "
   "reads the kernel's table, so the kernel can change without a program being rebuilt."),
  ("11-UIKIT", "UIKit", 4, "uikit", '#include "uikit/uikit.h"', "`lib/uikit.imp.a`",
   None,
   "UIKit is the interface: the windows and their frames, the widgets, the dialogs, the theme, the icons — "
   "and the window API itself, `uk_win_*` (`uikit/win.h`, plain C functions: what speaks to the graphics "
   "server; a C program links `lib/uikit.imp_c.a`). The rest is in the namespace `uikit`. A window is a "
   "`Root`; widgets are added to it; `run ()` is the event loop."),
  ("12-SYSTEMKIT", "SystemKit", 5, "systemkit", '#include "systemkit/systemkit.h"', "`lib/systemkit.imp.a` (C++) or `lib/systemkit.imp_c.a` (C)",
   ["systemkit/notify.h", "systemkit/clipboard.h", "systemkit/clipproto.h", "systemkit/trash.h", "systemkit/fileassoc.h",
    "systemkit/volume.h", "systemkit/wallpaper.h", "systemkit/dockconf.h", "systemkit/preloadini.h", "systemkit/autostart.h", "systemkit/locale.h", "systemkit/session.h",
    "systemkit/applet_proto.h"],
   "SystemKit is what a program says to the system and to the other programs: notifications, the "
   "clipboard, the trash, the file associations, the volume, the wallpaper, the dock, the programs loaded "
   "ahead and started at boot, the Control Panel's applets."),
  ("13-NETKIT", "NetKit", 6, "netkit", '#include "netkit/netkit.h"', "`lib/netkit.imp.a` (C++) or `lib/netkit.imp_c.a` (C)",
   ["netkit/httpc.h", "netkit/ftpfs.h", "netkit/http.hpp"],
   "NetKit is the network for the programs: a small HTTP client, the FTP volumes, and the HTTP/1.1 "
   "class with its TLS transport (`netkit/http.hpp`, included apart: still a header with its code). "
   "The sockets themselves are AppKit's (`kapi_tcp_*`)."),
  ("14-FILEKIT", "FileKit", 7, "filekit", '#include "filekit/filekit.h"', "`lib/filekit.imp.a`",
   ["filekit/filekit.h", "filekit/fsutil.h", "filekit/kvtext.h"],
   "FileKit is files and folders: whole files read and written, trees copied, moved and removed, "
   "paths, compression (zlib), archives — ZIP read and written, tar / tar.gz / gzip read — and "
   "sectioned key / value text documents read and written back (a progress file, a level pack)."),
  ("15-IMAGEKIT", "ImageKit", 8, "imagekit", '#include "imagekit/imagekit.h"', "`lib/imagekit.imp.a`",
   ["imagekit/imagekit.h"],
   "ImageKit is the system's one picture codec: BMP, GIF, PNG, JPEG, PCX and WebP read; PNG, JPEG, BMP "
   "and GIF written; resizing, turning, the photo adjustments."),
  ("16-AUDIOKIT", "AudioKit", 9, "audiokit", '#include "audiokit/audiokit.h"', "`lib/audiokit.imp.a`",
   ["audiokit/audiokit.h"],
   "AudioKit is sound: files played, notes on the General MIDI instruments, the FM synthesizer, "
   "mixing, WAV files written, and the system's output for a program that makes its own samples."),
  ("17-FONTKIT", "FontKit", 10, "fontkit", '#include "fontkit/uikitface.h"   // or "fontkit/fonts.h"', "`lib/fontkit.imp.a`",
   ["fontkit/uikitface.h", "fontkit/fonts.h"],
   "FontKit is fonts and text: FreeType as a shared library, the font manager (the card's families, "
   "a font at a size, its glyphs cached) and UIKit's anti-aliased text face."),
  ("18-PRINTERKIT", "PrinterKit", 11, "printerkit", '#include "printerkit/printerkit.h"', "`lib/printerkit.imp.a`",
   ["printerkit/printerkit.h"],
   "PrinterKit is printing: the Print dialog, a job and its pages — text, lines, shapes, pictures — "
   "handed to the print service, which makes of them what the printer takes, or a PDF file."),
  ("19-GPIOKIT", "GPIOKit", 12, "gpiokit", '#include "gpiokit/gpiokit.h"', "`lib/gpiokit.imp.a` (C++) or `lib/gpiokit.imp_c.a` (C)",
   ["gpiokit/gpiokit.h"],
   "GPIOKit is the Raspberry Pi's 40-pin header: a pin's mode and level, PWM, a servo, edges queued with "
   "their time, the I2C bus and SPI, the header's own table — and a simulated board (an SSD1306 display "
   "and a BME280 sensor on its I2C bus) for a PC or a system without the hardware. The levels are 3.3 V."),
]

PREFIX = re.compile(r"^(KAPI_FN|KAPI_C|SK_API|NK_API|FS_API|FK_KV_API|UIKIT_API)\s+")
SECTION = re.compile(r"^//\s*-{2,}\s*(.*?)\s*-*\s*$")
FUNC = re.compile(r"([A-Za-z_][\w:~]*)\s*\(")
SKIP_PP = re.compile(r"^#\s*(ifndef|ifdef|if|else|elif|endif|include|pragma|undef)\b")
KEEP_BLOCK = re.compile(r"^(typedef\s+)?(struct|class|enum|union)\b")

def brace_end(lines, i):
    """the index after the line that closes the block opened on or after line i"""
    d = 0; opened = False
    while i < len(lines):
        s = re.sub(r'"(\\.|[^"\\])*"|\'(\\.|[^\'\\])*\'|//.*$', "", lines[i])
        d += s.count("{") - s.count("}")
        if "{" in s: opened = True
        i += 1
        if opened and d <= 0: break
    return i

def internal(name):
    return "__" in name or name.endswith("_") or name in ("if", "for", "while", "switch", "return", "sizeof", "defined")

def render(header_rel, index):
    """one header -> markdown lines; index gets (name, summary, header)"""
    text = rd(os.path.join(K, header_rel))
    lines = text.split("\n")
    out = []; prose = []; code = []
    guard = None
    m = re.search(r"^#ifndef\s+(\w+)\s*\n#define\s+\1\b", text, re.M)
    if m: guard = m.group(1)
    last_prose = [""]

    def flush_prose():
        if not prose: return
        para = []; pre = []
        def fp():
            if para: out.append(" ".join(para)); out.append(""); para.clear()
        def fpre():
            if pre: out.append("```"); out.extend(pre); out.append("```"); out.append(""); pre.clear()
        for l in prose:
            if l.strip() == "": fp(); fpre(); continue
            if l.startswith("  ") or l.startswith("\t") or re.match(r"\s*\S+\s{3,}\S", l):
                fp(); pre.append(l.rstrip())
            else:
                fpre(); para.append(l.strip())
        fp(); fpre()
        last_prose[0] = " ".join(x.strip() for x in prose if x.strip())
        prose.clear()
    def flush_code():
        if not code: return
        while code and code[-1].strip() == "": code.pop()
        if code: out.append("```cpp"); out.extend(code); out.append("```"); out.append("")
        code.clear()
    def first_sentence(s):
        s = s.strip()
        m = re.match(r"(.+?[.:;])(\s|$)", s)
        s = (m.group(1) if m else s)
        return s[:160].rstrip(":;")

    i = 0
    first_comment = True
    while i < len(lines):
        l = lines[i]; t = l.strip()
        if t == "":
            if code: code.append(""); last_prose[0] = ""	# (a comment speaks of the declarations right under it)
            else: flush_prose()
            i += 1; continue
        if t.startswith("//"):
            sm = SECTION.match(t)
            if sm and sm.group(1) and len(t) > 12 and t.count("-") >= 4:
                flush_prose(); flush_code()
                out.append("### " + sm.group(1).strip(" -")); out.append(""); last_prose[0] = ""; i += 1; continue
            if code and not prose and lines[i - 1].strip() != "" and (i + 1 < len(lines) and not lines[i + 1].strip().startswith("//")) and False:
                pass
            flush_code()
            c = t[2:]
            if c.startswith(" "): c = c[1:]
            if set(c.strip()) <= set("-=*") and c.strip(): i += 1; continue
            prose.append(c); i += 1; continue
        # code
        if SKIP_PP.match(t) or t in ('extern "C" {', "}", "};") and not code:
            i += 1; continue
        if guard and re.match(r"#define\s+" + re.escape(guard) + r"\b", t): i += 1; continue
        if re.match(r"(namespace\s+\w+\s*\{|\}\s*//\s*namespace.*|using namespace\b.*)$", t): i += 1; continue
        flush_prose()
        if KEEP_BLOCK.match(t) and (t.endswith("{") or (not t.endswith(";") and i + 1 < len(lines) and lines[i + 1].strip().startswith("{")) or ("{" in t and "}" not in t)):
            e = brace_end(lines, i)
            block = lines[i:e]
            nm = re.match(r"(?:typedef\s+)?(?:struct|class|enum|union)\s+(\w+)", t)
            if nm and not internal(nm.group(1)): index.append((nm.group(1), first_sentence(last_prose[0]) or "(a type)", header_rel))
            code.extend(x.rstrip() for x in block); i = e; last_prose[0] = ""; continue
        # a declaration, or a definition whose body is dropped
        j = i; stmt = [l.rstrip()]
        def joined(): return " ".join(x.strip() for x in stmt)
        while not t.startswith("#") and not re.search(r"[;{]\s*(//.*)?$", re.sub(r'"(\\.|[^"\\])*"', '""', stmt[-1])) and j + 1 < len(lines) and lines[j + 1].strip() != "" and len(stmt) < 8:
            j += 1; stmt.append(lines[j].rstrip())
        nxt = lines[j + 1].strip() if j + 1 < len(lines) else ""
        has_body = False
        if not t.startswith("#"):
            flat = re.sub(r"//.*$", "", joined())
            if "(" in flat and ("{" in flat or nxt.startswith("{")):
                has_body = True
        shown = stmt
        if has_body:
            e = brace_end(lines, i)
            sig = re.sub(r"\s*\{.*$", "", re.sub(r"//.*$", "", joined())).rstrip()
            sig = re.sub(r"^(static\s+inline|inline\s+static|static|inline)\s+", "", sig)
            shown = [sig + ";"]; j = e - 1
        s0 = PREFIX.sub("", shown[0].lstrip())
        shown = [s0] + [x for x in shown[1:]]
        flat = " ".join(x.strip() for x in shown)
        code_part = re.sub(r"//.*$", "", flat)
        trailing = re.search(r"//\s*(.*)$", shown[-1])
        fm = None
        if not t.startswith("#") and "(" in code_part:
            head = code_part.split("(", 1)[0]
            nm = re.search(r"([A-Za-z_][\w:~]*)\s*$", head)
            if nm: fm = nm.group(1)
            fp = re.match(r"\s*typedef\b[^(]*\(\s*\*\s*(\w+)\s*\)", code_part)	# typedef void (*Name) (...)
            if fp: fm = fp.group(1)
        if fm and internal(fm):
            i = j + 1; continue
        if fm:
            summ = trailing.group(1).strip() if trailing and trailing.group(1).strip() else first_sentence(last_prose[0])
            summ = re.sub(r"^[\w.]+\.(h|hpp) -- ", "", summ)
            index.append((fm, summ, header_rel))
        if not fm: last_prose[0] = ""				# (a constant: the comment above was its own)
        code.extend(shown)
        i = j + 1
    flush_prose(); flush_code()
    return out

def guide_chapter(n):
    g = rd(os.path.join(DOCS, "06-KITS-GUIDE.md"))
    m = re.search(r"^## %d\. .*?\n(.*?)(?=^## \d+\. )" % n, g, re.M | re.S)
    body = m.group(1).strip() if m else ""
    return re.sub(r"^`#include[^\n]*\n(?:[^\n]+\n)*?\n", "", body, count = 1, flags = re.M)

def esc(s): return s.replace("|", "\\|").replace("\n", " ")

def uikit_headers():
    d = os.path.join(K, "uikit")
    hs = sorted(f for f in os.listdir(d) if f.endswith(".h") and f not in ("abi.h", "lift.h"))
    first = ["uikit.h", "widget.h", "root.h", "win.h", "canvas.h", "theme.h", "text.h", "label.h", "button.h", "dialog.h"]
    return ["uikit/" + f for f in first if f in hs] + ["uikit/" + f for f in hs if f not in first]

def abi_names(kit):
    p = os.path.join(K, kit, kit + ".abi")
    return [l.split()[1] for l in rd(p).split("\n") if l[:1].isdigit() and len(l.split()) > 1]

def main():
    made = []
    for fname, title, chap, lib, inc, link, headers, what in KITS:
        if headers is None: headers = uikit_headers()
        index = []
        ref = []
        for h in headers:
            body = render(h, index)
            if not any(x.strip() for x in body): continue
            ref.append("## `%s`" % h); ref.append("")
            ref.extend(body)
        n = len(abi_names(lib))
        doc = ["# Onyx — %s reference" % title, "",
               "*The reference of **%s** (`SD:/lib/%s.so`, `user/Kits/%s`): what it is for, how a program uses it, and "
               "every operation it exposes. The operations' part is made from the kit's headers by "
               "`tools/docgen/kitdocs.py` — the headers are the source. Overview of all the kits: "
               "[The Kits](06-KITS-GUIDE.md).*" % (title, lib, lib), "",
               "## Contents", "", "1. [What it is](#what-it-is)", "2. [Using it](#using-it)", "3. [Index](#index)"]
        for k, h in enumerate(headers): doc.append("%d. [`%s`](#%s)" % (k + 4, h, re.sub(r"[^a-z0-9]+", "", h.lower())))
        doc += ["", "---", "", "## What it is", "", what, "",
                "| | |", "|---|---|", "| Include | `%s` |" % inc.replace("|", "\\|"), "| Link | %s |" % link,
                "| Library | `SD:/lib/%s.so` — %d entries in its table (`user/Kits/%s/%s.abi`, append-only) |" % (lib, n, lib, lib),
                "| Sources | `user/Kits/%s/` |" % lib, "",
                "## Using it", "", guide_chapter(chap), ""]
        if lib == "fontkit":
            ft = [x for x in abi_names("fontkit") if x.startswith("FT_")]
            doc += ["### FreeType's own functions", "",
                    "The library exports FreeType's API under its own names — %d functions (`FT_Init_FreeType`, "
                    "`FT_New_Memory_Face`, `FT_Load_Glyph`, `FT_Render_Glyph`…), used as FreeType's documentation "
                    "says (freetype.org). The apps' build is lean: TrueType, the auto-hinter, the smooth "
                    "rasterizer. Most programs do not call them: they use the font manager and the text face "
                    "below." % len(ft), ""]
        doc += ["## Index", "", "Everything the headers declare, in their order — the details are in each header's part below.", "",
                "| Name | What it does | Header |", "|---|---|---|"]
        seen = set()
        for name, summ, h in index:
            if (name, h) in seen: continue
            seen.add((name, h))
            doc.append("| `%s` | %s | `%s` |" % (name, esc(summ) if summ else "", os.path.basename(h)))
        doc += ["", "---", ""] + ref
        text = "\n".join(doc).rstrip("\n") + "\n"
        text = re.sub(r"\n{3,}", "\n\n", text)
        out = os.path.join(DOCS, fname + ".md")
        with open(out, "w", encoding = "utf-8", newline = "\n") as f: f.write(text)
        made.append((fname, len(index), text.count("\n")))
    for m in made: print("%-16s %4d entries %6d lines" % m)

if __name__ == "__main__": main()
