#!/usr/bin/env python3
"""
mediacheck.py -- mediatest.sh's checks on what the bench's stand-in sound output heard
(SIM_SOUNDOUT: s16 L R at 44100 Hz) and on the frames' reference sums.

  mediacheck.py heard <sound.raw> <ref.raw>    the reference PCM (44.1 kHz s16 stereo) found in
                                               the sound, bit-exact (+-1: the resampler's rounding)
  mediacheck.py sum <frames.txt> <index>       the RGB sum of frame <index> (mkmedia.py's)
  mediacheck.py tone <sound.raw> <hz> <rate>   the strongest frequency of the sound is <hz>
"""
import math, struct, sys


def heard(sound, ref):
    s = open(sound, 'rb').read()
    r = open(ref, 'rb').read()
    S = struct.unpack('<%dh' % (len(s) // 2), s[:len(s) // 2 * 2])
    Rf = struct.unpack('<%dh' % (len(r) // 2), r[:len(r) // 2 * 2])
    # find the reference's first non-silent stretch in the sound
    probe = Rf[2000:2064]
    for off in range(0, len(S) - len(Rf), 2):
        if abs(S[off + 2000] - probe[0]) > 1 or abs(S[off + 2001] - probe[1]) > 1:
            continue
        if all(abs(S[off + 2000 + k] - probe[k]) <= 1 for k in range(len(probe))):
            badk = [k for k in range(min(len(Rf), len(S) - off)) if abs(S[off + k] - Rf[k]) > 1]
            print('heard at frame %d: %d of %d samples differ by more than 1%s' % (off // 2, len(badk), len(Rf),
                  (' (at ' + ', '.join('%d: %d not %d' % (k // 2, S[off + k], Rf[k]) for k in badk[:4]) + ')') if badk else ''))
            return 0 if not badk else 1
    print('not heard')
    return 1


def ref_sum(frames, index):
    for line in open(frames):
        f = line.split()
        if int(f[0]) == int(index):
            print(f[3])
            return 0
    return 1


def tone(sound, hz, rate):
    s = open(sound, 'rb').read()
    S = struct.unpack('<%dh' % (len(s) // 2), s[:len(s) // 2 * 2])
    L = [S[2 * i] for i in range(len(S) // 2)]
    n = min(len(L), 8192)
    seg = L[len(L) // 2 - n // 2:len(L) // 2 + n // 2] if len(L) > n else L
    best, bf = 0, 0
    for f in range(100, 2000, 10):
        re = sum(seg[i] * math.cos(2 * math.pi * f * i / rate) for i in range(0, len(seg), 2))
        im = sum(seg[i] * math.sin(2 * math.pi * f * i / rate) for i in range(0, len(seg), 2))
        p = re * re + im * im
        if p > best:
            best, bf = p, f
    print('strongest %d Hz' % bf)
    return 0 if abs(bf - int(hz)) <= 10 else 1


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'heard':
        sys.exit(heard(sys.argv[2], sys.argv[3]))
    if cmd == 'sum':
        sys.exit(ref_sum(sys.argv[2], sys.argv[3]))
    if cmd == 'tone':
        sys.exit(tone(sys.argv[2], sys.argv[3], int(sys.argv[4])))
