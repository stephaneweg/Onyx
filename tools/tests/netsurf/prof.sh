#!/bin/sh
# tools/tests/netsurf/prof.sh -- names the samples of NS_PROF (host_stubs.c, the PC bench's
# sampling profiler): the functions where the time is spent (self) and the ones on the stack
# (total), the most first.
#
#   NS_PROF=/tmp/p.txt <run the bench's netsurf> ; sh tools/tests/netsurf/prof.sh /tmp/p.txt [binary]
prof=$1; bin=${2:-${OUT:-/tmp/nsbench}/build/netsurf}
tr ' ' '\n' < "$prof" | grep . | sort -u > "$prof.addr"
sed 's/^/0x/' "$prof.addr" | addr2line -f -e "$bin" | paste - - | awk '{print $1}' > "$prof.names"
paste -d' ' "$prof.addr" "$prof.names" > "$prof.map"
python3 - "$prof" "$prof.map" <<'PY'
import sys, collections
names = dict(l.split(' ', 1) for l in open(sys.argv[2]).read().split('\n') if ' ' in l)
self_c = collections.Counter(); tot = collections.Counter(); n = 0
for l in open(sys.argv[1]):
    a = l.split()
    if not a: continue
    n += 1
    fs = [names.get(x, x).strip() for x in a]
    self_c[fs[0]] += 1
    for f in set(fs): tot[f] += 1
print("%d samples (1 ms each)" % n)
print("-- self"); [print("%6.1f%%  %s" % (100.0 * c / n, f)) for f, c in self_c.most_common(30)]
print("-- total"); [print("%6.1f%%  %s" % (100.0 * c / n, f)) for f, c in tot.most_common(40)]
PY
