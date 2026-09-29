#!/bin/sh
# tools/tests/koton/ai_run.sh -- Koton's AI composition (engine/ai.h: the prompts, the replies placed on a
# project) and the /bin/llm protocol (request / provider answers / result line) on the PC under ASan / UBSan /
# LSan; then the same sources compiled for the Pi, and /bin/llm built (a newlib + mbedTLS program).
# When Koton's C# sources are there (KOTON_SRC, default /home/user/stephaneweg/musictracker), every line of
# the generated prompts is also looked up in them: the French prompts must stay Koton's, verbatim.
#   sh tools/tests/koton/ai_run.sh
set -e
cd "$(dirname "$0")/../../.."
K=user/Apps/koton
OUT=${TMPDIR:-/tmp}/onyx_koton_ai
DUMP=${TMPDIR:-/tmp}/onyx_koton_ai_prompts
mkdir -p "$DUMP"
g++ -std=gnu++17 -O1 -g -Wall -Wextra -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	-I $K -I user -o "$OUT" tools/tests/koton/ai_test.cpp $K/engine/*.cpp $K/synth/*.cpp
"$OUT" --dump "$DUMP"

# the prompts against Koton's sources (C# literals: \" and "" unescaped); a line not found is printed
KOTON_SRC=${KOTON_SRC:-/home/user/stephaneweg/musictracker}
if [ -d "$KOTON_SRC/MusicTracker/Engine/AI" ] && command -v python3 >/dev/null 2>&1; then
	python3 - "$KOTON_SRC" "$DUMP" <<'EOF'
import sys, os, re, glob
src, dump = sys.argv[1], sys.argv[2]
# the C# string literals, in file order (regular, @verbatim, $interpolated: a hole becomes \x00)
lits = []
for f in sorted(glob.glob(src + "/MusicTracker/Engine/AI/*.cs")):
    t = open(f, encoding="utf-8").read()
    i = 0
    while i < len(t):
        if t.startswith("//", i):
            i = t.find("\n", i); i = len(t) if i < 0 else i; continue
        m = re.match(r'(\$@|@\$|@|\$)?"', t[i:])
        if not m or (i > 0 and (t[i-1].isalnum() or t[i-1] == "'")):
            i += 1; continue
        pre = m.group(1) or ""; i += len(m.group(0)); out = []; depth = 0
        while i < len(t):
            c = t[i]
            if "$" in pre and c == "{" and t[i+1:i+2] != "{":
                depth = 1; i += 1
                while i < len(t) and depth: depth += (t[i] == "{") - (t[i] == "}"); i += 1
                out.append("\x00"); continue
            if "@" in pre:
                if c == '"' and t[i+1:i+2] == '"': out.append('"'); i += 2; continue
                if c == '"': i += 1; break
            else:
                if c == "\\": out.append({"n": "\n", '"': '"', "\\": "\\"}.get(t[i+1], t[i+1])); i += 2; continue
                if c == '"': i += 1; break
            out.append(c); i += 1
        lits.append("".join(out))
blob = "\x01".join(lits)		# \x01 between two literals, \x00 for a hole: a match stays inside one literal
# the interpolated values: Koton's name tables (from its sources), numbers and note lists, the texts ai_test
# puts in the requests, the key / track names of its projects
dyn = []
for fn, names in (("Engine/Flow/PatternGenerator.cs", ("QualityNames", "StyleNames")), ("Engine/Timeline/MelodicLineEngine.cs", ("ContourNames", "AnchorNames"))):
    t = open(src + "/MusicTracker/" + fn, encoding="utf-8").read()
    for n in names:
        m = re.search(n + r"\s*=\s*\{(.*?)\};", t, re.S)
        if m:
            for x in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)):
                dyn.append(x); dyn.append(x.split(" ")[0])
dyn += ["bossa nova", "une soirée à Rio, mélancolique", "valse jazz", "afro-cubain", "une flûte qui répond au lead", "brosses",
        "samba", "Ré mineur", "4/4 swing", "une phrase de guitare", "gnawa", "transe", "D minor (Aeolian)", "C major", "Si♭ majeur",
        "ANGLAIS", "FRANÇAIS", '"Flute", "Cello"', "Accompaniment", "Lead", "Accords", "Pad", "Drums", "majeur", "mineur"]
dyn = sorted(set(d for d in dyn if len(d) >= 2), key=len, reverse=True)
num = re.compile(r"\d+(?:\.\d+)?(?:@\d+(?:\.\d+)?x\d+(?:\.\d+)?)?")
bad = 0
for f in sorted(glob.glob(dump + "/*.txt")):
    for line in open(f, encoding="utf-8").read().split("\n"):
        if line in blob:
            continue
        for d in dyn:
            line = line.replace(d, "\x00")
        line = num.sub("\x00", line)
        for piece in line.split("\x00"):
            if not re.search(r"[A-Za-zÀ-ÿ]{2}", piece) or piece in blob:
                continue
            # a piece spanning two C# literals (an optional sentence, a ternary): made of long runs of the sources
            k, ok = 0, True
            while k < len(piece):
                lo, hi = 0, len(piece) - k
                while lo < hi:
                    mid = (lo + hi + 1) // 2
                    if piece[k:k+mid] in blob: lo = mid
                    else: hi = mid - 1
                # a run that ends a literal or a hole (the next literal follows), or the piece's end
                L = lo
                def ends(c):
                    return (len(c) >= 3 or "\x01" + c + "\x01" in blob) and (c + "\x01" in blob or c + "\x00" in blob)
                while L > 0 and not (k + L == len(piece) or ends (piece[k:k+L])):
                    L -= 1
                if L == 0: ok = False; break
                k += L
            if not ok:
                bad += 1
                print("  not in Koton's sources (%s): [%s]" % (os.path.basename(f), piece[:200]))
print("prompts vs Koton's C#: %s" % ("every literal fragment found" if bad == 0 else "%d fragment(s) differ" % bad))
EOF
fi

# the Pi: the AI sources with the app's flags, and /bin/llm
A=${ARMGCC:-/opt/arm/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin}
if [ -x "$A/aarch64-none-elf-g++" ]; then
	for f in $K/engine/ai*.cpp; do
		$A/aarch64-none-elf-g++ -mcpu=cortex-a72 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
			-ffunction-sections -fdata-sections -Wall -Wextra -I $K -I user -I kernel/include -c "$f" -o /dev/null
	done
	PATH="$A:$PATH" make --no-print-directory -C user/bin llm.elf >/dev/null
	echo "aarch64: ai*.cpp compile, user/bin/llm.elf builds: OK"
fi
