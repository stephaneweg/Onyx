#!/bin/sh
# run_tools_test.sh -- the text tools of /bin (grep, wc, head, tail, sed, ed, sort, uniq, cut, tr,
# tee, nl, find, date, sleep, hexdump, diff) on the PC: the very sources of user/bin, built with
# -DTOOL_HOST (tool.h's libc back end) and the address / undefined sanitizers, each run on small
# inputs and its output compared with what is expected. docs/04 *The /bin tools*.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-${TMPDIR:-/tmp}/onyx_toolstest}
rm -rf "$OUT"; mkdir -p "$OUT/w/sub/deep"
export ASAN_OPTIONS=detect_leaks=0		# (a tool's memory goes with its process)
TOOLS="grep wc head tail sed ed sort uniq cut tr tee nl find date sleep hexdump diff"
for t in $TOOLS; do
	gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -DTOOL_HOST \
		"$ROOT/user/bin/$t.c" -o "$OUT/$t" || { echo "BUILD FAILED: $t"; exit 1; }
done
PATH="$OUT:$PATH"; export PATH
cd "$OUT/w" || exit 1

checks=0; fails=0
# t <name> <expected output> <command line (sh)>
t () {
	checks=$((checks + 1))
	got=$(sh -c "$3" 2>&1; echo "[$?]")
	if [ "$got" != "$2" ]; then
		fails=$((fails + 1))
		printf 'FAIL %s\n--- got\n%s\n--- want\n%s\n' "$1" "$got" "$2"
	fi
}

printf 'banana\napple\nCherry\napple\n10 ten\n9 nine\n' > f
printf 'one\ttwo\tthree\nuno\tdos\ttres\nplain\n' > tab
printf 'alpha\nbeta\ngamma\ndelta\nepsilon\n' > g
printf 'no newline' > nonl
seq 1 25 > nums

# ---- grep
t "grep plain"		"apple
apple
[0]"	"grep apple f"
t "grep -n -i"		"3:Cherry
[0]"	"grep -n -i cherry f"
t "grep -v -c"		"4
[0]"	"grep -v -c apple f"
t "grep regex"		"banana
apple
apple
[0]"	"grep '^[ab].*[ae]\$' f"
t "grep \\+ and group"	"banana
[0]"	"grep '\\(an\\)\\1' f"
t "grep none"		"[1]"	"grep zzz f"
t "grep -q"		"[0]"	"grep -q nine f"
t "grep two files"	"f:9 nine
g:beta
[0]"	"grep '^9' f g; grep '^bet' f g"
t "grep stdin"		"beta
delta
[0]"	"cat g | grep 'e.t*a\$'"
t "grep -c two files"	"f:2
g:0
[0]"	"grep -c apple f g"
t "grep -F"		"a.c
[0]"	"printf 'abc\na.c\n' | grep -F a.c"
t "grep bad file"	"grep: cannot open nofile
[2]"	"grep x nofile"

# ---- wc
t "wc"			"6 8 40 f
[0]"	"wc f"
t "wc -l stdin"		"5
[0]"	"wc -l < g"
t "wc -c no newline"	"10 nonl
[0]"	"wc -c nonl"
t "wc total"		"6 f
5 g
11 total
[0]"	"wc -l f g"

# ---- head / tail
t "head -n"		"alpha
beta
[0]"	"head -n 2 g"
t "head -3"		"1
2
3
[0]"	"head -3 nums"
t "head default"	"10
[0]"	"head nums | wc -l"
t "head -c"		"alp[0]"	"head -c 3 g"
t "tail -n"		"delta
epsilon
[0]"	"tail -n 2 g"
t "tail default"	"16
[0]"	"tail nums | head -1"
t "tail +N"		"24
25
[0]"	"tail -n +24 nums"
t "tail no newline"	"no newline[0]"	"tail -1 nonl"
t "tail stdin -c"	"lon
[0]"	"tail -c 4 g"
t "tail more than there is" "alpha
beta
gamma
delta
epsilon
[0]"	"tail -n 99 g"

