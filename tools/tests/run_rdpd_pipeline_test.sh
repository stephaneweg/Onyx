#!/bin/sh
# Host test of rdpd's loss tolerance and its compatibility (the pipelined rounds, the PINGs, no
# empty rounds, the held keys released at a session's end): rdpd built for the PC against the
# real kapi.h (rdpd/rdpdhost.c: a kapi table with real sockets and fake windows), the current
# one and an older one (OLD_REV, default f7cb6f19: the last lock-step rdpd), then
#  * rdpd/pipeline_test.py: a Python client, as an older client and as a pipelined one;
#  * rdpd/conntest (when the .NET SDK is there: dotnet, else ~/.dotnet/dotnet): Onyx Remote's
#    own Connection.cs against both servers (the coalescing, the reconnection, COPY).
#   sh tools/tests/run_rdpd_pipeline_test.sh [OLD_REV]
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
b=$(mktemp -d)
trap 'rm -rf "$b"' EXIT
rev=${1:-f7cb6f19}
git -C "$root" show "$rev:user/BinUtils/rdpd.c" > "$b/rdpd_old.c" 2>/dev/null || git -C "$root" show "$rev:user/bin/rdpd.c" > "$b/rdpd_old.c"
sh "$here/rdpd/build_host.sh" "$b/new" > /dev/null
oldhost="$b/old/rdpd_host"
# (f7cb6f19's rdpd includes the kapi.h of its time, gone since AppKit: then only the new server is tested)
sh "$here/rdpd/build_host.sh" "$b/old" "$b/rdpd_old.c" > /dev/null 2>&1 || { echo "(the older rdpd $rev does not build against today's headers: its checks skipped)"; oldhost=; }
python3 "$here/rdpd/pipeline_test.py" "$b/new/rdpd_host" $oldhost
DOTNET=${DOTNET:-$(command -v dotnet || echo "$HOME/.dotnet/dotnet")}
if [ -x "$DOTNET" ]; then
	DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 "$DOTNET" build "$here/rdpd/conntest/conntest.csproj" -c Release -o "$b/cs" -v quiet -nologo > /dev/null
	"$DOTNET" "$b/cs/conntest.dll" "$b/new/rdpd_host" $oldhost
else
	echo "(no .NET SDK: Onyx Remote's Connection not tested)"
fi
