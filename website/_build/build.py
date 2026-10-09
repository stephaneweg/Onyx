#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Onyx web site -- the page generator. Python 3, standard library only.

    python website/_build/build.py

writes the nine pages (index.html, fr/..., en/...) from the copy in content.py. The pages are plain
files with relative paths: commit them as they are; nothing has to run to view the site.
MIT licence (ours)."""
import html
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SITE = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import content as C  # noqa: E402
from alts import ALTS as CURATED  # noqa: E402

# True: links say ".../index.html" (the site then also opens from the file system, without a server).
# False: clean folder links ("../applications/"), what a static host serves.
FILE_LINKS = False

LANGS = ("fr", "en")
# On a phone a whole screen or a strip is shown enlarged and cut, not shrunk: which part of a screen shows
# (0 0 = its top left, the default; .5 .5 = its middle), and how much a strip is enlarged (1.7 by default).
FOCUS = {"setup-0": (.5, .4)}
ZOOM = {"menubar": 2.2}
WASHES = ("blueberry", "lagoon", "mint", "grape")

# ---------------------------------------------------------------------------------------------- images
SIZES = {}
for _l in open(os.path.join(SITE, "assets", "img", "sizes.tsv"), encoding="utf-8"):
    _p = _l.split()
    if len(_p) >= 3:
        SIZES[_p[0]] = (int(_p[1]), int(_p[2]))
USED = set()        # the captures the pages reference (build.py prints the list)

# The sizes attribute for each place a capture stands in; {n} = the widest it is ever shown, in px.
SZ = {
    "hero": "(max-width: 600px) 165vw, (max-width: 1336px) 71vw, {n}px",
    "feature": "(max-width: 600px) 150vw, (max-width: 900px) 88vw, (max-width: 1440px) 58vw, {n}px",
    "small": "(max-width: 600px) 220vw, (max-width: 900px) 88vw, (max-width: 1440px) 58vw, {n}px",
    "card": "(max-width: 600px) 150vw, (max-width: 900px) 88vw, (max-width: 1400px) 40vw, 560px",
    "screen": "(max-width: 1100px) 92vw, {n}px",
    "panel": "(max-width: 600px) 165vw, (max-width: 1100px) 92vw, {n}px",
    "full": "(max-width: 1100px) 94vw, {n}px",
}


class Page:
    """What a template needs to know about the page being written."""

    def __init__(self, lang, key):
        self.lang, self.key = lang, key
        self.i = LANGS.index(lang)
        self.depth = 1 + (1 if C.SLUGS[key][self.i] else 0)
        self.root = "../" * self.depth
        self.wash = 0

    def t(self, pair):
        s = pair[self.i].replace("'", "\u2019")        # typographic apostrophes, in both languages
        s = s.replace("Raspberry Pi 4", "Raspberry Pi\u00a04")   # the "4" never alone on a line
        if self.lang == "fr":       # French punctuation never starts a line
            s = re.sub(r" ([:;?!])", "\u00a0\\1", s)
        return html.escape(s, quote=True)

    def url(self, key, lang=None, anchor=""):
        lang = lang or self.lang
        path = self.root + lang + "/" + C.SLUGS[key][LANGS.index(lang)]
        return path + ("index.html" if FILE_LINKS else "") + anchor

    def alt(self, base, fallback=None):
        """A capture's alt text: content.ALTS, then the curator's (alts.py), then what the section gives."""
        pair = C.ALTS.get(base) or CURATED.get(base) or fallback
        if pair is None:
            raise SystemExit("no alt text for " + base)
        return self.t(pair)

    def next_wash(self):
        w = WASHES[self.wash % len(WASHES)]
        self.wash += 1
        return w

    def img(self, base, alt=None, cls="", ctx="feature", lazy=True, cap=1100, attrs=""):
        """An <img> with its width and height, its srcset when a half-size copy exists, and --native:
        the width it must never exceed (its own pixels; `cap` shows a very wide capture at half size)."""
        w, h = SIZES[base]
        USED.add(base)
        alt = self.alt(base, alt)
        src = "%sassets/img/%s.webp" % (self.root, base)
        half = os.path.exists(os.path.join(SITE, "assets", "img", base + "-half.webp"))
        native = w
        out = ['<img']
        if cls:
            out.append(' class="%s"' % cls)
        if half:
            USED.add(base + "-half")
            hw = (w + 1) // 2
            if w > cap:
                native = hw
            out.append(' src="%s" srcset="%sassets/img/%s-half.webp %dw, %s %dw" sizes="%s"'
                       % (src, self.root, base, hw, src, w, SZ[ctx].format(n=native)))
        else:
            out.append(' src="%s"' % src)
        out.append(' width="%d" height="%d" style="--native:%d"' % (w, h, native))
        if lazy:
            out.append(' loading="lazy"')
        out.append(' decoding="async" alt="%s"%s>' % (alt, attrs))
        return "".join(out)


GEM = ('<svg viewBox="0 0 32 32" fill="none" aria-hidden="true"><path d="M16 13 28.5 9v5.5L16 19Z" style="fill:var(--accent)"/>'
       '<g stroke="currentColor" stroke-width="1.8" stroke-linejoin="round" stroke-linecap="round">'
       '<path d="M16 1.5 28.5 9v14L16 30.5 3.5 23V9Z"/><path d="M3.5 9 16 13 28.5 9M16 1.5v29M28.5 14.5 16 19"/></g></svg>')

HEAD_SCRIPT = """<script>
/* Before the first paint: dark unless the visitor chose light (no flash). ?theme=light forces it (a link, a test). */
(function (d) {
  d.classList.add('js');
  /* If site.js never runs to its end (not loaded, an error), nothing may stay hidden: back to the page without script. */
  window.addEventListener('load', function () { if (!window.onyxReady) d.classList.remove('js'); });
  try { if (/[?&]theme=light/.test(location.search) || localStorage.getItem('onyx-theme') === 'light') d.setAttribute('data-theme', 'light'); } catch (e) {}
})(document.documentElement);
</script>"""

# Line icons (Get Onyx): 24 px, 1.6 px stroke, round joins, currentColor -- the gem's drawing style.
LINE_ICONS = {
    "pi": '<rect x="3" y="5" width="18" height="14" rx="2"/><rect x="7.5" y="9" width="5" height="5" rx=".8"/><path d="M16 9h2M16 12h2M16 15h2M6 5V3M10 5V3M14 5V3M18 5V3"/>',
    "card": '<path d="M7 3h7l4 4v13a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1Z"/><path d="M9.5 6.5v3M12 6.5v3M14.5 8v1.5"/>',
    "display": '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M9 20h6M12 16v4"/>',
    "keyboard": '<rect x="2.5" y="7" width="19" height="10" rx="2"/><path d="M6 10.5h.01M9 10.5h.01M12 10.5h.01M15 10.5h.01M18 10.5h.01M7.5 13.8h9"/>',
    "spaces": '<rect x="3" y="4" width="8" height="7" rx="1.5"/><rect x="13" y="4" width="8" height="7" rx="1.5"/><rect x="3" y="13" width="8" height="7" rx="1.5"/><rect x="13" y="13" width="8" height="7" rx="1.5"/>',
    "printer": '<path d="M7 8V3.5h10V8"/><rect x="3" y="8" width="18" height="9" rx="2"/><path d="M7 14h10v6.5H7zM17.5 11.5h.01"/>',
    "laptop": '<rect x="4.5" y="5" width="15" height="10.5" rx="1.5"/><path d="M2.5 19h19"/>',
    "wifi": '<path d="M2.8 9.2a13.5 13.5 0 0 1 18.4 0M5.9 12.6a9 9 0 0 1 12.2 0M9 16a4.5 4.5 0 0 1 6 0"/><path d="M12 19.2h.01"/>',
}


def line_icon(name):
    return ('<svg class="line-icon" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="currentColor" '
            'stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">%s</svg>' % LINE_ICONS[name])


# ---------------------------------------------------------------------------------------------- the frame
def head(p):
    title, desc = C.META[p.key]
    o = ['<!doctype html>', '<html lang="%s">' % p.lang, '<head>', '<meta charset="utf-8">',
         '<meta name="viewport" content="width=device-width, initial-scale=1">',
         '<title>%s</title>' % p.t(title), '<meta name="description" content="%s">' % p.t(desc)]
    def where(lang):       # absolute when the site's address is known, relative otherwise
        return C.SITE_URL + lang + "/" + C.SLUGS[p.key][LANGS.index(lang)] if C.SITE_URL else p.url(p.key, lang)
    if C.SITE_URL:
        o.append('<link rel="canonical" href="%s">' % where(p.lang))
    for l in LANGS:
        o.append('<link rel="alternate" hreflang="%s" href="%s">' % (l, where(l)))
    o.append('<link rel="alternate" hreflang="x-default" href="%s">' % where("en"))
    o += ['<meta property="og:type" content="website">', '<meta property="og:site_name" content="Onyx">',
          '<meta property="og:title" content="%s">' % p.t(title),
          '<meta property="og:description" content="%s">' % p.t(desc),
          '<meta property="og:locale" content="%s">' % ("fr_FR" if p.lang == "fr" else "en_GB")]
    if C.SITE_URL:
        o += ['<meta property="og:url" content="%s">' % where(p.lang),
              '<meta property="og:image" content="%sassets/og.png">' % C.SITE_URL,
              '<meta property="og:image:width" content="1200">', '<meta property="og:image:height" content="630">',
              '<meta name="twitter:card" content="summary_large_image">']
    o += ['<meta name="theme-color" content="#0B0B0B">',
          '<link rel="icon" href="%sfavicon.svg" type="image/svg+xml">' % p.root,
          '<link rel="icon" href="%sfavicon-32.png" type="image/png" sizes="32x32">' % p.root,
          '<link rel="apple-touch-icon" href="%sapple-touch-icon.png">' % p.root,
          '<link rel="stylesheet" href="%sassets/css/tokens.css">' % p.root,
          '<link rel="stylesheet" href="%sassets/css/site.css">' % p.root,
          HEAD_SCRIPT, '</head>']
    return "\n".join(o)


def header(p):
    def nav(key, cls=""):
        cur = ' aria-current="page"' if key == p.key else ''
        return '<a%s href="%s"%s>%s</a>' % (cls and ' class="%s"' % cls, p.url(key), cur, p.t(C.NAV[key]))

    langs = "".join('<a href="%s" lang="%s" hreflang="%s"%s>%s</a>'
                    % (p.url(p.key, l), l, l, ' aria-current="true"' if l == p.lang else '', l.upper()) for l in LANGS)
    return """<a class="skip" href="#main">%(skip)s</a>
