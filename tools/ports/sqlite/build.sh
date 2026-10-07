#!/bin/sh
# build.sh -- SQLite for Onyx (third_party/sqlite-3.50.4, the amalgamation; public domain):
# libsqlite3.a + sqlite3.h into the POSIX sysroot, and the sqlite3 shell (out/ports/bin/sqlite3.elf).
#
# The unix VFS on libonyxposix (open / pread / pwrite / fsync / ftruncate / fcntl locks that always
# succeed / unlink of an open file). No mmap I/O (SQLITE_MAX_MMAP_SIZE=0), no extension loading,
# threads on (SQLITE_THREADSAFE=1, pthreads). WAL needs a shared-memory file: use
# PRAGMA locking_mode=EXCLUSIVE with journal_mode=WAL, or keep the default rollback journal.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

SRC=$TP/sqlite-3.50.4
B=$PORTS_OUT/build/sqlite
mkdir -p "$B"
DEFS="-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_MAX_MMAP_SIZE=0 -DHAVE_USLEEP=1 \
      -DHAVE_LOCALTIME_R=1 -DHAVE_GMTIME_R=1 -DHAVE_FDATASYNC=1 -DHAVE_STRCHRNUL=0 -DSQLITE_OS_UNIX=1 \
      -DSQLITE_DEFAULT_FILE_PERMISSIONS=0644 -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_JSON1 \
      -DSQLITE_ENABLE_RTREE -DSQLITE_TEMP_STORE=2"

echo "sqlite: libsqlite3.a"
$CC $CFLAGS $DEFS -c "$SRC/sqlite3.c" -o "$B/sqlite3.o"
rm -f "$B/libsqlite3.a"
$AR rcs "$B/libsqlite3.a" "$B/sqlite3.o"
cp "$B/libsqlite3.a" "$ONYX_SYSROOT/lib/"
cp "$SRC/sqlite3.h" "$SRC/sqlite3ext.h" "$ONYX_SYSROOT/include/"
printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: SQLite\nDescription: SQL database engine\nVersion: 3.50.4\nLibs: -L${libdir} -lsqlite3\nCflags: -I${includedir}\n' "$ONYX_SYSROOT" > "$ONYX_SYSROOT/lib/pkgconfig/sqlite3.pc"

echo "sqlite: the sqlite3 shell"
$CC $CFLAGS $DEFS -DHAVE_READLINE=0 -DHAVE_EDITLINE=0 -DSQLITE_OMIT_POPEN \
    "$SRC/shell.c" $LDFLAGS -lsqlite3 -o "$B/sqlite3.elf"
onyx_tool_done "$B/sqlite3.elf" sqlite3
