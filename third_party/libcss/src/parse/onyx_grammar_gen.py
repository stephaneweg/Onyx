#!/usr/bin/env python3
# Onyx: the value grammars of every CSS property and at-rule descriptor, compiled to C tables
# for the generic grammar matcher (src/parse/onyx_grammar.c).
#
# The source is the W3C's webref consolidation of the specifications' "Value Definition
# Syntax" grammars (third_party/webref-css-8.7.5/css-grammar.json, MIT). Each grammar is
# parsed here (juxtaposition, &&, ||, |, [ ], the multipliers * + ? {A,B} # ! and their
# combinations, <type> / <'property'> / <fn()> references, keywords, literals and functions)
# into a graph of nodes; the named types and the functions are shared nodes (a reference is
# an edge), so the whole CSS grammar is one table. The numeric and token types (<length>,
# <number>, <string>, <custom-ident>...) are the matcher's own primitives (src/parse/
# onyx_grammar.h: ONYX_P_*), the math functions included.
#
#   python3 onyx_grammar_gen.py ../../../webref-css-8.7.5/css-grammar.json > onyx_grammar_tables.c
#
# The output is committed (no build-time Python). Warnings (unresolved references, grammars
# that do not parse) go to stderr; a property whose grammar cannot be built is left out
# (libcss then rejects it, as before).
import json, re, sys

src = json.load(open(sys.argv[1], encoding='utf-8'))

# ---- the primitives (must match enum onyx_prim in onyx_grammar.h) ------------------------------
PRIMS = [
    'length', 'percentage', 'length-percentage', 'number', 'integer', 'angle',
    'angle-percentage', 'time', 'time-percentage', 'frequency', 'frequency-percentage',
    'resolution', 'flex', 'dimension', 'string', 'url', 'ident', 'custom-ident',
    'dashed-ident', 'hex-color', 'urange', 'any-value', 'declaration-value', 'zero', 'id',
    'decibel', 'semitones', 'number-percentage', 'custom-property-name', 'an-plus-b',
    'function-token', 'unicode-range-token', 'dashed-function', 'ratio-number',
    'calc-sum',
]
PRIM_ALIAS = {
    'identifier': 'ident', 'ident-token': 'ident', 'string-token': 'string', 'uri': 'url',
    'number-token': 'number', 'percentage-token': 'percentage', 'dimension-token': 'dimension',
    'hash-token': 'id', 'unicode-range-token': 'urange', 'url-token': 'url',
    'an+b': 'an-plus-b', 'extension-name': 'dashed-ident', 'whole-value': 'declaration-value',
    'style-feature-value': 'declaration-value', 'style-feature-name': 'dashed-ident',
    'target-name': 'string', 'integer': 'integer',
}
# types the specifications define in prose (or whose webref syntax is not usable here)
OVERRIDES = {
    'family-name': '<string> | <custom-ident>+',
    'generic-family': 'serif | sans-serif | cursive | fantasy | monospace | system-ui | emoji | '
                      'math | fangsong | ui-serif | ui-sans-serif | ui-monospace | ui-rounded | '
                      'generic( <custom-ident> )',
    'border-style': 'none | hidden | dotted | dashed | solid | double | groove | ridge | inset | outset',
    'border-width': '<line-width>',
    'box': 'border-box | padding-box | content-box',
    'top': '<length> | auto', 'right': '<length> | auto', 'bottom': '<length> | auto',
    'left': '<length> | auto',
    'margin-width': '<length-percentage> | auto',
    'padding-width': '<length-percentage [0,∞]>',
    'font-src-list': '[ <url> [ format( <font-format> ) ]? [ tech( <font-tech># ) ]? | '
                     'local( <family-name> ) ]#',
    'url-set': '<image-set()>',     # (CSS UI 4: an image-set() of URLs)
    'age': 'child | young | old', 'gender': 'male | female | neutral',
    'voice-family-name': '<string> | <custom-ident>+',
    'level': 'x-weak | weak | medium | strong | x-strong',
    'timeline-range-name': 'cover | contain | entry | exit | entry-crossing | exit-crossing | scroll',
    'timeline-range-center-subject': 'source | target',
    'animation-action': 'none | play | play-once | play-forwards | play-backwards | pause | reset | replay',
    'url-modifier': '<ident> | <function-token> <any-value>? )',
    'size-keyword': 'auto | min-content | max-content | fit-content | stretch | contain',
    'segment-options': 'segment <integer [1,∞]>',
    'integer': None, 'number': None,     # primitives
    'color-base': None,                  # (webref's is fine)
}