<header class="site-header">
  <div class="container container--wide bar">
    <a class="brand" href="%(home)s" aria-label="%(brand)s">%(gem)s Onyx</a>
    <nav class="nav" id="nav" aria-label="%(navl)s">
      %(apps)s
      %(desktop)s
      %(get)s
    </nav>
    <div class="header-end">
      <div class="lang" role="group" aria-label="%(langl)s">%(langs)s</div>
      <button class="theme-btn" type="button" aria-label="%(theme)s" aria-pressed="false">
        <svg class="sun" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2.5v2.2M12 19.3v2.2M2.5 12h2.2M19.3 12h2.2M5.3 5.3l1.5 1.5M17.2 17.2l1.5 1.5M5.3 18.7l1.5-1.5M17.2 6.8l1.5-1.5"/></svg>
        <svg class="moon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M20 14.2A8 8 0 0 1 9.8 4a8 8 0 1 0 10.2 10.2Z"/></svg>
      </button>
      <a class="btn btn--ghost btn--small header-cta" href="%(geturl)s"%(getcur)s>%(getl)s</a>
      <button class="menu-btn" type="button" aria-label="%(menu)s" aria-expanded="false" aria-controls="nav"><span></span></button>
    </div>
  </div>
</header>""" % {
        "skip": p.t(C.UI["skip"]), "home": p.url("home"), "brand": p.t(C.UI["brand_label"]), "gem": GEM,
        "navl": p.t(C.UI["nav_label"]), "apps": nav("apps"), "desktop": nav("desktop"), "get": nav("get", "nav-cta"),
        "langl": p.t(C.UI["lang_label"]), "langs": langs, "theme": p.t(C.UI["theme_label"]),
        "geturl": p.url("get"), "getcur": ' aria-current="page"' if p.key == "get" else '',
        "getl": p.t(C.GET_ONYX), "menu": p.t(C.UI["menu_label"])}


def footer(p):
    links = "".join('<li><a href="%s">%s</a></li>' % (p.url(k), p.t(C.NAV[k])) for k in ("home", "apps", "desktop", "get"))
    other = LANGS[1 - p.i]
    legal = ['<p>%s</p>' % (p.t(C.FOOTER["about"]) % html.escape(C.AUTHOR))]
    if C.GITHUB_URL:
        legal.append('<p>%s</p>' % (p.t(C.FOOTER["open_source"]) % ('<a href="%s" rel="noopener">GitHub</a>' % html.escape(C.GITHUB_URL, True))))
    legal.append('<p>%s</p>' % p.t(C.FOOTER["trademark"]))
    legal.append('<p>© %s %s</p>' % (C.YEAR, html.escape(C.AUTHOR)))
    return """<footer class="site-footer">
  <div class="container container--wide">
    <div class="footer-top">
      <a class="brand" href="%(home)s" aria-label="%(brand)s">%(gem)s Onyx</a>
      <nav class="footer-col" aria-label="%(site)s">
        <p class="footer-h">%(site)s</p>
        <ul class="footer-links">%(links)s</ul>
      </nav>
      <div class="footer-col">
        <p class="footer-h">%(langh)s</p>
        <ul class="footer-links"><li><a href="%(other)s" lang="%(ol)s" hreflang="%(ol)s">%(otherl)s</a></li></ul>
      </div>
    </div>
    <div class="footer-legal">
      %(legal)s
    </div>
  </div>
