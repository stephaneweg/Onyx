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
exit $fail