# functions whose webref syntax misses what the specifications' prose allows
FUNC_FIXES = {
    # CSS Shapes: circle() takes a <length-percentage> radius, ellipse() two
    'circle()': 'circle( [ <radial-extent> | <length-percentage [0,∞]> ]? [ at <position> ]? )',
    'ellipse()': 'ellipse( [ <radial-extent> | <length-percentage [0,∞]> ]{2}? [ at <position> ]? )',
}

# properties / types whose webref syntax misses what the specifications (or the shipping
# engines' aliases of renamed keywords) allow
PROP_FIXES = {
    # CSS Anchor Positioning: anchor() / anchor-size() in every inset property
    'inset-block-start': "<'top'>", 'inset-block-end': "<'top'>",
    'inset-inline-start': "<'top'>", 'inset-inline-end': "<'top'>",
}
PROP_APPEND = {
    # anchor-center: the *-items properties too
    'justify-items': ' | anchor-center', 'align-items': ' | anchor-center',
}
TYPE_REPLACE = {
    # the names before CSS Anchor Positioning renamed them (x-self-start -> self-x-start...),
    # still what the engines ship
    'position-area': [('span-self-x-end |', 'span-self-x-end | x-self-start | x-self-end | '
                       'span-x-self-start | span-x-self-end |'),
                      ('span-self-y-end |', 'span-self-y-end | y-self-start | y-self-end | '
                       'span-y-self-start | span-y-self-end |')],
}
PROP_REPLACE = {
    'position-visibility': [('anchor-visible ||', 'anchor-visible || anchors-visible ||')],
}

types, funcs, props = {}, {}, {}
for t in src['types']:
    types.setdefault(t['name'], []).append(t)
for f in src['functions']:
    funcs.setdefault(f['name'].lower(), []).append(f)
for p in src['properties']:
    if 'syntax' in p:
        props[p['name']] = p['syntax']
for k, v in PROP_FIXES.items():
    props[k] = v
for k, v in PROP_APPEND.items():
    props[k] += v
for k, reps in PROP_REPLACE.items():
    for a, b in reps:
        assert a in props[k], (k, a)
        props[k] = props[k].replace(a, b)
for k, reps in TYPE_REPLACE.items():
    for t in types[k]:
        for a, b in reps:
            assert a in t['syntax'], (k, a)
            t['syntax'] = t['syntax'].replace(a, b)

def warn(*a):
    print('onyx_grammar_gen:', *a, file=sys.stderr)

# ---- the Value Definition Syntax tokenizer / parser --------------------------------------------
class Bad(Exception):
    pass

def vds_tokens(s):
    i, out = 0, []
    while i < len(s):
        c = s[i]
        if c.isspace():
            i += 1
            continue
        if c == '<':
            depth, j = 0, i
            while j < len(s):
                if s[j] == '<':
                    depth += 1
                elif s[j] == '>':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            if j >= len(s):
                raise Bad('unclosed <')
            out.append(('type', s[i + 1:j].strip()))
            i = j + 1
            continue
        if c == "'":
            j = s.index("'", i + 1)
            lit = s[i + 1:j]
            out.append(('lit', lit))
            i = j + 1
            continue
        if s.startswith('||', i) or s.startswith('&&', i):
            out.append(('op', s[i:i + 2]))
            i += 2
            continue
        if c == '{':
            j = s.index('}', i)
            out.append(('range', s[i + 1:j]))
            i = j + 1
            continue
        if c in '[]|*+?#!':
            out.append(('op', c))
            i += 1
            continue
        m = re.match(r'[-+]?\d[\d.]*[A-Za-z%]*', s[i:])
        if m:
            out.append(('num', m.group(0)))
            i += len(m.group(0))
            continue
        m = re.match(r'-?-?[A-Za-z_\u0080-￿][-\w\u0080-￿]*', s[i:])
        if m:
            name = m.group(0)
            i += len(name)
            if i < len(s) and s[i] == '(':
                out.append(('func', name))
                i += 1
            else:
                out.append(('kw', name))
            continue
        if c in ',/:;()=@.%':
            out.append(('lit', c))
            i += 1
            continue
        raise Bad('unexpected %r' % c)
    return out