</footer>""" % {"home": p.url("home"), "brand": p.t(C.UI["brand_label"]), "gem": GEM, "site": p.t(C.FOOTER["col_site"]),
                "links": links, "langh": p.t(C.FOOTER["col_lang"]), "other": p.url(p.key, other), "ol": other,
                "otherl": p.t(C.UI["other_lang"]), "legal": "\n      ".join(legal)}


def document(p, body, tail=""):
    return "\n".join([head(p), '<body class="page-%s">' % p.key, header(p), '<main id="main">', body, '</main>',
                      footer(p), tail, '<script src="%sassets/js/site.js" onerror="document.documentElement.classList.remove(\'js\')"></script>' % p.root, '</body>', '</html>', ''])


# ---------------------------------------------------------------------------------------------- blocks
def screen(p, base, alt=None, ctx="screen", lazy=True):
    """A whole-screen capture in its thin display; on a phone it shows enlarged, around its focus."""
    fx, fy = FOCUS.get(base, (0, 0))
    return ('<div class="shot-screen" style="--native:%d;--fx:%s;--fy:%s">%s</div>'
            % (SIZES[base][0], fx, fy, p.img(base, alt, ctx=ctx, lazy=lazy)))


def media(p, kind, base, alt, bleed=True):
    """A capture in the frame its kind calls for (design.md section 7)."""
    if kind == "window":
        cut = C.CUTS.get(base)
        return ('<figure class="stage%s%s reveal" data-wash="%s" data-delay="1">%s</figure>'
                % (" stage--bleed" if bleed else "", " stage--cut-" + cut if cut else "", p.next_wash(), p.img(base, alt, "shot-window")))
    if kind == "screen":
        return ('<figure class="feature-media reveal" data-delay="1">%s</figure>' % screen(p, base, alt, "feature"))
    if kind == "crops":         # two small pieces of the desktop, side by side
        return ('<div class="crop-pair reveal" data-delay="1">%s</div>'
                % "".join('<figure class="shot-crop" style="--native:%d">%s</figure>' % (SIZES[b][0], p.img(b, alt, ctx="feature")) for b in base))
    return ('<figure class="feature-media reveal" data-delay="1"><div class="shot-crop shot-crop--zoom" style="--native:%d;--zoom:%s">%s</div></figure>'
            % (SIZES[base][0], ZOOM.get(base, 1.7), p.img(base, alt, ctx="small")))


def feature(p, f, alt_bg, flip, kind="window", shot=None):
    text = ['<div class="feature-text reveal">']
    if f.get("eyebrow"):
        text.append('<p class="eyebrow">%s</p>' % p.t(f["eyebrow"]))
    text.append('<h2>%s</h2>' % p.t(f["title"]))
    text.append('<p class="t-lead">%s</p>' % p.t(f["body"]))
    if f.get("points"):
        text.append('<ul class="points">%s</ul>' % "".join('<li>%s</li>' % p.t(x) for x in f["points"]))
    if f.get("small"):
        text.append('<p class="t-small feature-note">%s</p>' % p.t(f["small"]))
    if f.get("link"):
        label, key, anchor = f["link"]
        text.append('<a class="more" href="%s">%s</a>' % (p.url(key, anchor=anchor), p.t(label)))
    text.append('</div>')
    return """<section class="section%s">
  <div class="container container--wide feature%s">
    %s
    %s
  </div>
