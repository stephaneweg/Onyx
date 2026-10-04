#!/bin/sh
# el0scan.sh -- scan Onyx's user-space ELFs for instructions an app may not execute at EL0
# (protected mode: docs/02 section 6, docs/EL0-PROTECTED-MODE.md). Every app, /bin tool, Koton
# plugin and BASIC runtime runs at EL0; one of these instructions kills the process
# when it runs ("el0: <name> (pid N) killed" in kmsg).
#
# What the kernel lets EL0 do (El0CoreInit, per core): the counters (CNTKCTL_EL1.EL0PCTEN /
# EL0VCTEN: cntpct_el0, cntvct_el0, cntfrq_el0), SCTLR_EL1.UCI (dc cvau / cvac / civac,
# ic ivau), nTWE / nTWI (wfe / wfi), UCT (ctr_el0), DZE (dc zva); TPIDRRO_EL0 (read) = the core
# number; the PMU only with cmdline.txt el0pmu=1.
#
# Reported, per binary and function:
#   ERROR  a system register other than those (msr daifset / daifclr included), an ID register
#          (midr_el1, mpidr_el1, id_aa64*: they trap at EL0 on ARMv8.0), a dc / ic op other than
#          the ones above, tlbi, at, eret, hvc, smc, hlt, sys / sysl, dcps*, drps
#   PMU    a PMU register (allowed only with el0pmu=1: the code must not reach it otherwise)
#   note   brk / udf (fine at EL0: a deliberate trap, the process is killed when it runs) -- only
#          counted, listed with -v
#   known  not reported (listed with -v): a function whose name holds "el0scan_expected" (a
#          deliberate fault: faulttest, el0test), and libgcc's SME helpers (__arm_za_disable,
#          __arm_tpidr2_save...: tpidr2_el0, run only when __aarch64_have_sme, 0 on the A72)
#
# Use:  sh tools/el0scan.sh [-v] [file-or-dir ...]
#   no argument: the build's outputs (user/*.elf, user/bin/*.elf, the Koton plugins); a directory: every ELF
#   or static library (.a) under it (e.g. sdcard/apps sdcard/bin sdcard/koton: what is on the
#   card, no extension; third_party: the prebuilt libraries).
# Exit status: 1 if an ERROR was found, else 0.
#
# Copyright (c) 2026 the Onyx authors. MIT licence:
# Permission is hereby granted, free of charge, to any person obtaining a copy of this software
# and associated documentation files (the "Software"), to deal in the Software without
# restriction, including without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions: The above copyright notice
# and this permission notice shall be included in all copies or substantial portions of the
# Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

HERE=$(cd "$(dirname "$0")/.." && pwd)
OBJDUMP=${OBJDUMP:-aarch64-none-elf-objdump}
command -v "$OBJDUMP" >/dev/null 2>&1 || { echo "el0scan: no $OBJDUMP in PATH" >&2; exit 2; }

VERBOSE=0
[ "$1" = "-v" ] && { VERBOSE=1; shift; }

is_elf () { [ -f "$1" ] && [ "$(head -c 4 "$1" 2>/dev/null | od -An -c | tr -d ' ')" = "177ELF" ]; }
is_ar () { [ -f "$1" ] && [ "$(head -c 7 "$1" 2>/dev/null)" = "!<arch>" ]; }