class P:
    def __init__(self, toks):
        self.t, self.i = toks, 0
    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else None
    def next(self):
        t = self.peek()
        self.i += 1
        return t
    def one(self):
        alts = [self.any()]
        while self.peek() == ('op', '|'):
            self.next()
            alts.append(self.any())
        return alts[0] if len(alts) == 1 else ('one', alts)
    def any(self):
        alts = [self.all()]
        while self.peek() == ('op', '||'):
            self.next()
            alts.append(self.all())
        return alts[0] if len(alts) == 1 else ('any', alts)
    def all(self):
        alts = [self.seq()]
        while self.peek() == ('op', '&&'):
            self.next()
            alts.append(self.seq())
        return alts[0] if len(alts) == 1 else ('all', alts)
    def seq(self):
        items = []
        while True:
            t = self.peek()
            if t is None or t in (('op', '|'), ('op', '||'), ('op', '&&'), ('op', ']'), ('lit', ')')):
                break
            items.append(self.mult())
        if not items:
            raise Bad('empty sequence')
        return items[0] if len(items) == 1 else ('seq', items)
    def mult(self):
        a = self.atom()
        while True:
            t = self.peek()
            if t == ('op', '*'):
                self.next(); a = ('mult', a, 0, None, False)
            elif t == ('op', '+'):
                self.next(); a = ('mult', a, 1, None, False)
            elif t == ('op', '?'):
                self.next(); a = ('mult', a, 0, 1, False)
            elif t == ('op', '!'):
                self.next(); a = ('bang', a)
            elif t == ('op', '#'):
                self.next()
                lo, hi = 1, None
                if self.peek() and self.peek()[0] == 'range':
                    lo, hi = self.rng(self.next()[1])
                a = ('mult', a, lo, hi, True)
            elif t and t[0] == 'range':
                self.next()
                lo, hi = self.rng(t[1])
                a = ('mult', a, lo, hi, False)
            else:
                return a
    def rng(self, s):
        parts = [x.strip() for x in s.split(',')]
        lo = int(parts[0])
        if len(parts) == 1:
            return lo, lo
        return lo, (int(parts[1]) if parts[1] else None)
    def atom(self):
        t = self.next()
        if t is None:
            raise Bad('unexpected end')
        if t == ('op', '['):
            n = self.one()
            if self.next() != ('op', ']'):
                raise Bad('expected ]')
            return n
        if t[0] == 'kw':
            return ('kw', t[1])
        if t[0] == 'num':
            return ('num', t[1])
        if t[0] == 'lit':
            if len(t[1]) == 1:
                return ('char', t[1])
            raise Bad('literal %r' % t[1])
        if t[0] == 'type':
            return ('ref', t[1])
        if t[0] == 'func':
            if self.peek() == ('lit', ')'):
                self.next()
                return ('func', t[1], None)
            n = self.one()
            if self.next() != ('lit', ')'):
                raise Bad('expected ) in %s(' % t[1])
            return ('func', t[1], n)
        raise Bad('unexpected %r' % (t,))

def parse(s):
    p = P(vds_tokens(s))
    n = p.one()
    if p.peek() is not None:
        raise Bad('trailing %r' % (p.peek(),))
    return n

# ---- compilation into the node table -------------------------------------------------------------
OPS = {'kw': 1, 'char': 2, 'prim': 3, 'seq': 4, 'all': 5, 'any': 6, 'one': 7, 'mult': 8,
       'func': 9, 'ref': 10, 'bang': 11, 'num': 12}
nodes = []          # (op, flags, a, b, c)
kids = []
strings = {}
pool = bytearray()
ranges = [(0.0, 0.0)]    # index 0: none
node_cache = {}

def sidx(s):
    s = s.lower()
    if s not in strings:
        strings[s] = len(pool)
        pool.extend(s.encode('utf-8') + b'\0')
    return strings[s]

def new_node(op, flags=0, a=0, b=0, c=0):
    key = (op, flags, a, b, c)
    if op != OPS['ref'] and key in node_cache:
        return node_cache[key]
    nodes.append([op, flags, a, b, c])
    i = len(nodes) - 1
    if op != OPS['ref']:
        node_cache[key] = i
    return i

named = {}          # ('type'|'prop'|'func', name, def index) -> node index (a ref placeholder)
pending = []

def rng_index(spec):
    # "[0,∞]" / "[-90deg,90deg]" / "[0Hz,∞]" -- the numbers (units are the type's canonical)
    m = re.match(r'\[\s*([^,]+),\s*([^\]]+)\]', spec)
    if not m:
        return 0
    def num(x):
        x = x.strip()
        if x in ('∞', '+∞', 'infinity'):
            return 1e30
        if x in ('-∞', '-infinity'):
            return -1e30
        mm = re.match(r'[-+]?[\d.]+', x)
        return float(mm.group(0)) if mm else 0.0
    r = (num(m.group(1)), num(m.group(2)))
    if r not in ranges:
        ranges.append(r)
    return ranges.index(r)

