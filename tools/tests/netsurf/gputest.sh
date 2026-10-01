#!/bin/sh
# tools/tests/netsurf/gputest.sh -- GPU compositing (docs/06 §25): the composited frames against the CPU
# painting of the same NetSurf (gpu_compositing off: NS_GPU=0), pixel by pixel, on the PC bench:
#
#   1. pages with layers (opacity, transforms, filters, blend modes, hovers, transitions and animations
#      stopped), shot as loaded and after a scroll of a few wheel notches;
#   2. hovers that change only a layer's transform / opacity (css-fxlayer.html: composite-only frames);
#   3. a long page scrolled notch by notch against the same page scrolled in one jump (the band);
#   4. with the software V3D linked in (host.mk SOFTGPU=1, GPC_SOFTGPU=1): gpucomp's GPU path -- the
#      kernel's own fragment shader in the QPU simulator, the kernel's target packets -- and the
#      self-test's fallback when the GPU "hangs" (GPC_SOFTGPU=hang);
#   5. the scroll frames' cost (NS_PERF: "ONYX-SCROLL <us>"), CPU painting against composited.
#
# A picture passes when at most 0.1 % of its pixels differ by more than 16 (a channel) -- rounding
# of the layers' filtering, as fxtest.sh allows against Chromium. Pictures and logs: $OUT/gpu/.
#
#   OUT=/tmp/nsbench sh tools/tests/netsurf/gputest.sh
cd "$(dirname "$0")/../../.."
T=tools/tests/netsurf
OUT=${OUT:-/tmp/nsbench}
G=$OUT/gpu
mkdir -p "$G"
make -f $T/host.mk OUT="$OUT/build" SOFTGPU=1 -j"$(nproc)" >"$G/build.log" 2>&1 || { echo "build failed: $G/build.log"; exit 1; }
fail=0

W=$(i=0; while [ $i -lt 30 ]; do printf 'wait;'; i=$((i + 1)); done)
# shoot <page> <steps (@W: 30 turns)> <out.png> [WxH] -- env passes through
shoot() {
	url="file://$(realpath "$1")"
	steps=$(echo "$2" | sed "s/@W/$W/g")
	SIM_SCREEN=${4:-1100x700} SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="$url" SIM="${steps}dump $3.elsm;exit" \
		timeout ${TMO:-300} "$OUT/build/netsurf" >"$3.log" 2>&1
	python3 tools/tests/desktop_sim/shot.py "$3.elsm" "$3" >/dev/null
}
# same <a.png> <b.png> <what>
same() {
	python3 - "$1" "$2" "$3" <<'EOF'
import sys
import numpy as np
from PIL import Image
a = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(np.int32)
b = np.asarray(Image.open(sys.argv[2]).convert("RGB")).astype(np.int32)
if a.shape != b.shape:
    print("  FAIL  %s: sizes differ" % sys.argv[3]); sys.exit(1)
d = np.abs(a - b).max(axis=2)
n = int((d > 16).sum())
ok = n <= d.size // 1000
print("  %s  %-44s max %3d, %5d pixels off by more than 16" % ("ok  " if ok else "FAIL", sys.argv[3], d.max(), n))
sys.exit(0 if ok else 1)
EOF
}
SCROLL="wheel 400 400 -1;wait;wait;wait;wheel 400 400 -1;wait;wait;wait;wheel 400 400 -1;@W"

echo "---- 1. layers: composited = CPU painting"
for p in css-opacity css-transform css-filter css-fxhover css-transition css-animation js-animate gpu-scroll; do
	for s in load scroll; do
		steps="@W@W"
		[ "$s" = scroll ] && steps="@W@W$SCROLL"
		NS_NO_ANIM=1 NS_GPU=0 shoot $T/pages/$p.html "$steps" "$G/$p-$s-cpu.png"
		NS_NO_ANIM=1 NS_GPU=1 shoot $T/pages/$p.html "$steps" "$G/$p-$s-gpu.png"
		same "$G/$p-$s-cpu.png" "$G/$p-$s-gpu.png" "$p ($s)" || fail=1
	done
done

echo "---- 2. hovers of a layer's transform / opacity (composite-only)"
for step in 1 2 3; do
	moves="move 150 120;@W"
	[ "$step" -ge 2 ] && moves="${moves}move 350 120;@W"
	[ "$step" -ge 3 ] && moves="${moves}move 560 120;@W"
	NS_GPU=0 shoot $T/pages/css-fxlayer.html "@W$moves" "$G/fxlayer-$step-cpu.png" 900x500
	NS_PERF=1 NS_GPU=1 shoot $T/pages/css-fxlayer.html "@W$moves" "$G/fxlayer-$step-gpu.png" 900x500
	same "$G/fxlayer-$step-cpu.png" "$G/fxlayer-$step-gpu.png" "css-fxlayer hover $step" || fail=1
done

# animated transform / opacity: most frames a composite (nothing painted)
NS_PERF=1 NS_GPU=1 shoot $T/pages/anim-layers.html "@W@W@W@W@W@W" "$G/anim-layers.png" 900x600
python3 - "$G/anim-layers.png.log" <<'EOF' || fail=1
import re, sys
m = re.findall(r'ONYX-COMP (\d+) frames: (\d+) painted .*?, (\d+) composite only', open(sys.argv[1]).read())
if not m:
    print("  FAIL  anim-layers: no frames counted"); sys.exit(1)
n, painted, comp = map(int, m[-1])
ok = comp >= n * 3 // 4
print("  %s  %-44s %d frames, %d composite only, %d painted" % ("ok  " if ok else "FAIL", "anim-layers: composite-only frames", n, comp, painted))
sys.exit(0 if ok else 1)
EOF