</section>""" % (" section--alt" if alt_bg else "", " feature--flip" if flip else "", "\n    ".join(text),
                 media(p, kind, shot, f.get("alt")))


def cta(p, title, body=None):
    return """<section class="section section--band cta">
  <div class="container center">
    <h2 class="reveal">%s</h2>%s
    <div class="actions reveal" data-delay="2"><a class="btn btn--primary" href="%s">%s</a></div>
  </div>
</section>""" % (p.t(title), '\n    <p class="t-lead reveal" data-delay="1">%s</p>' % p.t(body) if body else "",
                 p.url("get"), p.t(C.GET_ONYX))


def shots_note(p, english_shots):
    """French pages that show English captures say so (content.md, small strings)."""
    if p.lang == "fr" and english_shots:
        return '<p class="t-small shots-note container">%s</p>' % p.t(C.UI["shots_english"])
    return ""


# ---------------------------------------------------------------------------------------------- home
def home(p):
    H = C.HOME_HERO
    o = ["""<section class="hero">
  <div class="container center">
    <h1 class="reveal">%s</h1>
    <p class="t-lead reveal" data-delay="1">%s</p>
    <div class="actions reveal" data-delay="2">
      <a class="btn btn--primary" href="%s">%s</a>
      <a class="more" href="%s">%s</a>
    </div>
  </div>
  <div class="container container--wide">
    <div class="hero-comp reveal" data-delay="2">
      %s
      %s
    </div>
  </div>