def pick(defs, ctx):
    for d in defs:
        f = d.get('for')
        if f and any(x in ctx for x in f):
            return defs.index(d)
    for d in defs:
        if not d.get('for'):
            return defs.index(d)
    return 0

def ref_named(kind, name, ctx):
    """a node for <name> (kind: type / prop / func)"""
    if kind == 'type':
        defs = types.get(name, [])
        di = pick(defs, ctx) if defs else 0
    elif kind == 'func':
        defs = funcs.get(name.lower(), [])
        di = pick(defs, ctx) if defs else 0
    else:
        di = 0
    key = (kind, name, di)
    if key in named:
        return named[key]
    i = new_node(OPS['ref'], 0, 0xffff)
    named[key] = i
    pending.append((i, kind, name, di, ctx))
    return i

MATH = {'calc', 'min', 'max', 'clamp', 'round', 'mod', 'rem', 'sin', 'cos', 'tan', 'asin',
        'acos', 'atan', 'atan2', 'pow', 'sqrt', 'hypot', 'log', 'exp', 'abs', 'sign'}

def resolve_type(name, ctx):
    rng = 0
    m = re.match(r"^([^\s\[]+)\s*(\[.*\])?$", name)
    if m and m.group(2):
        name, rng = m.group(1), rng_index(m.group(2))
    if name.startswith("'") and name.endswith("'"):
        p = name[1:-1]
        if p not in props:
            raise Bad("unknown property <'%s'>" % p)
        return ref_named('prop', p, ctx)
    if name.endswith('()'):
        fn = name[:-2]
        if fn.lower() in MATH:
            raise Bad('math function reference <%s>' % name)
        return ref_named('func', name, ctx)
    pn = PRIM_ALIAS.get(name, name)
    if pn in PRIMS and (name not in OVERRIDES or OVERRIDES[name] is None):
        return new_node(OPS['prim'], 0, PRIMS.index(pn), rng)
    if name in OVERRIDES and OVERRIDES[name] is not None:
        return ref_named('type', name, ctx)
    if name in types:
        return ref_named('type', name, ctx)
    raise Bad('unknown type <%s>' % name)

def compile_node(n, ctx):
    k = n[0]
    if k == 'kw':
        return new_node(OPS['kw'], 0, sidx(n[1]), len(n[1].encode('utf-8')))
    if k == 'char':
        return new_node(OPS['char'], 0, ord(n[1]))
    if k == 'num':
        return new_node(OPS['num'], 0, sidx(n[1]))
    if k == 'ref':
        return resolve_type(n[1], ctx)
    if k in ('seq', 'all', 'any', 'one'):
        ch = [compile_node(x, ctx) for x in n[1]]
        if k in ('all', 'any') and len(ch) > 16:
            raise Bad('too many || / && terms')
        start = len(kids)
        kids.extend(ch)
        return new_node(OPS[k], 0, start, len(ch))
    if k == 'mult':
        c = compile_node(n[1], ctx)
        return new_node(OPS['mult'], 1 if n[4] else 0, c, n[2], 0xffff if n[3] is None else n[3])
    if k == 'bang':
        return new_node(OPS['bang'], 0, compile_node(n[1], ctx))
    if k == 'func':
        fname = n[1].lower()
        if fname in MATH:
            raise Bad('math function %s() in a grammar' % fname)
        return new_node(OPS['func'], 0, sidx(n[1]), 0xffff if n[2] is None else compile_node(n[2], ctx))
    raise Bad('node %r' % (n,))

def body_of(kind, name, di):
    if kind == 'prop':
        return props[name], [name]
    if kind == 'type':
        if name in OVERRIDES and OVERRIDES[name] is not None:
            return OVERRIDES[name], [name]
        d = types[name][di]
        if 'syntax' not in d:
            raise Bad('<%s> has no syntax' % name)
        return d['syntax'], [name]
    if name.lower() in FUNC_FIXES:
        return FUNC_FIXES[name.lower()], [name]
    d = funcs[name.lower()][di] if name.lower() in funcs else None
    if d is None or 'syntax' not in d:
        raise Bad('function %s has no syntax' % name)
    return d['syntax'], [name]

failed = set()

def build_pending():
    while pending:
        i, kind, name, di, ctx = pending.pop()
        try:
            syn, own = body_of(kind, name, di)
            target = compile_node(parse(syn), ctx | set(own))
            nodes[i][2] = target
        except (Bad, ValueError) as e:
            failed.add((kind, name))
            warn('%s %s: %s' % (kind, name, e))
            nodes[i][2] = 0xffff      # a dead reference: never matches

# node 0: a node that never matches (a dead reference points nowhere)
new_node(OPS['ref'], 0, 0xffff)

