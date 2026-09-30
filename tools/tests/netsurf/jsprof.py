#!/usr/bin/env python3
# tools/tests/netsurf/jsprof.py -- sums up NS_JSPROF's samples (qjs.c, the PC bench: the running
# script's stack, at most once a millisecond): the functions where the scripts spend their
# time (self: the top frame) and the ones on the stack (total), the most first.
#
#   NS_JSPROF=/tmp/js.txt <run the bench's netsurf> ; python3 tools/tests/netsurf/jsprof.py /tmp/js.txt [n]
import collections, re, sys

path = sys.argv[1]
top = int(sys.argv[2]) if len(sys.argv) > 2 else 40
self_c, tot = collections.Counter(), collections.Counter()
n = 0
frame = re.compile(r'^\s*at (.*?) \((.*?)(?::(\d+))?(?::\d+)?\)\s*$')


def name(f):
    m = frame.match(f)
    if not m:
        return f.strip()
    fn, src, line = m.group(1), m.group(2), m.group(3)
    src = src.rsplit('/', 1)[-1]
    return '%s (%s:%s)' % (fn, src, line or '?')


for l in open(path, errors='replace'):
    fs = [name(f) for f in l.rstrip('\n').split('|') if f.strip().startswith('at ')]
    if not fs:
        continue
    n += 1
    self_c[fs[0]] += 1
    for f in set(fs):
        tot[f] += 1
print('%d samples (~1 ms each)' % n)
print('-- self')
for f, c in self_c.most_common(top):
    print('%6.1f%%  %s' % (100.0 * c / n, f))
print('-- total')
for f, c in tot.most_common(top):
    print('%6.1f%%  %s' % (100.0 * c / n, f))
