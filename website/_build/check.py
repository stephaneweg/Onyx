#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Onyx web site -- checks the generated pages. Python 3, standard library only.

    python website/_build/check.py            the checks
    python website/_build/check.py --prune    also deletes the .webp of assets/img that no page references

Every href / src / srcset must lead to a file of the site (and every #anchor to an id), every <img> has
width, height and alt, no "[[...]]" placeholder and no legacy name is left, there is one <h1> a page, no
external request, each page has its hreflang twin. MIT licence (ours)."""
import os
import re
import sys
from html.parser import HTMLParser

SITE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
errors = []
used = set()


class P(HTMLParser):
    def __init__(self):
        super().__init__()
        self.refs, self.ids, self.imgs, self.h1, self.lang = [], set(), [], 0, None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if "id" in a:
            self.ids.add(a["id"])
        if tag == "html":
            self.lang = a.get("lang")
        if tag == "h1":
            self.h1 += 1
        if tag == "img":
            self.imgs.append(a)
        for k in ("href", "src"):
            if a.get(k):
                self.refs.append(a[k])
        if a.get("srcset"):
            for part in a["srcset"].split(","):
                self.refs.append(part.split()[0])
        if a.get("content") and tag == "meta" and a.get("property") in ("og:image", "og:url"):
            pass


def pages():
    for d, _, files in os.walk(SITE):
        if os.sep + "_build" in d:
            continue
        for f in files:
            if f.endswith(".html"):
                yield os.path.join(d, f)


parsed = {}
for path in pages():
    text = open(path, encoding="utf-8").read()
    p = P()
    p.feed(text)
    parsed[path] = p
    rel = os.path.relpath(path, SITE).replace(os.sep, "/")
    if "[[" in text or "]]" in text:
        errors.append("%s: a [[placeholder]] is left" % rel)
    if re.search(r"zircon", text, re.I):
        errors.append("%s: the legacy name is in the page" % rel)
    if re.search(r"prefers-color-scheme", text):
        errors.append("%s: prefers-color-scheme" % rel)
    if p.h1 != 1:
        errors.append("%s: %d <h1>" % (rel, p.h1))
    if not p.lang:
        errors.append("%s: no lang" % rel)
    for a in p.imgs:
        for k in ("width", "height", "alt"):
            if k not in a:
                errors.append("%s: <img %s> without %s" % (rel, a.get("src"), k))
    if rel != "index.html" and 'hreflang="fr"' not in text or 'hreflang="en"' not in text:
        errors.append("%s: hreflang missing" % rel)

for path, p in parsed.items():
    rel = os.path.relpath(path, SITE).replace(os.sep, "/")
    for ref in p.refs:
        if re.match(r"^(https?:)?//|^mailto:|^data:", ref):
            errors.append("%s: external reference %s" % (rel, ref))
            continue
        if ref.startswith("/"):
            errors.append("%s: absolute path %s" % (rel, ref))
            continue
        target, _, anchor = ref.partition("#")
        if target:
            t = os.path.normpath(os.path.join(os.path.dirname(path), target.replace("/", os.sep)))
            if os.path.isdir(t):
                t = os.path.join(t, "index.html")
            if not os.path.isfile(t):
                errors.append("%s: broken link %s" % (rel, ref))
                continue
            used.add(os.path.normcase(t))
        else:
            t = path
        if anchor and t in parsed and anchor not in parsed[t].ids:
            errors.append("%s: no id for %s" % (rel, ref))

# the style sheets and the script: no url() to elsewhere, no forbidden rule
for d in ("css", "js"):
    for f in os.listdir(os.path.join(SITE, "assets", d)):
        text = open(os.path.join(SITE, "assets", d, f), encoding="utf-8").read()
        code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        if re.search(r"https?://|@import|url\(", code):
            errors.append("assets/%s/%s: an external reference or url()" % (d, f))
        if "prefers-color-scheme" in code:
            errors.append("assets/%s/%s: prefers-color-scheme" % (d, f))
        if re.search(r"zircon", text, re.I):
            errors.append("assets/%s/%s: the legacy name" % (d, f))

img = os.path.join(SITE, "assets", "img")
unused = [f for f in sorted(os.listdir(img)) if f.endswith(".webp") and os.path.normcase(os.path.join(img, f)) not in used]
icons = os.path.join(SITE, "assets", "icons")
unused_icons = [f for f in sorted(os.listdir(icons)) if os.path.normcase(os.path.join(icons, f)) not in used]

print("%d pages, %d files referenced, %d unreferenced .webp in assets/img, %d unreferenced icons"
      % (len(parsed), len(used), len(unused), len(unused_icons)))
if "--prune" in sys.argv and not errors:
    for f in unused:
        os.remove(os.path.join(img, f))
    for f in unused_icons:
        os.remove(os.path.join(icons, f))
    print("pruned")
total = 0
for d, _, files in os.walk(SITE):
    if os.sep + "_build" in d:
        continue
    for f in files:
        total += os.path.getsize(os.path.join(d, f))
print("site weight (without _build): %.2f MB" % (total / 1048576.0))
for e in errors:
    print("ERROR", e)
sys.exit(1 if errors else 0)
