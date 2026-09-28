#!/usr/bin/env python3
# jitprof.py -- reads gcemu --jitprof's jitprof.bin (the costliest JIT blocks' code) and prints each
# block: its address, runs, guest (PowerPC) and host (AArch64) instructions, disassembled when the
# objdumps are given (a command each, e.g. on Windows "wsl ~/local/usr/bin/powerpc-linux-gnu-objdump").
#   python tools/gc/jitprof.py jitprof.bin [--top N] [--ppc CMD] [--a64 CMD] [--host]
# The file: per block u32 pc, runs (saturated), guest words, host words (little-endian), the guest
# words (big-endian, as in MEM1), then the host code.
import struct, subprocess, sys, tempfile, os, shlex

def objdump (cmd, data, arch, big):
    if not cmd: return None
    with tempfile.NamedTemporaryFile (delete = False, suffix = '.bin') as f:
        f.write (data); name = f.name
    try:
        path = name
        if cmd.startswith ('wsl'):			# (a Windows path seen from WSL)
            path = '/mnt/' + name[0].lower () + name[2:].replace ('\\', '/')
        args = shlex.split (cmd) + ['-D', '-b', 'binary', '-m', arch] + (['-EB', '-M', '750cl'] if big else []) + [path]
        out = subprocess.run (args, capture_output = True, text = True).stdout
        lines = [l for l in out.splitlines () if ':\t' in l]
        return [l.split ('\t', 2)[-1] if l.count ('\t') >= 2 else l for l in lines]
    finally:
        os.unlink (name)

def main ():
    a = sys.argv[1:]
    top, ppc, a64, host = 20, None, None, False
    path = None
    i = 0
    while i < len (a):
        if a[i] == '--top': top = int (a[i + 1]); i += 2
        elif a[i] == '--ppc': ppc = a[i + 1]; i += 2
        elif a[i] == '--a64': a64 = a[i + 1]; i += 2
        elif a[i] == '--host': host = True; i += 1
        else: path = a[i]; i += 1
    d = open (path, 'rb').read ()
    n = 0; k = 0
    while n + 16 <= len (d) and k < top:
        pc, runs, gw, hw = struct.unpack_from ('<4I', d, n); n += 16
        guest = d[n:n + gw * 4]; n += gw * 4
        code = d[n:n + hw * 4]; n += hw * 4
        print ('== block %d: %08X, %d runs, %d guest / %d host instructions (%.1f a guest one)' % (k, pc, runs, gw, hw, hw / max (gw, 1)))
        g = objdump (ppc, guest, 'powerpc:common', True)
        for j in range (gw):
            w = struct.unpack_from ('>I', guest, j * 4)[0]
            print ('  %08X  %08X  %s' % (pc + j * 4, w, g[j] if g and j < len (g) else ''))
        if host:
            h = objdump (a64, code, 'aarch64', False)
            for j in range (hw):
                w = struct.unpack_from ('<I', code, j * 4)[0]
                print ('      %4d  %08X  %s' % (j, w, h[j] if h and j < len (h) else ''))
        k += 1

main ()
