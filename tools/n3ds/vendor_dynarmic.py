#!/usr/bin/env python3
# MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors
"""vendor_dynarmic.py -- copies into third_party/ what Onyx builds of Dynarmic (the ARM11 JIT of the 3DS emulator,
docs/3DS-EMULATOR-STUDY.md) and of Boost: the sources its CMake build compiled for AArch64 with the A32 front end
alone, and the headers they included (ninja's dependency log) -- not the x86-64 / RISC-V back ends, the A64 front
end, the tests, nor the rest of Boost.

    python3 tools/n3ds/vendor_dynarmic.py <work> <build> <commit>

<work>   holds dynarmic/ (git clone https://github.com/azahar-emu/dynarmic + its submodules fmt, mcl, oaknut,
         robin-map) and boost_1_86_0/ (Boost's headers);
<build>  a CMake + Ninja build of it made with the bare-metal toolchain and -DDYNARMIC_FRONTENDS=A32
         (-DDYNARMIC_USE_BUNDLED_EXTERNALS=ON -DFMT_OS=OFF -DDYNARMIC_TESTS=OFF, Onyx's shim headers of
         user/Libs/dynarmic/onyx on the include path);
<commit> Dynarmic's commit (the folder's name: third_party/dynarmic-<commit>).
The Onyx build itself needs none of that: user/Libs/dynarmic/Makefile compiles the copied tree."""
import os, re, shutil, subprocess, sys

def main ():
	if len (sys.argv) != 4: sys.exit (__doc__)
	work, build, commit = os.path.abspath (sys.argv[1]), os.path.abspath (sys.argv[2]), sys.argv[3]
	root = os.path.dirname (os.path.dirname (os.path.dirname (os.path.abspath (__file__))))
	dyn, boost = os.path.join (work, "dynarmic"), os.path.join (work, "boost_1_86_0")
	out_dyn = os.path.join (root, "third_party", "dynarmic-" + commit)
	out_boost = os.path.join (root, "third_party", "boost-1.86.0")
	files = set ()
	deps = subprocess.run (["ninja", "-C", build, "-t", "deps"], capture_output = True, text = True, check = True).stdout
	for line in deps.splitlines ():
		if line.startswith ("    "): files.add (os.path.normpath (line.strip ()))
	cmds = subprocess.run (["ninja", "-C", build, "-t", "commands"], capture_output = True, text = True, check = True).stdout
	for m in re.finditer (r" -c (\S+)", cmds): files.add (os.path.normpath (m.group (1)))
	for d in (out_dyn, out_boost):
		if os.path.isdir (d): shutil.rmtree (d)
	n = {"dynarmic": 0, "boost": 0}
	for f in sorted (files):
		if f.startswith (dyn + os.sep): dst, k = os.path.join (out_dyn, os.path.relpath (f, dyn)), "dynarmic"
		elif f.startswith (boost + os.sep): dst, k = os.path.join (out_boost, os.path.relpath (f, boost)), "boost"
		else: continue						# (the toolchain's and Onyx's own headers)
		os.makedirs (os.path.dirname (dst), exist_ok = True)
		shutil.copyfile (f, dst); n[k] += 1
	# the licences, and what says where each part comes from
	for src, dst in (("LICENSE.txt", "LICENSE.txt"), ("README.md", "README.md"),
			 ("externals/fmt/LICENSE", "externals/fmt/LICENSE"), ("externals/mcl/LICENSE", "externals/mcl/LICENSE"),
			 ("externals/oaknut/LICENSE", "externals/oaknut/LICENSE"), ("externals/robin-map/LICENSE", "externals/robin-map/LICENSE")):
		s = os.path.join (dyn, src)
		if not os.path.isfile (s): sys.exit ("missing " + s)
		os.makedirs (os.path.dirname (os.path.join (out_dyn, dst)), exist_ok = True)
		shutil.copyfile (s, os.path.join (out_dyn, dst))
	shutil.copyfile (os.path.join (boost, "LICENSE_1_0.txt"), os.path.join (out_boost, "LICENSE_1_0.txt"))
	def rev (path):
		r = subprocess.run (["git", "-C", path, "rev-parse", "HEAD"], capture_output = True, text = True)
		return r.stdout.strip () or "?"
	with open (os.path.join (out_dyn, "ONYX-VENDOR.txt"), "w", newline = "\n") as o:
		o.write ("Dynarmic for Onyx's 3DS emulator: the part Onyx builds (AArch64 back end, A32 front end).\n"
			 "Copied by tools/n3ds/vendor_dynarmic.py -- do not edit by hand; Onyx's own pieces are in user/Libs/dynarmic.\n\n"
			 "dynarmic   https://github.com/azahar-emu/dynarmic  %s  (0BSD)\n"
			 "fmt        https://github.com/fmtlib/fmt            %s  (MIT)\n"
			 "mcl        https://github.com/azahar-emu/mcl        %s  (MIT)\n"
			 "oaknut     https://github.com/merryhime/oaknut      %s  (MIT)\n"
			 "robin-map  https://github.com/Tessil/robin-map      %s  (MIT)\n"
			 % (rev (dyn), rev (os.path.join (dyn, "externals/fmt")), rev (os.path.join (dyn, "externals/mcl")),
			    rev (os.path.join (dyn, "externals/oaknut")), rev (os.path.join (dyn, "externals/robin-map"))))
	with open (os.path.join (out_boost, "ONYX-VENDOR.txt"), "w", newline = "\n") as o:
		o.write ("Boost 1.86.0 (Boost Software License 1.0): only the headers Dynarmic's build includes\n"
			 "(boost/icl, boost/variant and what they pull). Copied by tools/n3ds/vendor_dynarmic.py.\n")
	print ("dynarmic: %d files -> %s" % (n["dynarmic"], out_dyn))
	print ("boost: %d files -> %s" % (n["boost"], out_boost))

if __name__ == "__main__":
	main ()