echo "---- 3. the band: scrolled notch by notch = scrolled in one jump"
N="wheel 400 400 -1;wait;wait;wait;"
NS_GPU=1 shoot $T/pages/gpu-scroll.html "@W@W$N$N$N$N$N$N$N$N$N$N$N$N$N$N$N@W" "$G/band-notches.png" 1280x800
NS_GPU=1 shoot $T/pages/gpu-scroll.html "@W@Wwheel 400 400 -15;@W" "$G/band-jump.png" 1280x800
NS_GPU=0 shoot $T/pages/gpu-scroll.html "@W@W$N$N$N$N$N$N$N$N$N$N$N$N$N$N$N@W" "$G/band-cpu.png" 1280x800
same "$G/band-notches.png" "$G/band-jump.png" "gpu-scroll: 15 notches = a jump of 15" || fail=1
same "$G/band-notches.png" "$G/band-cpu.png" "gpu-scroll: 15 notches, composited = CPU" || fail=1

echo "---- 3b. a new page: the page before's layers gone; a fragment let go of once scrolled"
# (gpu-nav-a.html: an animated layer and a link to gpu-nav-b.html, clicked)
NAV="move 500 450;wait;down 500 450;wait;wait;up 500 450;@W@W@W"
NS_GPU=1 shoot $T/pages/gpu-nav-a.html "@W@W$NAV" "$G/nav-gpu.png"
NS_GPU=0 shoot $T/pages/gpu-nav-a.html "@W@W$NAV" "$G/nav-cpu.png"
same "$G/nav-gpu.png" "$G/nav-cpu.png" "gpu-nav: the animated layer of the page before gone" || fail=1
# (nav-fragment.html#target, scrolled to the top while late content still reflows the page)
frag() {
	steps=$(echo "$2" | sed "s/@W/$W/g")
	SIM_SCREEN=900x600 SIM_SLEEP=1 SIM_POS=0,0 SIM_ARGS="file://$(realpath $T/pages/nav-fragment.html)$1" \
		SIM="${steps}dump $3.elsm;exit" timeout ${TMO:-300} "$OUT/build/netsurf" >"$3.log" 2>&1
	python3 tools/tests/desktop_sim/shot.py "$3.elsm" "$3" >/dev/null
}
NS_GPU=1 frag "#target" "@Wwheel 400 300 60;@W@W@W@W@W" "$G/frag-up.png"
NS_GPU=1 frag "" "@W@W@W@W@W@W" "$G/frag-top.png"
same "$G/frag-up.png" "$G/frag-top.png" "nav-fragment: scrolled up from #target, stays up" || fail=1

echo "---- 4. the software V3D (gpucomp's GPU path)"
GPC_SOFTGPU=1 NS_GPU=1 TMO=900 shoot $T/pages/css-transform.html "@W" "$G/softgpu.png" 520x420
NS_GPU=0 shoot $T/pages/css-transform.html "@W" "$G/softgpu-cpu.png" 520x420
if grep -q "compositing on: GPU" "$G/softgpu.png.log"; then
	grep "self-test" "$G/softgpu.png.log" | sed 's/^/  /'
	same "$G/softgpu-cpu.png" "$G/softgpu.png" "css-transform on the software V3D" || fail=1
else
	echo "  FAIL  the software V3D was not used:"; grep compositing "$G/softgpu.png.log"; fail=1
fi
GPC_SOFTGPU=1 NS_GPU=1 TMO=900 shoot $T/pages/css-fxlayer.html "@Wmove 150 120;@Wmove 350 120;@W" "$G/softgpu-hover.png" 900x400
NS_GPU=0 shoot $T/pages/css-fxlayer.html "@Wmove 150 120;@Wmove 350 120;@W" "$G/softgpu-hover-cpu.png" 900x400
same "$G/softgpu-hover-cpu.png" "$G/softgpu-hover.png" "css-fxlayer hovers on the software V3D" || fail=1
GPC_SOFTGPU=hang NS_GPU=1 TMO=900 shoot $T/pages/css-transform.html "@W" "$G/softgpu-hang.png" 520x420
if grep -q "failed its self-test" "$G/softgpu-hang.png.log"; then
	echo "  ok    the GPU hanging in the self-test: $(grep 'failed its' "$G/softgpu-hang.png.log" | sed 's/netsurf: compositing: //')"
	same "$G/softgpu-cpu.png" "$G/softgpu-hang.png" "css-transform after the fallback" || fail=1
else
	echo "  FAIL  no fallback when the GPU hangs"; fail=1
fi

echo "---- 5. scroll frames (the PC; NS_PERF)"
for g in 0 1; do
	NS_PERF=1 NS_GPU=$g shoot $T/pages/gpu-scroll.html "@W@W$N$N$N$N$N$N$N$N$N$N$N$N$N$N$N$N$N$N$N$N@W" "$G/perf-$g.png" 1280x800
done
python3 - "$G/perf-0.png.log" "$G/perf-1.png.log" <<'EOF'
import re, statistics, sys
for name, path in (("CPU painting", sys.argv[1]), ("composited  ", sys.argv[2])):
    v = [int(x) for x in re.findall(r'ONYX-SCROLL (\d+) us', open(path, errors='replace').read())]
    if v:
        print("  %s  %2d scroll frames: mean %5.2f ms, median %5.2f ms, max %5.2f ms" % (name, len(v),
              statistics.mean(v) / 1000, statistics.median(v) / 1000, max(v) / 1000))
EOF

[ "$fail" = 0 ] && echo "all passed" || echo "FAILED (pictures: $G/)"
exit "$fail"
