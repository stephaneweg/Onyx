# revision.sh -- the WebKit revision the Onyx port is pinned to (sourced by tools/webkit/*.sh).
# WebKit main of 2026-10-02: its Source/ThirdParty/skia is Skia m154 (588b550a), the copy ported
# into the sysroot (third_party/skia-m154/README.onyx), so WebCore will build against the same Skia.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
WEBKIT_REPO=${WEBKIT_REPO:-https://github.com/WebKit/WebKit.git}
WEBKIT_REVISION=b8a7a626127c0010a557c9d6466fefd38d9477c1