</section>""" % (p.t(H["title"]).replace(". ", ".<br>", 1), p.t(H["lead"]), p.url("get"), p.t(C.GET_ONYX), p.url("apps"),
                 p.t(H["secondary"]),
                 p.img(H["shots"][0][0], H["shots"][0][1], "shot-window", ctx="hero", lazy=False),
                 p.img(H["shots"][1][0], H["shots"][1][1], "shot-window", ctx="hero", lazy=False))]

    D = C.HOME_DESKTOP
    o.append("""<section class="section showcase">
  <div class="container center">
    <p class="eyebrow reveal">%s</p>
    <h2 class="reveal">%s</h2>
    <p class="t-lead reveal" data-delay="1">%s</p>
    <p class="reveal" data-delay="1"><a class="more" href="%s">%s</a></p>
  </div>
  <div class="container container--wide">
    <figure class="showcase-media reveal" data-delay="1">
      %s
    </figure>
  </div>
</section>""" % (p.t(D["eyebrow"]), p.t(D["title"]), p.t(D["body"]), p.url("desktop"), p.t(D["link"]),
                 screen(p, D["shot"], D["alt"])))

    alt_bg, flip = True, False
    for f in C.HOME_FEATURES + C.HOME_MORE + [C.HOME_LEARN]:
        o.append(feature(p, f, alt_bg, flip, "window", f["shot"][p.i]))
        alt_bg, flip = not alt_bg, not flip

    A = C.HOME_APPS
    apps = {a["id"]: a for a in C.CATALOGUE}
    tabs, panels = [], []
    for n, (aid, shot, title, tag) in enumerate(A["tiles"]):
        tabs.append('<button type="button" role="tab" id="tab-%s" aria-controls="panel-%s" aria-selected="%s"%s>'
                    '<img class="app-icon app-icon--2x" src="%sassets/icons/%s.png" width="80" height="80" alt="" loading="lazy">%s</button>'
                    % (aid, aid, "true" if n == 0 else "false", "" if n == 0 else ' tabindex="-1"', p.root, aid, p.t(title)))
        alt = (C.UI["window_of"][0] % title[0], C.UI["window_of"][1] % title[1])
        panels.append("""<div class="apps-panel center" role="tabpanel" id="panel-%s" aria-labelledby="tab-%s" tabindex="0">
      %s
      <p class="t-lead"><b>%s.</b> %s</p>
    </div>""" % (aid, aid, p.img(shot, alt, "shot-window", ctx="panel"), p.t(title), p.t(apps[aid]["pitch"])))
    o.append("""<section class="section section--lift apps-band">
  <div class="container center">
    <h2 class="reveal">%s</h2>
  </div>
  <div class="apps-row reveal" role="tablist" aria-label="%s">
    %s
  </div>
  <div class="container container--wide apps-panels reveal">
    %s
  </div>
  <p class="center apps-all"><a class="more" href="%s">%s</a></p>
</section>""" % (p.t(A["title"]), p.t(C.UI["apps_tabs"]), "\n    ".join(tabs), "\n    ".join(panels), p.url("apps"),
                 p.t(A["button"])))

    o.append(shots_note(p, True))
    o.append(cta(p, C.HOME_CTA["title"], C.HOME_CTA["body"]))
    return "\n\n".join(o)


# ---------------------------------------------------------------------------------------------- apps
def app_alt(p, a, shot):
    return p.alt(shot, (C.UI["window_of"][0] % a["name"][0], C.UI["window_of"][1] % a["name"][1]))


def shot_link(p, a, cls, inner, attrs=""):
    """A link to an app's capture; site.js opens it in the page's <dialog>, without script it opens the file."""
    shot = a["shot"][p.i]
    w, h = SIZES[shot]
    USED.add(shot)
    return ('<a class="%s"%s href="%sassets/img/%s.webp" data-shot data-w="%d" data-h="%d" data-alt="%s" data-caption="%s%s">%s</a>'
            % (cls, attrs, p.root, shot, w, h, app_alt(p, a, shot), p.t(C.UI["shot_prefix"]), p.t(a["name"]), inner))


def app_feature(p, a, wide=False):
    shot = a["shot"][p.i]
    o = ['<article class="app-feature%s reveal" id="app-%s">' % (" app-feature--wide" if wide else "", a["id"]),
         shot_link(p, a, "stage stage--bleed app-stage" + (" stage--cut-" + C.CUTS[shot] if shot in C.CUTS else ""),
                   p.img(shot, None, "shot-window", ctx="feature" if wide else "card"), ' data-wash="%s"' % p.next_wash()),
         '<div class="app-feature-body">',
         '<div class="app-head"><img class="app-icon" src="%sassets/icons/%s.png" width="40" height="40" alt="" loading="lazy">'
         '<h3>%s</h3></div>' % (p.root, a["id"], p.t(a["name"])),
         '<p>%s</p>' % p.t(a["pitch"]),
         '<ul class="points">%s</ul>' % "".join('<li>%s</li>' % p.t(x) for x in a["points"])]
    if a.get("small"):
        o.append('<p class="t-small">%s</p>' % p.t(a["small"]))
    o += ['</div>', '</article>']
    return "\n      ".join(o)


def app_card(p, a, mini=False):
    """A small card: the icon, the name, one line. mini = a dense tile (the games), the icon at 40 px."""
    name = p.t(a["name"])
    title = shot_link(p, a, "card-link", name) if a["shot"] else name
    return """<article class="app-card%s reveal" id="app-%s">
        <img class="app-icon%s" src="%sassets/icons/%s.png" width="%d" height="%d" alt="" loading="lazy">
        <h3>%s</h3>
        <p>%s</p>
      </article>""" % (" app-card--mini" if mini else "", a["id"], "" if mini else " app-icon--2x", p.root, a["id"],
                       40 if mini else 80, 40 if mini else 80, title, p.t(a["pitch"]))


def apps(p):
    H = C.APPS_HERO
    chips = ['<a class="chip" href="#catalogue" data-filter="all">%s</a>' % p.t(C.UI["all"])]
    chips += ['<a class="chip" href="#%s" data-filter="%s">%s</a>' % (gid, gid, p.t(title)) for gid, title, _ in C.GROUPS]
    o = ["""<section class="hero hero--sub">
  <div class="container center">
    <h1 class="reveal">%s</h1>
    <p class="t-lead reveal" data-delay="1">%s</p>
  </div>
