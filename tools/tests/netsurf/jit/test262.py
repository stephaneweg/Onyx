#!/usr/bin/env python3
"""
tools/tests/netsurf/jit/test262.py -- test262 on qjsrun (build.sh): each test's harness
includes, its strict / sloppy modes, its expected error, async completion; the pass count of
one build, or -- with two -- the tests whose outcome differs (a change to the engine must not
change any; a JIT against the interpreter). Module tests and the features QuickJS-ng lacks
are skipped.

  python3 tools/tests/netsurf/jit/test262.py --t262 ~/test262 --bin /tmp/qjsrun/qjsrun \\
      [--bin2 /tmp/qjsrun-base/qjsrun] [-j 4] [test/language/expressions ...]

A binary may be "qemu-aarch64 /tmp/qjsrun/qjsrun-a64" (quoted).
"""
import argparse, os, re, subprocess, sys, tempfile, concurrent.futures, shlex

SKIP_FEATURES = {'Atomics', 'SharedArrayBuffer', 'tail-call-optimization', 'Temporal',
                 'ShadowRealm', 'decorators', 'import-defer', 'source-phase-imports',
                 'Intl.Locale-info', 'IsHTMLDDA', 'cross-realm', 'host-gc-required',
                 'import-bytes', 'immutable-arraybuffer', 'Atomics.pause'}


def meta(src):
    m = re.search(r'/\*---(.*?)---\*/', src, re.S)
    y = m.group(1) if m else ''
    def lst(key):
        mm = re.search(r'^\s*' + key + r':\s*\[(.*?)\]', y, re.M | re.S)
        if mm:
            return [x.strip() for x in mm.group(1).split(',') if x.strip()]
        mm = re.search(r'^\s*' + key + r':\s*\n((?:\s+-\s*.*\n)+)', y, re.M)
        if mm:
            return [x.strip()[1:].strip() for x in mm.group(1).strip('\n').split('\n')]
        return []
    neg = re.search(r'negative:\s*\n\s*phase:\s*(\w+)\s*\n\s*type:\s*(\w+)', y)
    return {'includes': lst('includes'), 'flags': lst('flags'), 'features': lst('features'),
            'negative': (neg.group(1), neg.group(2)) if neg else None}


def run_one(binary, t262, path, harness_cache):
    src = open(path, encoding='utf-8', errors='replace').read()
    md = meta(src)
    flags = md['flags']
    if 'module' in flags or SKIP_FEATURES & set(md['features']):
        return 'skip'
    modes = []
    if 'raw' in flags:
        modes = ['raw']
    elif 'onlyStrict' in flags:
        modes = ['strict']
    elif 'noStrict' in flags:
        modes = ['sloppy']
    else:
        modes = ['sloppy', 'strict']
    incs = ['assert.js', 'sta.js'] + (['doneprintHandle.js'] if 'async' in flags else []) + md['includes']
    outcome = 'pass'
    for mode in modes:
        pre = ''
        if mode != 'raw':
            for inc in incs:
                if inc not in harness_cache:
                    harness_cache[inc] = open(os.path.join(t262, 'harness', inc), encoding='utf-8').read()
                pre += harness_cache[inc] + '\n'
        body = ('"use strict";\n' if mode == 'strict' else '') + pre + src
        with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False, encoding='utf-8') as f:
            f.write(body)
            name = f.name
        try:
            p = subprocess.run(shlex.split(binary) + [name], capture_output=True, timeout=60)
            out = p.stdout.decode('utf-8', 'replace')
            code = p.returncode
        except subprocess.TimeoutExpired:
            out, code = 'timeout', -9
        finally:
            os.unlink(name)
        neg = md['negative']
        if code < 0 or code > 1 or 'timeout' == out:
            res = 'crash' if code != -9 else 'timeout'
        elif neg:
            res = 'pass' if code == 1 and neg[1] in out else 'fail'
        elif 'async' in flags:
            res = 'pass' if 'Test262:AsyncTestComplete' in out and code == 0 else 'fail'
        else:
            res = 'pass' if code == 0 else 'fail'
        if res != 'pass':
            outcome = res + ':' + mode
            break
    return outcome


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--t262', required=True)
    ap.add_argument('--bin', required=True)
    ap.add_argument('--bin2')
    ap.add_argument('-j', type=int, default=os.cpu_count())
    ap.add_argument('dirs', nargs='*')
    a = ap.parse_args()
    dirs = a.dirs or ['test/language', 'test/built-ins']
    tests = []
    for d in dirs:
        for root, _, files in os.walk(os.path.join(a.t262, d)):
            for f in files:
                if f.endswith('.js') and '_FIXTURE' not in f:
                    tests.append(os.path.join(root, f))
    tests.sort()
    cache = {}
    def job(t):
        r1 = run_one(a.bin, a.t262, t, cache)
        r2 = run_one(a.bin2, a.t262, t, cache) if a.bin2 else None
        return t, r1, r2
    counts = {}
    diffs = []
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        for t, r1, r2 in ex.map(job, tests):
            k = r1.split(':')[0]
            counts[k] = counts.get(k, 0) + 1
            if a.bin2 and r1 != r2:
                diffs.append((os.path.relpath(t, a.t262), r1, r2))
    print('tests %d: %s' % (len(tests), ', '.join('%s %d' % kv for kv in sorted(counts.items()))))
    if a.bin2:
        print('differing from --bin2: %d' % len(diffs))
        for t, r1, r2 in diffs[:200]:
            print('  %s: %s (bin2: %s)' % (t, r1, r2))
    return 1 if diffs else 0


if __name__ == '__main__':
    sys.exit(main())
