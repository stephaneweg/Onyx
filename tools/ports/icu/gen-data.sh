#!/bin/sh
# gen-data.sh -- makes Onyx's ICU data file, third_party/icu-78.3/source/data/in/icudt78l.dat (about 15 MB,
# against 32 MB for ICU's full data), from ICU's data sources with the filter tools/ports/icu/data-filter.json:
#
#   1. fetches ICU 78.3's source release (pinned, sha256 checked) and its data sources (the git tag
#      release-78.3 of unicode-org/icu, sparse: icu4c/source/data only; the commit id checked) -- ICU's
#      source tarball ships only a prebuilt full .dat, the sources are in the repository (or the release's
#      -data.zip);
#   2. a host build of ICU (its own configure, the PC's compiler) with ICU_DATA_FILTER_FILE: the tools
#      (genrb, genbrk, gencnval, makeconv, gendict, gencfu, icupkg, pkgdata...) build the filtered data;
#   3. copies the packaged icudt78l.dat into third_party (tools/ports/icu/build.sh links it in).
#
# Run it after changing the filter (then commit the new .dat). Needs a host C++ compiler, make, python3,
# curl, git; about 3 minutes on 4 cores, 400 MB in $WORK (default ~/.cache/onyx-icu).
#
# What the filter keeps (WebKit's needs; docs/POSIX-PLAN.md "Ports for WebKit"): the locales of 42
# languages (all their regional variants) for JSC's Intl (dates, numbers, units, currencies, plurals,
# list / relative-time formats, display names, time-zone names) and Intl.Collator / WebCore's usearch;
# the break iterators (word, line, sentence, grapheme) with the Thai / Lao / Khmer / Burmese / Chinese-
# Japanese dictionaries and the Japanese phrase model (word-break: auto-phrase); the converters of WebKit's
# TextCodecICU and the CJK ones (Shift_JIS, EUC-JP, ISO-2022-JP, GBK, GB18030, Big5, EUC-KR...); the
# normalization data (IDNA: UTS 46), the properties, the time-zone rules. Out: transliteration, spell-out
# rules (rbnf), confusables (no uspoof in WebKit), stringprep (IDNA is UTS 46), character names, the LSTM
# models, the full Unihan collation tables (the implicit Han order instead).
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
set -e
ONYX=$(cd "$(dirname "$0")/../../.." && pwd)
: "${WORK:=$HOME/.cache/onyx-icu}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
FILTER=$ONYX/tools/ports/icu/data-filter.json
OUT=$ONYX/third_party/icu-78.3/source/data/in/icudt78l.dat

# ICU 78.3's source release, as Debian's orig tarball (the upstream icu4c 78.3 release, not repacked)
TARBALL_URL=https://deb.debian.org/debian/pool/main/i/icu/icu_78.3.orig.tar.gz
TARBALL_SHA256=3a2e7a47604ba702f345878308e6fefeca612ee895cf4a5f222e7955fabfe0c0
# the data sources: the release tag in ICU's repository
REPO=https://github.com/unicode-org/icu
TAG=release-78.3
TAG_COMMIT=21d1eb0f306e1141c10931e914dfc038c06121da

mkdir -p "$WORK"
cd "$WORK"
if [ ! -f icu_78.3.orig.tar.gz ]; then
	echo "gen-data: fetching $TARBALL_URL"
	curl -fsSL -o icu_78.3.orig.tar.gz.part "$TARBALL_URL"
	mv icu_78.3.orig.tar.gz.part icu_78.3.orig.tar.gz
fi
echo "$TARBALL_SHA256  icu_78.3.orig.tar.gz" | sha256sum -c - >/dev/null || { echo "gen-data: sha256 mismatch" >&2; exit 1; }
if [ ! -d data-src/.git ]; then
	echo "gen-data: fetching the data sources ($REPO, $TAG, icu4c/source/data)"
	rm -rf data-src
	GIT_LFS_SKIP_SMUDGE=1 git clone -q --depth 1 --branch "$TAG" --filter=blob:none --sparse "$REPO" data-src
	git -C data-src sparse-checkout set icu4c/source/data
fi
[ "$(git -C data-src rev-parse HEAD)" = "$TAG_COMMIT" ] || { echo "gen-data: $TAG is not $TAG_COMMIT" >&2; exit 1; }

# the tree: the release with the data sources in place of its prebuilt full .dat
rm -rf tree host
mkdir tree
tar xzf icu_78.3.orig.tar.gz -C tree
rm -f tree/icu/source/data/in/icudt78l.dat
cp -a data-src/icu4c/source/data/. tree/icu/source/data/

echo "gen-data: host build of ICU's tools and the filtered data"
mkdir host
cd host
ICU_DATA_FILTER_FILE=$FILTER "$WORK/tree/icu/source/configure" --disable-tests --disable-samples --disable-extras \
	--disable-icuio --disable-layoutex --enable-static --disable-shared --with-data-packaging=archive \
	>configure.log 2>&1 || { tail -30 configure.log; exit 1; }
make -j "$JOBS" >make.log 2>&1 || { tail -40 make.log; exit 1; }
cp data/out/icudt78l.dat "$OUT"
ls -l "$OUT" | awk '{print "gen-data: " $5 " bytes  " $NF}'
echo "gen-data: the converters: $(ls data/out/build/icudt78l/*.cnv | wc -l), the locales (main): $(ls data/out/build/icudt78l/*.res | wc -l) resources"