# ---- sed
t "sed s"		"bXnana
[0]"	"sed 's/a/X/' f | head -1"
t "sed s g"		"bXnXnX
[0]"	"sed 's/a/X/g' f | head -1"
t "sed s 2"		"banXna
[0]"	"sed 's/a/X/2' f | head -1"
t "sed groups"		"ten=10
nine=9
[0]"	"sed -n 's/^\\([0-9]*\\) \\(.*\\)/\\2=\\1/p' f"
t "sed &"		"[alpha]
[0]"	"sed -n '1s/.*/[&]/p' g"
t "sed -n p range"	"beta
gamma
[0]"	"sed -n '2,3p' g"
t "sed regex range"	"beta
gamma
delta
[0]"	"sed -n '/^b/,/^d/p' g"
t "sed d"		"alpha
epsilon
[0]"	"sed '2,4d' g"
t "sed \$ and !"	"epsilon
[0]"	"sed '\$!d' g"
t "sed q"		"alpha
beta
[0]"	"sed 2q g"
t "sed ="		"5
[0]"	"sed -n '\$=' g"
t "sed y"		"ALphA
[0]"	"sed -n '1y/al/AL/;1p' g | sed 's/Lph/lph/' | sed 's/Alp/ALp/'"
t "sed a i c"		"top
alpha
after
BETA
gamma
[0]"	"sed -e '1i top' -e '1a after' -e '2c BETA' g | head -5"
t "sed two commands"	"ALPHA
[0]"	"sed -n 's/alpha/ALPHA/;1p' g"
t "sed other delimiter"	"/usr/local
[0]"	"echo /usr/bin | sed 's,/bin,/local,'"
t "sed empty match g"	"XaXcX
[0]"	"echo abc | sed 's/b*/X/g'"
t "sed no newline kept"	"nX newline[0]"	"sed 's/o/X/' nonl"
t "sed newline in repl"	"a
b
[0]"	"echo a,b | sed 's/,/\\n/'"
t "sed i flag"		"X
[0]"	"echo CHERRY | sed 's/cherry/X/i'"
cp g g2
t "sed -i"		"alpha
BETA
[0]"	"sed -i 's/beta/BETA/' g2 && head -2 g2"
t "sed bad command"	"sed: unknown command k
[2]"	"sed k g"
t "sed unterminated"	"sed: unterminated command a
[2]"	"sed 's/a' g"

# ---- ed
t "ed print"		"31
beta
gamma
[0]"	"printf '2,3p\nq\n' | ed g"
t "ed n and ="		"1	alpha
5
[0]"	"printf '1n\n=\nq\n' | ed g | tail -2"
t "ed append write"	"31
40
alpha
inserted
beta
[0]"	"cp g e1; printf '1a\ninserted\n.\nw\nq\n' | ed e1; head -3 e1"
t "ed s and print"	"31
bEta
[0]"	"printf '2s/e/E/p\nQ\n' | ed g"
t "ed s g on range"	"31
Alpha
betA
gAmmA
[0]"	"printf '1s/a/A/\n2,3s/a/A/g\n1,3p\nQ\n' | ed g"
t "ed delete, dirty q"	"31
?
[0]"	"printf '2d\nq\nq\n' | ed g"
t "ed search"		"31
gamma
delta
beta
[0]"	"printf '/mm/\n/el/\n?et?\nQ\n' | ed g"
t "ed g"		"31
alpha
beta
gamma
delta
[0]"	"printf 'g/a\$/p\nQ\n' | ed g"
t "ed g with s"		"31
alpha
beta
<gamma>
[0]"	"printf 'g/mm/s/.*/<&>/\n1,3p\nQ\n' | ed g"
t "ed v d"		"31
gamma
[0]"	"printf 'v/mm/d\n,p\nQ\n' | ed g"
t "ed move copy join"	"31
beta
gamma
alpha
alpha
deltaepsilon
[0]"	"printf '1m3\n3t3\n5,6j\n,p\nQ\n' | ed g"
t "ed undo"		"31
alpha
beta
[0]"	"printf '1,3d\nu\n1,2p\nQ\n' | ed g"
t "ed change insert"	"31
new
X
gamma
[0]"	"printf '1,2c\nX\n.\n1i\nnew\n.\n1,3p\nQ\n' | ed g"
t "ed errors"		"31
?
invalid address
?
[0]"	"printf '99p\nh\nxyz\nQ\n' | ed g"
t "ed new file"		"newf: a new file
4
a
[0]"	"printf 'a\na\nb\n.\nw\nq\n' | ed newf; head -1 newf"
t "ed address alone, next" "31
gamma
delta
epsilon
[0]"	"printf '3\n\n+\nQ\n' | ed g"
t "ed l, marks, r"	"31
a\\tb\$
beta
[0]"	"printf '1c\na\tb\n.\n1l\n2ka\n1d\n'\"'\"'ap\nQ\n' | ed g"
t "ed -p prompt"	"31
*alpha
*[0]"	"printf '1p\nQ\n' | ed -p '*' g"

