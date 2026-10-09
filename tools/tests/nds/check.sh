#!/bin/sh
# check.sh <ndstest> -- runs the test programs of roms/ (build them with src/build.sh) and says what passed.
T=$1
R=$(cd "$(dirname "$0")" && pwd)/roms
fail=0
# cputest: the C kernels on the ARM9 / ARM7, ARM / Thumb, as on the PC; the ARMv5 checks
ref=$(tr '\n' ' ' < "$R/ccheck_ref.txt")
out=$(NDS_DUMP=02300040,80 "$T" "$R/cputest.nds" 60 | tail -n 11 | tr '\n' ' ')
for k in 0 1 3 4; do
	got=$(echo $out | cut -d' ' -f$((k * 16 + 1))-$((k * 16 + 12)))
	if [ "$got " = "$ref" ] || [ "$got" = "$(echo $ref)" ]; then echo "ok   cputest ccheck $k"; else echo "FAIL cputest ccheck $k: $got"; fail=1; fi
done
v5=$(echo $out | cut -d' ' -f33)
if [ "$v5" = "00003fff" ]; then echo "ok   cputest armv5"; else echo "FAIL cputest armv5: $v5"; fail=1; fi
# systest: 11 checks on the ARM9, the ARM7's readings
out=$(NDS_TOUCH="0-300:100,50" NDS_DUMP=02300000,40 "$T" "$R/systest.nds" 120 | tail -n 6 | tr '\n' ' ')
for i in $(seq 4 14); do v=$(echo $out | cut -d' ' -f$i); if [ "$v" = "00000001" ]; then echo "ok   systest $((i - 4))"; else echo "FAIL systest $((i - 4)): $v"; fail=1; fi; done
exp="78796e4f 04bc0778 0000003f 00010126 00000052 00000008"
got=$(echo $out | cut -d' ' -f33-38)
if [ "$got" = "$exp" ]; then echo "ok   systest arm7 (firmware, touch, keys, clock, IPC)"; else echo "FAIL systest arm7: $got"; fail=1; fi
# the pictures: a few pixels (gfx2d: engines A / B; gfx3d: the triangle, the texture, the lit quad, the clear,
# the capture on the bottom screen) and the sound (both sides loud enough, 440 Hz on the left)
T2=${TMPDIR:-/tmp}/onyx_nds_check
"$T" "$R/gfx2d.nds" 10 "$T2.2d.ppm" > /dev/null
"$T" "$R/gfx3d.nds" 12 "$T2.3d.ppm" > /dev/null
NDS_WAV="$T2.wav" "$T" "$R/sound.nds" 120 > /dev/null
python3 - "$T2" <<'PY' || fail=1
import sys, wave, struct
p = sys.argv[1]
def ppm(f):
    d = open(f, 'rb').read(); i = d.index(b'255\n') + 4
    return lambda x, y: tuple(d[i + (y * 256 + x) * 3: i + (y * 256 + x) * 3 + 3])
ok = True
def want(name, got, exp, tol=24):
    global ok
    good = all(abs(a - b) <= tol for a, b in zip(got, exp))
    print(("ok   " if good else "FAIL ") + name + ("" if good else ": %s, not %s" % (got, exp)))
    ok = ok and good
a = ppm(p + '.2d.ppm')
want('gfx2d BG2 bitmap', a(200, 20), (148, 40, 66))
want('gfx2d bitmap sprite', a(205, 105), (81, 255, 81), 8)
want('gfx2d engine B (darkened)', a(10, 192 + 10), (0, 0, 0), 255)
b = ppm(p + '.3d.ppm')
want('gfx3d clear', b(5, 5), (20, 20, 85))
want('gfx3d triangle top (red)', b(78, 62), (210, 30, 40), 60)
want('gfx3d lit quad', b(50, 140), (182, 182, 182), 30)
want('gfx3d capture', b(5, 192 + 5), (16, 16, 82), 12)
w = wave.open(p + '.wav'); n = w.getnframes(); s = struct.unpack('<%dh' % (n * 2), w.readframes(n))
pl = max(abs(v) for v in s[0::2]); pr = max(abs(v) for v in s[1::2])
want('sound levels', (min(pl, 9000), min(pr, 9000)), (9000, 9000), 0)
sys.exit(0 if ok else 1)
PY
exit $fail
