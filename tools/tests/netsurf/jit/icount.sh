#!/bin/sh
# tools/tests/netsurf/jit/icount.sh -- the AArch64 instructions a qjsrun-a64 run executes,
# counted exactly by qemu-aarch64 (one instruction a translation block, each logged): the
# measure for the JIT, whose code callgrind (x86) cannot run. Slow (a few million a second):
# small workloads.
#
#   sh tools/tests/netsurf/jit/icount.sh <qjsrun-a64> file.js...     (QJS_JIT=n in the env)
bin=$1
shift
qemu-aarch64 -one-insn-per-tb -d exec,nochain -D /dev/stdout "$bin" -q "$@" | grep -c '^Trace'
