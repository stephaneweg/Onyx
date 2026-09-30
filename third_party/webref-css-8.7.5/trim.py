#!/usr/bin/env python3
# Onyx: css-grammar.json from @webref/css's css.json (npm pack @webref/css@8.7.5): only what
# libcss's grammar generator (third_party/libcss/src/parse/onyx_grammar_gen.py) reads -- the
# names and value syntaxes of the properties, types, functions, at-rules (with their
# descriptors) and the selectors' names.
#   python3 trim.py <path to package/css.json> > css-grammar.json
import json, sys
w = json.load(open(sys.argv[1], encoding='utf-8'))
def keep(x, keys):
    return {k: x[k] for k in keys if k in x}
out = {
    'properties': [keep(p, ('name', 'syntax')) for p in w['properties']],
    'types': [keep(t, ('name', 'syntax', 'for')) for t in w['types']],
    'functions': [keep(f, ('name', 'syntax', 'for')) for f in w['functions']],
    'atrules': [dict(keep(a, ('name', 'syntax')),
                     descriptors=[keep(d, ('name', 'syntax', 'for')) for d in a.get('descriptors', [])])
                for a in w['atrules']],
    'selectors': [keep(s, ('name', 'syntax')) for s in w['selectors']],
}
json.dump(out, sys.stdout, indent=0, ensure_ascii=False, sort_keys=True)
sys.stdout.write('\n')
