#!/usr/bin/env python3
# tools/tests/netsurf/html5lib_tok.py -- the html5lib tokenizer tests (tokenizer/*.test, JSON)
# run through html5lib_tok (hubbub's tokeniser alone). Prints passed/total per file.
#
#   html5lib_tok.py <html5lib_tok binary> [-v] file.test ...
import json, subprocess, sys

def unescape(s):
    # doubleEscaped: "\\uXXXX" sequences in the strings
    return s.encode('utf-8', 'surrogatepass').decode('unicode-escape') if isinstance(s, str) else s

def deep(x, f):
    if isinstance(x, str): return f(x)
    if isinstance(x, list): return [deep(i, f) for i in x]
    if isinstance(x, dict): return {f(k): deep(v, f) for k, v in x.items()}
    return x

def norm(tokens):
    out = []
    for t in tokens:
        if t == 'ParseError' or (isinstance(t, list) and t and t[0] == 'ParseError'):
            continue
        if t[0] == 'Character' and out and out[-1][0] == 'Character':
            out[-1] = ['Character', out[-1][1] + t[1]]
            continue
        if t[0] == 'StartTag' and len(t) == 4 and not t[3]:
            t = t[:3]
        if t[0] == 'EndTag':
            t = t[:2]
        if t[0] == 'Character':
            t = list(t)
        out.append(t)
    return out

def main():
    exe = sys.argv[1]
    args = sys.argv[2:]
    verbose = '-v' in args
    files = [a for a in args if a != '-v']
    # xmlViolation.test is the XML infoset coercion's (U+FFFF -> U+FFFD, "--" split in
    # comments...), which a browser does not do (unicodeChars.test says the opposite)
    files = [f for f in files if not f.endswith('xmlViolation.test')]
    tp = tr = ts = 0
    for fn in files:
        tests = json.load(open(fn, encoding='utf-8'))
        tests = tests.get('tests', tests.get('xmlViolationTests', []))
        jobs = []
        for t in tests:
            inp, exp = t['input'], t['output']
            if t.get('doubleEscaped'):
                inp = unescape(inp)
                exp = deep(exp, unescape)
            try:
                data = inp.encode('utf-8')
            except UnicodeEncodeError:
                ts += 1          # a lone surrogate: not UTF-8
                continue
            for st in t.get('initialStates', ['Data state']):
                jobs.append((t, st, data, exp))
        feed = bytearray()
        for t, st, data, exp in jobs:
            feed += ('%s\n%s\n%d\n' % (st, t.get('lastStartTag', ''), len(data))).encode()
            feed += data + b'\n'
        res = subprocess.run([exe], input=bytes(feed), capture_output=True)
        lines = res.stdout.decode('utf-8', 'replace').split('\n')
        p = 0
        for i, (t, st, data, exp) in enumerate(jobs):
            ok = False
            got = None
            if i < len(lines) and lines[i]:
                try:
                    got = json.loads(lines[i])
                    ok = norm(got) == norm(exp)
                except Exception as e:
                    got = 'bad output: %s: %r' % (e, lines[i][:200])
            if ok:
                p += 1
            elif verbose:
                print('=== FAIL %s: %s [%s]\n  input: %r\n  expected: %s\n  got:      %s' % (
                    fn.split('/')[-1], t.get('description'), st, data,
                    json.dumps(norm(exp), ensure_ascii=False),
                    json.dumps(norm(got), ensure_ascii=False) if isinstance(got, list) else got))
        print('%-44s %4d / %4d%s' % (fn.split('/')[-1], p, len(jobs), '' if p == len(jobs) else '  *'))
        tp += p; tr += len(jobs)
    print('TOTAL tokenizer: %d / %d passed (%.1f%%)%s' % (tp, tr, 100.0 * tp / max(tr, 1),
          ' (%d tests with lone surrogates skipped)' % ts if ts else ''))

main()