</section>

<nav class="filters" aria-label="%s">
  <div class="container container--wide filters-row">
    %s
  </div>
</nav>

<div id="catalogue">""" % (p.t(H["title"]), p.t(H["lead"]), p.t(C.UI["filter_label"]), "\n    ".join(chips))]

    alt_bg = False
    for gid, title, intro in C.GROUPS:
        gt = p.t(title)
        mine = [a for a in C.CATALOGUE if a["group"] == gid]
        feat = [a for a in mine if a["featured"] and a["id"] != "gamelib"]
        small = [a for a in mine if not a["featured"]]
        s = ['<section class="section%s group" id="%s" data-group="%s" aria-labelledby="h-%s">' % (" section--alt" if alt_bg else "", gid, gid, gid),
             '  <div class="container container--wide">',
             '    <div class="group-head reveal">',
             '      <h2 id="h-%s">%s</h2>' % (gid, gt),
             '      <p class="t-lead">%s</p>' % p.t(intro),
             '    </div>']
        if feat:
            s.append('    <div class="featured-grid">\n      %s\n    </div>'
                     % "\n      ".join(app_feature(p, a, len(feat) % 2 == 1 and a is feat[-1]) for a in feat))
        if small:
            mini = gid == "play"
            s.append('    <div class="card-grid%s">\n      %s\n    </div>'
                     % (" card-grid--mini" if mini else "", "\n      ".join(app_card(p, a, mini) for a in small)))
        if gid == "play":
            E = C.EMULATORS
            lib = [a for a in mine if a["id"] == "gamelib"][0]
            s.append("""    <div class="featured-grid consoles">
      %s
      <article class="emulators reveal" data-delay="1" id="app-emulators">
        <h3>%s</h3>
        <ul class="points">%s</ul>
        <p>%s</p>
        <p class="t-small">%s</p>
      </article>
    </div>""" % (app_feature(p, lib), p.t(E["title"]), "".join('<li>%s</li>' % html.escape(x) for x in E["systems"]),
                 p.t(E["body"]), p.t(E["legal"])))
        s += ['  </div>', '</section>']
        o.append("\n".join(s))
        if gid == "work":
            T = C.TOGETHER
            o.append("""<section class="section section--lift" data-group="work" data-band>
  <div class="container container--wide feature feature--flip">
    <div class="feature-text reveal">
      <h2>%s</h2>
      <p class="t-lead">%s</p>
    </div>
    <figure class="stage stage--bleed reveal" data-wash="%s" data-delay="1">%s</figure>
  </div>
</section>""" % (p.t(T["title"]), p.t(T["body"]), p.next_wash(), p.img(T["shot"], T["alt"], "shot-window")))
        alt_bg = not alt_bg
    o.append('</div>')
    o.append(shots_note(p, True))
    o.append(cta(p, C.APPS_CTA["title"], C.APPS_CTA["body"]))
    return "\n\n".join(x for x in o if x)


SHOT_DIALOG = """<dialog class="shot-dialog" aria-labelledby="shot-caption">
  <form method="dialog"><button class="dialog-close" type="submit" aria-label="%s"><svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" aria-hidden="true"><path d="M6 6l12 12M18 6 6 18"/></svg></button></form>
  <figure><img class="shot-window" alt="" width="1008" height="654"><figcaption class="t-small" id="shot-caption"></figcaption></figure>