prop_roots = {}
for name in sorted(props):
    try:
        prop_roots[name] = compile_node(parse(props[name]), {name})
    except (Bad, ValueError) as e:
        warn('property %s: %s' % (name, e))
    build_pending()

desc_roots = {}
for a in src['atrules']:
    if a['name'] in ('@media', '@container'):
        continue            # (media / container features: not declarations)
    for d in a['descriptors']:
        if 'syntax' not in d:
            continue
        try:
            desc_roots[(a['name'], d['name'])] = compile_node(parse(d['syntax']), {a['name'], d['name']})
        except (Bad, ValueError) as e:
            warn('descriptor %s/%s: %s' % (a['name'], d['name'], e))
        build_pending()

# a property whose grammar reaches a dead reference at its top level is left out if the
# dead part is required; the matcher treats a dead reference as "never matches", which is
# right for an alternative and makes a required part fail -- so nothing more to do here.

# collapse the references: a child that is a resolved reference points at its target
def target(i, seen=None):
    seen = seen or set()
    while nodes[i][0] == OPS['ref'] and nodes[i][2] != 0xffff and i not in seen:
        seen.add(i)
        i = nodes[i][2]
    return i

for n in nodes:
    op = n[0]
    if op in (OPS['mult'], OPS['bang']):
        n[2] = target(n[2])
    elif op == OPS['func'] and n[3] != 0xffff:
        n[3] = target(n[3])
for j, kid in enumerate(kids):
    kids[j] = target(kid)
for k in prop_roots:
    prop_roots[k] = target(prop_roots[k])
for k in desc_roots:
    desc_roots[k] = target(desc_roots[k])

if len(nodes) >= 0xffff or len(kids) >= 0xffff or len(pool) >= 0xffff:
    sys.exit('onyx_grammar_gen: table too large (%d nodes, %d kids, %d string bytes)' %
             (len(nodes), len(kids), len(pool)))

for k in prop_roots:
    sidx(k)
for k in desc_roots:
    sidx(k[0] + '/' + k[1])

# ---- output --------------------------------------------------------------------------------
o = sys.stdout
o.write('/* Generated by onyx_grammar_gen.py from third_party/webref-css-8.7.5/css-grammar.json\n'
        ' * (the W3C webref consolidation of the CSS specifications\' value grammars, MIT).\n'
        ' * Do not edit: run the generator again. */\n\n#include "parse/onyx_grammar.h"\n\n')
o.write('const onyx_gnode onyx_grammar_nodes[%d] = {\n' % len(nodes))
for n in nodes:
    o.write('\t{%d,%d,%d,%d,%d},\n' % tuple(n))
o.write('};\n\nconst uint16_t onyx_grammar_kids[%d] = {\n' % max(1, len(kids)))
for j in range(0, len(kids), 16):
    o.write('\t' + ','.join(str(x) for x in kids[j:j + 16]) + ',\n')
o.write('};\n\nconst char onyx_grammar_strings[%d] =\n' % (len(pool) + 1))
data = bytes(pool)
for j in range(0, len(data), 64):
    chunk = data[j:j + 64]
    o.write('\t"' + ''.join('\\000' if b == 0 else ('\\x%02x""' % b if b > 126 else chr(b))
                             for b in chunk) + '"\n')
o.write(';\n\nconst float onyx_grammar_ranges[%d][2] = {\n' % len(ranges))
for r in ranges:
    o.write('\t{%.6e,%.6e},\n' % r)
o.write('};\n\n/* the properties, sorted by name */\nconst onyx_gname onyx_grammar_props[%d] = {\n' % len(prop_roots))
for k in sorted(prop_roots):
    o.write('\t{%d,%d}, /* %s */\n' % (sidx(k), prop_roots[k], k))
o.write('};\nconst unsigned onyx_grammar_nprops = %d;\n\n' % len(prop_roots))
o.write('/* the at-rules\' descriptors, sorted by "@rule/descriptor" */\n')
dk = sorted(desc_roots, key=lambda x: x[0] + '/' + x[1])
o.write('const onyx_gname onyx_grammar_descs[%d] = {\n' % max(1, len(dk)))
for k in dk:
    o.write('\t{%d,%d}, /* %s */\n' % (sidx(k[0] + '/' + k[1]), desc_roots[k], k[0] + '/' + k[1]))
o.write('};\nconst unsigned onyx_grammar_ndescs = %d;\n' % len(dk))
warn('%d properties, %d descriptors, %d nodes, %d kids, %d string bytes' %
     (len(prop_roots), len(dk), len(nodes), len(kids), len(pool)))