# ---- sort / uniq
t "sort"		"10 ten
9 nine
Cherry
apple
apple
banana
[0]"	"sort f"
t "sort -f -u -r"	"Cherry
banana
apple
9 nine
10 ten
[0]"	"sort -f -u -r f"
t "sort -n"		"Cherry
9 nine
10 ten
[0]"	"sort -n f | sed -n '1p;5,6p'"
t "sort two files"	"11
[0]"	"sort f g | wc -l"
t "uniq"		"apple
banana
apple
[0]"	"printf 'apple\napple\nbanana\napple\n' | uniq"
t "uniq -c"		"      2 apple
      1 banana
[0]"	"printf 'apple\napple\nbanana\n' | uniq -c"
t "uniq -d -i"		"apple
[0]"	"printf 'apple\nAPPLE\nbanana\n' | uniq -d -i"
t "uniq -u"		"banana
[0]"	"printf 'apple\napple\nbanana\n' | uniq -u"

# ---- cut / tr / nl / tee
t "cut -f"		"two
dos
plain
[0]"	"cut -f 2 tab"
t "cut -f list -s"	"one	three
uno	tres
[0]"	"cut -s -f 1,3 tab"
t "cut -d"		"b:c
[0]"	"echo a:b:c | cut -d : -f 2-"
t "cut -c"		"alp
bet
[0]"	"cut -c -3 g | head -2"
t "cut -c ranges"	"ah
[0]"	"cut -c 1,4-4 g | head -1"
t "tr upper"		"ALPHA
[0]"	"head -1 g | tr a-z A-Z"
t "tr -d"		"lph
[0]"	"head -1 g | tr -d a"
t "tr -s"		"a b
[0]"	"echo 'a    b' | tr -s ' '"
t "tr escapes"		"a b [0]"	"printf 'a\nb\n' | tr '\\n' ' '"
t "nl"			"     1	a
     2	b
[0]"	"printf 'a\n\nb\n' | nl | grep -v '^\$'"
t "nl -b a"		"8
[0]"	"printf 'a\n\nb\n' | nl -b a | sed -n 2p | wc -c"
t "tee"			"x
x
x
[0]"	"echo x | tee t1 t2; cat t1 t2"
t "tee -a"		"x
y
[0]"	"echo y | tee -a t1 >/dev/null; cat t1"

# ---- find
touch sub/a.txt sub/deep/b.TXT sub/deep/c.dat
t "find all"		"sub
sub/a.txt
sub/deep
sub/deep/b.TXT
sub/deep/c.dat
[0]"	"find sub | sort"
t "find -name"		"sub/a.txt
sub/deep/b.TXT
[0]"	"find sub -name '*.txt' | sort"
t "find -type d"	"sub
sub/deep
[0]"	"find sub -type d | sort"
t "find -maxdepth"	"sub
sub/a.txt
sub/deep
[0]"	"find sub -maxdepth 1 | sort"
t "find name ?"		"sub/deep/c.dat
[0]"	"find sub -type f -name '?.d*'"
t "find missing"	"find: cannot open nodir
[1]"	"find nodir -type f"
t "find a file"		"g
[0]"	"find g"
t "date format"		"ok
[0]"	"date +%Y-%m-%d | grep -q '^20[0-9][0-9]-[01][0-9]-[0-3][0-9]\$' && echo ok"
t "date default"	"ok
[0]"	"date | grep -q '^20[0-9][0-9]-[0-9][0-9]-[0-9][0-9] [0-9][0-9]:[0-9][0-9]:[0-9][0-9]\$' && echo ok"
t "date literal"	"a%b
[0]"	"date +a%%b"
t "sleep"		"[0]"	"sleep 0.05"
t "sleep usage"		"usage: sleep <seconds>
[2]"	"sleep abc"
t "hexdump"		"00000000  61 6c 70 68 61 0a 62 65  74 61 0a 67 61 6d 6d 61  |alpha.beta.gamma|
[0]"	"hexdump g | head -1"
t "hexdump -s -n"	"00000006  62 65 74 61                                       |beta|
0000000a
[0]"	"hexdump -s 6 -n 4 g"
printf 'alpha\nBETA\ngamma\nepsilon\nzeta\n' > h
t "diff"		"2c2
< beta
---
> BETA
4d3
< delta
5a5
> zeta
[1]"	"diff g h"
t "diff same"		"[0]"	"diff g g"
t "diff -q"		"Files g and h differ
[1]"	"diff -q g h"
t "diff add at start"	"0a1
> first
[1]"	"printf 'first\nalpha\nbeta\ngamma\ndelta\nepsilon\n' > h2; diff g h2"

echo "tools: $checks checks, $fails failed"
[ "$fails" = 0 ] && echo "all passed"
[ "$fails" = 0 ]