</dialog>"""


# ---------------------------------------------------------------------------------------------- desktop
def desktop(p):
    H = C.DESKTOP_HERO
    o = ["""<section class="hero hero--sub hero--media">
  <div class="container center">
    <h1 class="reveal">%s</h1>
    <p class="t-lead reveal" data-delay="1">%s</p>
  </div>
  <div class="container container--wide">
    <figure class="showcase-media reveal" data-delay="2">
      %s
    </figure>
  </div>
</section>""" % (p.t(H["title"]), p.t(H["lead"]), screen(p, H["shot"], H["alt"], lazy=False))]

    alt_bg, flip = False, False
    for f in C.DESKTOP_FEATURES:
        if f.get("themes"):
            sw, figs = [], []
            for n, (name, colour, shot, alt) in enumerate(f["swatches"]):
                sw.append('<button type="button" class="swatch" style="--swatch:%s" data-look="%s" aria-pressed="%s">'
                          '<span class="swatch-dot" aria-hidden="true"></span>%s</button>'
                          % (colour, shot, "true" if n == 0 else "false", name))
                figs.append('<figure class="showcase-media look" data-look="%s">%s'
                            '<figcaption class="t-small">%s</figcaption></figure>'
                            % (shot, screen(p, shot, alt), name))
            o.append("""<section class="section section--lift showcase looks">
  <div class="container center">
    <h2 class="reveal">%s</h2>
    <p class="t-lead reveal" data-delay="1">%s</p>
    <div class="swatches reveal" data-delay="1" role="group" aria-label="%s">
      %s
    </div>
  </div>
  <div class="container container--wide reveal" data-delay="1">
    %s
  </div>
</section>""" % (p.t(f["title"]), p.t(f["body"]), p.t(C.UI["themes_label"]), "\n      ".join(sw), "\n    ".join(figs)))
            alt_bg = True           # the next band is the alternate one, after the lifted band
            continue
        o.append(feature(p, f, alt_bg, flip, f["kind"], f["shot"]))
        flip = not flip
        alt_bg = not alt_bg
    cards = []
    for n, (icon, title, body, small) in enumerate(C.DESKTOP_ALSO):
        cards.append('<article class="also-card reveal"%s>%s<h2>%s</h2><p>%s</p>%s</article>'
                     % (' data-delay="%d"' % n if n else "", line_icon(icon), p.t(title), p.t(body),
                        '<p class="t-small">%s</p>' % p.t(small) if small else ""))
    o.append("""<section class="section%s">
  <div class="container container--wide also-grid also-grid--%d">
    %s
  </div>
</section>""" % (" section--alt" if alt_bg else "", len(cards), "\n    ".join(cards)))
    o.append(shots_note(p, True))
    o.append(cta(p, C.DESKTOP_CTA["title"]))
    return "\n\n".join(x for x in o if x)


# ---------------------------------------------------------------------------------------------- get
def get(p):
    H = C.GET_HERO
    if C.DOWNLOAD_URL:
        action = '<a class="btn btn--primary" href="%s">%s</a>' % (html.escape(C.DOWNLOAD_URL, True), p.t(H["button"]))
        note = '<p class="t-small reveal" data-delay="2">%s</p>' % p.t(C.DOWNLOAD_NOTE) if C.DOWNLOAD_NOTE[p.i] else ""
    else:
        action = '<button class="btn btn--soon" type="button" disabled>%s</button>' % p.t(H["button"])
        note = '<p class="t-small soon-note reveal" data-delay="2">%s</p>' % p.t(C.UI["soon"])
    N, S, Q = C.GET_NEED, C.GET_STEPS, C.GET_FAQ
    o = ["""<section class="hero hero--sub">
  <div class="container center">
    <h1 class="reveal">%s</h1>
    <p class="t-lead reveal" data-delay="1">%s</p>
    <div class="actions reveal" data-delay="2">%s</div>
    %s
  </div>
</section>""" % (p.t(H["title"]), p.t(H["lead"]), action, note)]

    o.append("""<section class="section" aria-labelledby="h-need">
  <div class="container">
    <h2 class="reveal" id="h-need">%s</h2>
    <ul class="need-grid">
      %s
    </ul>
    <p class="need-note reveal">%s</p>
    <p class="t-small reveal">%s %s</p>
  </div>
</section>""" % (p.t(N["title"]),
                 "\n      ".join('<li class="need-card reveal">%s<span>%s</span></li>' % (line_icon(i), p.t(t)) for i, t in N["items"]),
                 p.t(N["optional"]), p.t(N["card_note"]), p.t(N["unsupported"])))

    steps = "\n      ".join('<li class="step reveal"><span class="step-n" aria-hidden="true">%d</span><h3>%s</h3><p>%s</p></li>'
                            % (n + 1, p.t(t), p.t(b)) for n, (t, b) in enumerate(S["steps"]))
    o.append("""<section class="section section--alt" aria-labelledby="h-steps">
  <div class="container container--wide">
    <h2 class="visually-hidden" id="h-steps">%s</h2>
    <ol class="steps">
      %s
    </ol>
    <figure class="showcase-media reveal">
      %s
    </figure>
  </div>