list=$(mktemp)
trap 'rm -f "$list"' EXIT
if [ $# -eq 0 ]; then
	for f in "$HERE"/user/*.elf "$HERE"/user/bin/*.elf "$HERE"/user/Apps/kp_*/kp_*.elf; do
		is_elf "$f" && echo "$f" >> "$list"
	done
else
	for a in "$@"; do
		if [ -d "$a" ]; then
			find "$a" -type f | sort | while read -r f; do { is_elf "$f" || is_ar "$f"; } && echo "$f"; done >> "$list"
		elif is_elf "$a" || is_ar "$a"; then echo "$a" >> "$list"
		else echo "el0scan: $a: not an ELF" >&2
		fi
	done
fi

nfiles=0 nbad=0 nerr=0
while read -r f; do
	nfiles=$((nfiles + 1))
	out=$("$OBJDUMP" -d --no-show-raw-insn "$f" 2>/dev/null | awk -v verbose="$VERBOSE" '
	BEGIN {
		n = split("nzcv fpcr fpsr tpidr_el0 tpidrro_el0 cntfrq_el0 cntpct_el0 cntvct_el0 ctr_el0 dczid_el0 pmuserenr_el0", a, " ")
		for (i = 1; i <= n; i++) mrsok[a[i]] = 1
		n = split("nzcv fpcr fpsr tpidr_el0", a, " ")
		for (i = 1; i <= n; i++) msrok[a[i]] = 1
		n = split("cvau cvac cvap cvadp civac zva", a, " ")
		for (i = 1; i <= n; i++) dcok[a[i]] = 1
		n = split("tlbi at eret eretaa eretab hvc smc hlt sys sysl dcps1 dcps2 dcps3 drps", a, " ")
		for (i = 1; i <= n; i++) bad[a[i]] = 1
		fn = "?"
	}
	/^[0-9a-f]+ <.*>:$/ { fn = $2; sub(/^</, "", fn); sub(/>:$/, "", fn); next }
	/^ *[0-9a-f]+:\t/ {
		split($0, t, "\t"); op = t[2]; args = t[3]; sub(/ *\/\/.*$/, "", args)
		addr = t[1]; gsub(/[ :]/, "", addr)
		kind = ""
		if (op == "mrs" || op == "msr") {
			split(args, r, ","); reg = (op == "mrs") ? r[2] : r[1]; gsub(/ /, "", reg); reg = tolower(reg)
			if (reg ~ /^pm/ && reg != "pmuserenr_el0") kind = "PMU"
			else if (op == "mrs" && (reg in mrsok)) kind = ""
			else if (op == "msr" && (reg in msrok)) kind = ""
			else if (reg ~ /^(midr|mpidr|revidr|id_|aidr|clidr|ccsidr|csselr)/) kind = "ERROR(id)"
			else kind = "ERROR"
		}
		else if (op == "dc") { split(args, r, ","); o = r[1]; gsub(/ /, "", o); if (!(o in dcok)) kind = "ERROR" }
		else if (op == "ic") { split(args, r, ","); o = r[1]; gsub(/ /, "", o); if (o != "ivau") kind = "ERROR" }
		else if (op in bad) kind = "ERROR"
		else if (op == "brk" || op == "udf") { notes++; if (verbose) print "  note  " fn " +" addr ": " op " " args; next }
		if (kind != "" && (fn ~ /el0scan_expected/ || fn ~ /^__arm_(za_disable|tpidr2_save|tpidr2_restore|sme_state|get_current_vg)$/)) {
			known++; if (verbose) print "  known " fn " +" addr ": " op " " args
			next
		}
		if (kind != "") {
			key = kind "|" fn "|" op " " args
			if (!(key in seen)) { seen[key] = addr; order[++nk] = key }
			cnt[key]++
		}
	}
	END {
		for (i = 1; i <= nk; i++) {
			split(order[i], p, "|")
			printf "  %-9s %s: %s  (at %s%s)\n", p[1], p[2], p[3], seen[order[i]], (cnt[order[i]] > 1 ? ", x" cnt[order[i]] : "")
		}
		if (notes) printf "  note      %d brk/udf\n", notes
		if (known && verbose) printf "  known     %d deliberate or guarded\n", known
	}')
	if echo "$out" | grep -q "ERROR\|PMU"; then
		echo "$f"; echo "$out"
		nbad=$((nbad + 1))
		echo "$out" | grep -q ERROR && nerr=$((nerr + 1))
	elif [ "$VERBOSE" = 1 ] && [ -n "$out" ]; then
		echo "$f"; echo "$out"
	fi
done < "$list"

echo "el0scan: $nfiles ELF(s) scanned, $nbad with findings, $nerr with errors"
[ "$nerr" -eq 0 ]
