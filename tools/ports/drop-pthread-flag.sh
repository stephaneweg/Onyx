#!/bin/sh
# drop-pthread-flag.sh -- a compiler launcher (CMAKE_<LANG>_COMPILER_LAUNCHER) for the ports whose build
# adds -pthread on its own (libwebp): aarch64-onyx-elf's GCC does not know the option (it is a Linux /
# BSD target option, config/gnu-user.opt; on Onyx the threads are always there: libonyxposix, linked by
# onyx.specs), so it is removed from the command line and the rest is run unchanged.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
cmd=$1
shift
for a do
	shift
	[ "$a" = -pthread ] || set -- "$@" "$a"
done
exec "$cmd" "$@"