</section>""" % (p.t(S["label"]), steps, screen(p, S["shot"], S["alt"])))

    o.append("""<section class="section" aria-labelledby="h-faq">
  <div class="container faq">
    <h2 class="reveal" id="h-faq">%s</h2>
    <div class="faq-list reveal">
      %s
    </div>
  </div>
</section>""" % (p.t(Q["title"]),
                 "\n      ".join('<details><summary>%s</summary><p>%s</p></details>' % (p.t(q), p.t(a)) for q, a in Q["items"])))

    if C.ONYX_REMOTE_URL and C.KOTON_WINDOWS_URL:
        P = C.GET_PC
        o.append("""<section class="section section--alt">
  <div class="container center textblock">
    <h2 class="reveal">%s</h2>
    <p class="t-lead reveal" data-delay="1">%s</p>
    <div class="actions reveal" data-delay="1"><a class="more" href="%s">%s</a><a class="more" href="%s">%s</a></div>
  </div>
</section>""" % (p.t(P["title"]), p.t(P["body"]), html.escape(C.ONYX_REMOTE_URL, True), p.t(P["remote"]),
                 html.escape(C.KOTON_WINDOWS_URL, True), p.t(P["koton"])))
    o.append(shots_note(p, True))
    return "\n\n".join(x for x in o if x)


# ---------------------------------------------------------------------------------------------- root
def root_page():
    fr, en = C.ROOT["lead"]
    alts = "\n".join('<link rel="alternate" hreflang="%s" href="%s/">' % (l, l) for l in LANGS)
    return """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Onyx</title>
<meta name="description" content="%(den)s">
%(alts)s
<link rel="alternate" hreflang="x-default" href="en/">
<meta name="theme-color" content="#0B0B0B">
<link rel="icon" href="favicon.svg" type="image/svg+xml">
<link rel="icon" href="favicon-32.png" type="image/png" sizes="32x32">
<link rel="apple-touch-icon" href="apple-touch-icon.png">
<link rel="stylesheet" href="assets/css/tokens.css">
<link rel="stylesheet" href="assets/css/site.css">
<script>
/* To the visitor's language: French if the browser says so, English otherwise. Without script, the two links below. */
(function () {
  var l = (navigator.languages && navigator.languages[0]) || navigator.language || '';
  location.replace((/^fr/i.test(l) ? 'fr/' : 'en/')%(index)s);
})();
</script>
</head>
<body class="page-root">
<main class="root-main">
  <h1 class="brand root-brand">%(gem)s Onyx</h1>
  <p class="t-lead" lang="fr">%(fr)s</p>
  <p class="t-lead">%(en)s</p>
  <p class="actions">
    <a class="btn btn--ghost" href="fr/%(idx)s" lang="fr" hreflang="fr">Français</a>
    <a class="btn btn--ghost" href="en/%(idx)s" hreflang="en">English</a>
  </p>
</main>
</body>
</html>
""" % {"den": html.escape(C.META["home"][1][1], True), "alts": alts, "gem": GEM, "fr": html.escape(fr), "en": html.escape(en),
       "index": " + 'index.html'" if FILE_LINKS else "", "idx": "index.html" if FILE_LINKS else ""}


# ---------------------------------------------------------------------------------------------- main
BUILDERS = {"home": home, "apps": apps, "desktop": desktop, "get": get}


def write(rel, text):
    path = os.path.join(SITE, rel.replace("/", os.sep))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote", rel, len(text.encode("utf-8")), "bytes")


def main():
    write("index.html", root_page())
    for lang in LANGS:
        for key in ("home", "apps", "desktop", "get"):
            p = Page(lang, key)
            tail = SHOT_DIALOG % p.t(C.UI["close"]) if key == "apps" else ""
            write("%s/%sindex.html" % (lang, C.SLUGS[key][p.i]), document(p, BUILDERS[key](p), tail))
    robots = "User-agent: *\nAllow: /\n"
    sitemap = os.path.join(SITE, "sitemap.xml")
    if C.SITE_URL:
        urls = [C.SITE_URL + l + "/" + C.SLUGS[k][LANGS.index(l)] for k in ("home", "apps", "desktop", "get") for l in LANGS]
        write("sitemap.xml", '<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
              + "".join("  <url><loc>%s</loc></url>\n" % html.escape(u) for u in urls) + "</urlset>\n")
        robots += "Sitemap: %ssitemap.xml\n" % C.SITE_URL
    elif os.path.exists(sitemap):
        os.remove(sitemap)
    write("robots.txt", robots)
    with open(os.path.join(HERE, "used-images.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(sorted(USED)) + "\n")
    print(len(USED), "image files referenced (the list: _build/used-images.txt)")


if __name__ == "__main__":
    main()
