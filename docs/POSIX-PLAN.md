# Onyx: demand paging and the minimum POSIX layer for a WebKit port

*Status: plan (2026-10-02, kapi v74 on `main`). This is the spec for kapi **v75**: what WebKit and its libraries need from the OS, the toolchain decision, and the work packages, each with its exact ABI. Every claim about the current system cites the code it was checked against. Where something still has to be confirmed against a chosen upstream revision, it is marked **(verify)**.*

---

## 0. Findings from the code (read these first)

Five facts from the code shape the design. Some of them contradict the current docs.

1. **`TPIDR_EL0` is already saved per task.** Circle's `TaskSwitch` (`circle/lib/sched/taskswitch.S` lines 71 and 94, field `TTaskRegisters::tpidr_el0`) saves and restores it, and Onyx's scheduler calls it (`kernel/sched/scheduler.cpp:211`). Preemption from EL0 also goes through it: `El0IrqExit` → `Yield` → `TaskSwitch`. The kernel never writes `TPIDR_EL0`. So a value an EL0 thread writes with `msr tpidr_el0` (allowed at EL0) survives every switch today. Only three things are missing:
   - an initial value for a new thread (a new `CTask` starts with 0: `memset (&m_Regs, 0 …)` in `circle/lib/sched/task.cpp`);
   - a value for app-core jobs (cores 2–3 keep whatever was there);
   - a test.

   `docs/HANDOFF.md`, `docs/EL0-PROTECTED-MODE.md` §7 and `docs/SUPERTUXKART-PORT.md` K1 should be corrected (WP-MEM does this).
2. **The kernel works in app buffers in place**, and assumes that *"an app's user pages are never unmapped while it lives"* (`kern/uaccess.h`). For example, `kapi_read` calls `UserWritable` and then `f_read` straight into the buffer, yielding between 64 KB chunks (`ChunkedRead`, `sys/kapi.cpp`). `kapi_tcp_recv` does `memcpy` into the user buffer. A fault at EL1 outside the uaccess fixup table is a **kernel panic** (`SyncHandlerEL1` → `DumpAndHalt`). Demand paging and `munmap` must therefore:
   - make `UserReadable` / `UserWritable` **populate**;
   - make the fault-safe copies **retry after populating**;
   - never free a frame that a kapi in progress is using: "pins" plus a *deferred zap* (§3.1).
3. **`SyncHandlerEL1` runs on the core's small exception stack** (`vectors.S`: `SyncEL1Entry` uses `SAVE_TRAP` on `SP_EL1`), with all of DAIF masked. Circle's spin locks assert there (the FIQ check documented in `DumpAndHalt`). So **no page allocation may happen in the EL1 exception path**: the EL1 copy routines fail through the fixup, and C code populates and retries.
4. **Circle's sockets support TCP and UDP and `MSG_DONTWAIT`, and have a `GetStatus ()`** that returns `bConnected`, `bRxReady` and `bTxReady` (`circle/include/circle/net/netconnection.h`). They have no select, no non-blocking connect, no non-blocking accept (`CSocket::Accept` blocks, `circle/lib/net/socket.cpp:192`), no `MSG_PEEK`, no half-close and no peer-port getter. Also, `CSocket::Receive` **loses data when the buffer is smaller than `FRAME_BUFFER_SIZE`** (its own doc comment). Today's `DoRecv` passes the app's length straight through when `netcore=0`.
5. **The toolchain is `--disable-threads --disable-tls`** (`Thread model: single`). `__thread` compiles to **emutls**, and with single gthreads that is one process-wide copy. libsupc++'s static-init guards are not thread-safe. `std::thread` and `std::condition_variable` are missing from `libstdc++.a`. `libstdc++.a` already contains `std::filesystem`.
   - newlib is 4.4.0 with retargetable locking and `_REENT_THREAD_LOCAL` off, so `errno` is shared by all threads.
   - newlib's `pthread.h` declares nothing unless `_POSIX_THREADS` is defined, and its pthread types are 32-bit object ids.
   - newlib lacks these headers: `sys/mman.h`, `poll.h`, `sys/socket.h`, `netinet/*`, `arpa/inet.h`, `netdb.h`, `sys/uio.h`, `sys/un.h`, `sys/utsname.h`, `semaphore.h`, `sys/ioctl.h`, `net/if.h`, `sys/random.h`, `sys/statvfs.h`, `endian.h`, `byteswap.h`, `dlfcn.h`, `execinfo.h`, `syslog.h`, `sys/sysmacros.h`.
   - `libc.a` lacks these functions: `posix_memalign`, `realpath`, `sysconf`, `clock_gettime`, `nanosleep`, `sleep`, `usleep`, `sigaction`, `posix_spawn`, `getpagesize`, `getcwd`, `opendir`, `lstat`, `mmap`, `uname`, `gethostname`, `timegm`.
   - newlib's `rename` goes through `_link_r` + `_unlink_r` (`libc_a-renamer.o`), so the POSIX layer must define `rename` itself.

---

## 1. What WebKit and its dependencies need from the OS

Scope:
- **WTF**, using its POSIX/Unix code paths (`wtf/posix`, `wtf/unix`, generic RunLoop and WorkQueue as on PlayStation).
- **JavaScriptCore** with C_LOOP: `ENABLE_JIT=OFF`, `ENABLE_C_LOOP=ON`, no WebAssembly, no sampling profiler, `USE_SYSTEM_MALLOC=ON`, Gigacage off.
- **WebCore** with curl networking and Skia or Cairo CPU rendering, statically linked, with no `dlopen`.
- The libraries: **curl** (+mbedTLS, nghttp2, zlib, brotli), **ICU**, **HarfBuzz**, **FreeType**, **libxml2** (+libxslt optional), **SQLite**, **Cairo/pixman** or **Skia**.

Legend:
- **MUST**: needed to build or run correctly.
- **SHOULD**: real code paths use it; a degraded fallback exists.
- **STUB-OK**: it must link and return a sane failure or no-op.
- "Who" names the main users.

### 1.1 Threads and TLS

| Need | Who | Level | How in Onyx |
|---|---|---|---|
| `pthread_create` (attr: stack size, detach state), `join`, `detach`, `self`, `equal`, `exit` | WTF `ThreadingPOSIX`, curl threaded resolver, SQLite, libxml2, Skia, cairo, ICU (via libstdc++) | MUST | libc on `thread_create_ex` (WP-MEM) |
| `pthread_attr_*` (setstacksize, getstacksize, setdetachstate, setguardsize no-op, setstack → `ENOTSUP`, sched* no-op) | WTF, curl | MUST | libc |
| `pthread_getattr_np` + `pthread_attr_getstack` (stack bounds) | WTF `StackBounds` (JSC recursion limits, conservative GC) | MUST | libc on `thread_info` |
| `pthread_setname_np` / `getname_np` | WTF, Skia | STUB-OK | name kept in libc; the kernel name is set at creation |
| `pthread_key_create`/`delete`/`getspecific`/`setspecific` with destructors | WTF `ThreadSpecific`, libxml2, libstdc++ (thread_local destructors), mbedTLS no | MUST | libc, 128 keys |
| `__thread` / C++ `thread_local` (static TLS) | pixman, Skia, libstdc++ (`call_once`), newlib errno (option b), WebKit in places | MUST | `TPIDR_EL0` + TLS block per thread (libc). Toolchain (a): emutls override; (b): native TLS |
| `pthread_mutex_*` (normal, recursive, errorcheck, trylock, timedlock), mutexattr | everyone | MUST | futex (`wait_word` / `wake_word`, v68) |
| `pthread_cond_*` (wait, timedwait on REALTIME or MONOTONIC via `condattr_setclock`, signal, broadcast); `pthread_cond_clockwait` | WTF `ThreadCondition`, libstdc++ `condition_variable`, curl | MUST | futex sequence counter |
| `pthread_once` | ICU (via `std::call_once`), libxml2, curl | MUST | futex state word |
| `pthread_rwlock_*` | libstdc++ `shared_mutex`, Skia, curl | MUST | futex |
| `sem_init`/`wait`/`trywait`/`timedwait`/`post`/`destroy` (`semaphore.h`) | WTF `ThreadingPOSIX` (suspend/resume handshake), Skia | MUST | futex |
| `pthread_barrier_*`, `pthread_spin_*` | Skia, tests | SHOULD | libc |
| `sched_yield` | WTF, libstdc++ | MUST | `kapi_yield` |
| `sched_get_priority_min/max`, `pthread_setschedparam` | WTF | STUB-OK | map priority > 0 to `thread_priority (tid, 1)` (SHOULD) |
| `pthread_kill` / `Thread::suspend` / `resume` via signals | WTF (JSC: `MachineThreads` scanning threads registered with the same VM; sampling profiler; Watchdog) | STUB-OK for the minimum | Use one VM on the main thread, no concurrent GC across VMs, no sampling profiler. Later: a `thread_suspend` / `thread_regs` kapi |
| `pthread_cancel`, `pthread_atfork` | rare | STUB-OK | `ENOSYS`; `atfork` returns 0 (there is no fork) |
| `sysconf(_SC_NPROCESSORS_ONLN/CONF)` | WTF `numberOfProcessorCores`, JSC GC markers, Skia | MUST | **1**: all of a process's threads run on core 0 (docs/02 §5) |

### 1.2 Memory

| Need | Who | Level | How |
|---|---|---|---|
| `mmap(MAP_PRIVATE\|MAP_ANONYMOUS)` with any prot, PROT_NONE reservations of GBs, `MAP_NORESERVE`, `MAP_FIXED` inside a reservation | WTF `OSAllocatorPOSIX` (reserve, then commit with `mprotect`; aligned reservations trim their edges with partial `munmap`), JSC CLoopStack, JSC structure heap (a multi-GB aligned reservation **(verify** the size in the chosen revision; can be shrunk)) | MUST | `vm_map` (WP-MEM) |
| `munmap`, partial (splits) | WTF | MUST | `vm_unmap` |
| `mprotect` (NONE ↔ READ ↔ READ\|WRITE; no EXEC: there is no JIT) | WTF `OSAllocator::commit/decommit` | MUST | `vm_protect` |
| `madvise(MADV_DONTNEED / MADV_FREE)` (zero-fill on next touch), `MADV_WILLNEED`, NORMAL/RANDOM/SEQUENTIAL | WTF decommit and `hintMemoryNotNeededSoon` | MUST | `vm_advise` |
| `mmap` of a file, `MAP_PRIVATE`, `PROT_READ` | WTF `MappedFileData`, HarfBuzz `hb_blob_create_from_file`, Skia `SkData::MakeFromFILE`, ICU (unused with static data) | MUST | libc: anonymous map + `pread` (eager copy). `MAP_SHARED` + `PROT_WRITE` on a file → `ENOTSUP` |
| `getpagesize`, `sysconf(_SC_PAGESIZE)` = **65536** | WTF `pageSize()` (asserts ≤ `CeilingOnPageSize`), Skia, SQLite | MUST | libc. WebKit port: `CeilingOnPageSize = 64 KB` for OS(ONYX) (`wtf/PageBlock.h`) |
| `posix_memalign`, `aligned_alloc`, `memalign`, `malloc_usable_size` | WTF `fastAlignedMalloc` (JSC MarkedBlocks with system malloc), Skia | MUST | newlib has `memalign`, `aligned_alloc` and `malloc_usable_size`; libc adds `posix_memalign` |
| A good `malloc` (multi-threaded, returns memory) | everything | SHOULD | newlib's `mallocr` is sbrk-only with one lock. Later: dlmalloc 2.8.6 (CC0) or mimalloc (MIT) on `vm_map` (§6) |
| `sysconf(_SC_PHYS_PAGES)`, memory footprint (`vm_stats`), `getrusage` (zeros + maxrss) | WTF `ramSize`, `MemoryPressureHandler`, `memoryFootprint` | SHOULD | libc on `meminfo` / `vm_stats` |

### 1.3 Time

| Need | Who | Level | How |
|---|---|---|---|
| `clock_gettime(CLOCK_REALTIME, CLOCK_MONOTONIC, _RAW, _COARSE, CLOCK_BOOTTIME)`, `clock_getres` | WTF `MonotonicTime` / `WallTime`, libstdc++ `chrono`, curl, SQLite | MUST | libc, user-side: `CNTPCT_EL0` plus one `clock_info` sample (WP-PROC); no system call per read |
| `CLOCK_PROCESS_CPUTIME_ID` / `CLOCK_THREAD_CPUTIME_ID` | WTF `CPUTime` | STUB-OK | monotonic time (per-task CPU accounting later) |
| `gettimeofday`, `time` | everyone | MUST | rebuilt on `clock_info` (today's `_gettimeofday` re-derives the epoch from `get_datetime`) |
| `nanosleep`, `clock_nanosleep`, `usleep`, `sleep` | WTF, curl, SQLite, libstdc++ `sleep_for` | MUST | `sleep_us` (WP-PROC). Resolution: scheduler wake checks, worst case one 10 ms tick when the system idles; < 1 ms sleeps yield-spin |
| `localtime_r`, `gmtime_r`, `mktime`, `strftime`, `tzset` + `TZ` | WTF date code, ICU, SQLite, curl | MUST | newlib has them. libc sets `TZ` to a fixed offset from `clock_info.tz_minutes` when the environment has none (no DST). An Olson name in `TZ` is honoured by ICU (JS dates), not by newlib |
| `timegm` | curl (has its own), WebKit (own date math) | SHOULD | libc |
| `alarm`, `setitimer`, `timer_create` | curl (SIGALRM resolver: off) | STUB-OK | `ENOSYS` |

### 1.4 Files and directories

| Need | Who | Level | How |
|---|---|---|---|
| `open` with `O_RDONLY/WRONLY/RDWR/CREAT/TRUNC/APPEND/EXCL/CLOEXEC/NONBLOCK/DIRECTORY`; `read`, `write`, `lseek`, `pread`, `pwrite`, `close` — **incremental, not whole-file** | SQLite (pread/pwrite of pages), WTF `FileSystem`, curl (CA file, cookies), libxml2, ICU (no) | MUST | `file_*` (WP-FILE) |
| `fstat`, `stat`, `lstat` (= stat): size, mode file/dir, mtime, a stable `st_ino`/`st_dev` (SQLite identifies files by them for its inode lock table) | SQLite, WTF, libstdc++ filesystem | MUST | `file_stat` / `path_stat` |
| `ftruncate` (grow with zeros), `truncate`, `fsync`, `fdatasync` | SQLite | MUST | `file_truncate`, `file_sync` |
| `unlink` **of an open file** (SQLite's `DELETEONCLOSE` temp files are unlinked right after `open`), `rename` replacing an existing target, `mkdir`, `rmdir` (`ENOTEMPTY`) | SQLite, WTF, curl | MUST | `path_unlink` / `path_rename` / `path_mkdir` |
| `opendir`, `readdir` (255-character names), `closedir`, `fdopendir`, `dirfd`, `scandir` | WTF, libstdc++ filesystem, fontconfig | MUST (opendir/readdir), SHOULD (rest) | `dir_read` (`kapi_dirent2`) |
| `access`, `getcwd`, `chdir`, `realpath` | WTF, ICU, SQLite, curl | MUST | libc on `path_stat` + `getcwd` |
| `openat`/`fstatat`/`unlinkat`/`mkdirat`/`renameat` (`AT_FDCWD` + dirfd of a known path) | libstdc++ `std::filesystem::remove_all` | SHOULD | libc |
| `fcntl(F_GETFL/F_SETFL O_NONBLOCK/O_APPEND, F_GETFD/F_SETFD FD_CLOEXEC, F_DUPFD(_CLOEXEC))`; `F_SETLK/F_GETLK/F_SETLKW`; `flock` | curl (NONBLOCK), SQLite (POSIX locks), WTF `lockFile` | MUST (flags); STUB-OK (locks succeed, single process per DB) | libc |
| `dup`, `dup2`, `dup3` (shared offset) | curl, generic code | MUST | libc (refcounted open-file descriptions) |
| `pipe`, `pipe2(O_NONBLOCK\|O_CLOEXEC)` | curl `multi_wakeup` (pipe fallback), WebKit run loops | MUST | kernel pipe stream + `stream_write_nb` |
| `socketpair(AF_UNIX)` | curl (if `HAVE_SOCKETPAIR`: leave undefined) | SHOULD | libc: two pipes |
| `mkstemp`, `tmpfile`, `/tmp` | SQLite, WTF `openTemporaryFile` | MUST | newlib `mkstemp` (works once `O_EXCL` does); libc maps `/tmp` → `RAM:/tmp` |
| `statvfs`/`fstatvfs` | WebKit disk-cache sizing | SHOULD | `vol_info` (v71) |
| `utime`/`utimes`/`futimens` | WTF, curl `-R` | SHOULD | `path_utime` |
| `chmod`, `fchmod`, `umask`, `chown` | SQLite, curl | STUB-OK | read-only attribute (SHOULD), else 0 |
| `symlink`, `readlink`, `link` | WTF | STUB-OK | `ENOSYS` / `EPERM` |
| `/dev/null`, `/dev/zero`, `/dev/urandom`, `/dev/random`, `/dev/stdin\|stdout\|stderr` | mbedTLS entropy, curl, WebKit `RandomDevice` fallback | MUST | libc pseudo-files |
| `readv`/`writev` | misc | SHOULD | libc loops |
| `isatty`, `ttyname`, `tcgetattr` | curl progress, generic | STUB-OK | console fds → 1, else 0 / `ENOTTY` |

### 1.5 Processes, environment, signals, misc

| Need | Who | Level | How |
|---|---|---|---|
| `getenv`/`setenv`/`unsetenv`/`putenv`/`environ`, populated at start | WebKit (`WEBKIT_*`, `JSC_*` options), curl (proxies, `CURL_CA_BUNDLE`), ICU (`TZ`, `ICU_DATA`) | MUST | `get_env` (WP-PROC) + newlib |
| `argc`/`argv`/`envp` to `main` | every tool | MUST | `get_argv` + `crt0posix` |
| `getpid`, `getppid` | SQLite (temp names), curl | MUST | `getpid` kapi |
| `posix_spawn(p)`, `waitpid(WNOHANG)`, `WIFEXITED`/`WIFSIGNALED` | tests, curl (no), WebKit2 (out of scope) | SHOULD | `spawn_ex` + `proc_wait` |
| `fork`, `execve`, `system`, `popen` | — | STUB-OK | `ENOSYS` |
| `sigaction`/`signal` (stored handlers), `sigprocmask`/`pthread_sigmask` (no-op), `sigemptyset` & co., `raise`, `abort`, `kill(getpid(), sig)`; `SIGPIPE` never raised (`send` → `EPIPE`); `sigaltstack` no-op | curl (`signal(SIGPIPE, SIG_IGN)`, `MSG_NOSIGNAL`), WTF, JSC | MUST (to link and behave) | libc only; no asynchronous signals |
| `atexit`, `exit`, `_exit`, `abort` (status 134) | everyone | MUST | newlib + `_exit` → `kapi_exit` |
| `getrandom`, `getentropy`, `arc4random` | WebKit `cryptographicallyRandomValues`, mbedTLS, curl | MUST | existing `kapi_random` (v30) |
| `uname`, `gethostname` | curl, UA strings | SHOULD | libc: `Onyx`, `aarch64`, host name from `net_info` |
| `getuid`/`geteuid`/`getgid`/`getegid` → 0, `getpwuid(_r)`/`getpwnam` (a fixed `onyx` user, `HOME`) | SQLite, curl (`.netrc`) | STUB-OK | libc |
| `getrlimit`/`setrlimit` (`RLIMIT_STACK` from `thread_info(1)`, `NOFILE` 1024) | WTF, JSC | SHOULD | libc |
| `setlocale` with `C`/`C.UTF-8`/`""`, `nl_langinfo(CODESET)`, `newlocale`/`uselocale` | ICU default code page, libstdc++ | MUST (C/C.UTF-8 only) | newlib (`_MB_CAPABLE`) |
| `iconv_*` | libxml2 (build without iconv), curl (no) | STUB-OK | newlib's is disabled; `iconv_open` → −1 `EINVAL` |
| `dlopen`/`dlsym`/`dladdr`, `backtrace`, `syslog` | WTF (backtrace, symbolication), ICU (`U_ENABLE_DYLOAD=0`) | STUB-OK | stubs; `syslog` → kmsg (SHOULD) |
| `setjmp`/`longjmp` | JSC conservative register scan, libpng | MUST | newlib has them |

### 1.6 Network (curl, WebCore's curl backend)

| Need | Who | Level | How |
|---|---|---|---|
| `socket(AF_INET, SOCK_STREAM\|SOCK_NONBLOCK\|SOCK_CLOEXEC)`, `SOCK_DGRAM`; `AF_INET6`/`AF_UNIX` → `EAFNOSUPPORT` | curl (built `--disable-ipv6`) | MUST (TCP), SHOULD (UDP) | `sock_open` (WP-NET) |
| **non-blocking `connect` → `EINPROGRESS`**, then `poll(POLLOUT)` and `getsockopt(SO_ERROR)` | curl's connection filters (always non-blocking) | MUST | async connect (§3.3) |
| `send`/`recv` with `MSG_DONTWAIT`, `MSG_PEEK` (curl's connection-alive check), `MSG_NOSIGNAL` (ignored), `MSG_WAITALL` (libc loop); `sendto`/`recvfrom` | curl | MUST (PEEK included) | `sock_send` / `sock_recv` with a kernel carry buffer |
| `bind`/`listen`/`accept`/`accept4` (non-blocking accept) | servers, tests | SHOULD | Circle patch `CSocket::AcceptReady ()` |
| `getsockname`/`getpeername` | curl (connection info) | MUST | `sock_name` (+ Circle patch `GetForeignPort`) |
| `setsockopt`/`getsockopt`: `SO_ERROR`, `SO_RCVTIMEO`/`SO_SNDTIMEO`, `SO_TYPE`, `SO_BROADCAST`; `TCP_NODELAY`, `SO_KEEPALIVE`, `SO_REUSEADDR`, `SO_RCVBUF`/`SNDBUF` accepted and ignored | curl | MUST | `sock_getopt` / `sock_setopt` + libc |
| `shutdown` | rare (curl no) | STUB-OK | `SHUT_RDWR` = mark; `SHUT_WR` = no-op |
| `poll` over sockets, pipes, files and stdin; `select`/`pselect` on top of poll (`FD_SETSIZE` 1024) | curl (`curl_multi_poll`), WebKit's curl scheduler thread | MUST | `poll` kapi (WP-NET) |
| `ioctl(FIONBIO, FIONREAD)` | curl alternatives | SHOULD | libc |
| `getaddrinfo` (`AI_NUMERICHOST`, `AI_PASSIVE`, numeric and well-known services), `freeaddrinfo`, `getnameinfo` (`NI_NUMERICHOST`), `gai_strerror`, `gethostbyname(_r)`, `inet_pton`/`ntop`/`aton`/`addr`, `htons`… | curl (threaded resolver: blocks a worker thread) | MUST | libc on the existing `net_resolve` (v43: the kernel's DNS cache; IPv4, one address) |
| TLS: curl + mbedTLS (in tree: `third_party/mbedtls-3.6.3`) | curl | MUST | curl `--with-mbedtls`. **Note:** WebKit's curl backend reads certificate details through **OpenSSL** APIs (`CurlSSLVerifier`, `CURLOPT_SSL_CTX_FUNCTION`) **(verify)**: either a small mbedTLS adaptation of that file (recommended) or OpenSSL/BoringSSL (§6, licences) |

### 1.7 What the libraries need from the build

- **WebKit:** C++20/23 with GCC ≥ 14 (OK), libstdc++ `<filesystem>`, `<span>`, `<bit>`, `<expected>`, ranges, `<atomic>` (lock-free on AArch64: libgcc's `__aarch64_cas*` outline atomics are present). It uses `std::mutex`/`std::condition_variable` in places and in RunLoopGeneric/WorkQueueGeneric (the PlayStation model). Built `-fno-exceptions -fno-rtti`. Host tools: Perl, Python 3, **Ruby** (offlineasm generates the C_LOOP LLInt), gperf, CMake, Ninja.
- **ICU:** `std::mutex`, `std::condition_variable` and `std::call_once` (`umutex.cpp`) → it needs a **threaded libstdc++**. ICU data linked as a static object; cross-building ICU needs a host ICU build.
- **Skia:** `std::thread`, `std::mutex`, `thread_local`, C++20; `getauxval` for CPU features → port tweak (AArch64 NEON is always present).
- **Cairo/pixman:** pthreads; pixman uses `__thread`; pixman's ARM CPU detection (`/proc/cpuinfo`) → compile-time flags.
- **SQLite:** `-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_MAX_MMAP_SIZE=0 -DHAVE_USLEEP=1`, the unix VFS on the calls above. Avoid WAL, which needs `MAP_SHARED` for `-shm`, unless `locking_mode=EXCLUSIVE`.
- **libxml2:** `--without-http --without-ftp --without-iconv --without-python`, threads on.
- **curl:** `--with-mbedtls --disable-ipv6 --enable-threaded-resolver --disable-unix-sockets --without-libpsl --disable-ldap`, nghttp2/zlib/brotli (in tree).

---

## 2. The toolchain

### 2.1 What we have

`/opt/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf`:
- GCC 14.2.1, configured `--disable-threads --disable-tls --disable-shared --with-newlib`, thread model **single**;
- newlib 4.4.0 with `_RETARGETABLE_LOCKING 1`, `_MB_CAPABLE`, `_WANT_IO_LONG_LONG`, no `_WANT_REENT_THREAD_LOCAL`, iconv disabled;
- libstdc++ with exceptions and RTTI available (the STK port uses them through `stk.ld` + `onyx_eh.c`), `std::filesystem` compiled in, and none of the thread classes' out-of-line code.

The STK port (`user/stk/compat/bits/gthr-default.h`, `onyx_gthreads.c`, `onyx_gthreads_cxx.cpp`) shows the workaround: shadow `gthr-default.h`, define `_GLIBCXX_HAS_GTHREADS`, and rewrite `std::thread` / `condition_variable` / `call_once`. Its own doc lists what remains broken (docs/SUPERTUXKART-PORT.md):
- function-local statics are not thread-safe;
- `thread_local` is one global copy (emutls with single gthreads);
- `errno` is shared;
- `libstdc++.a`'s internal code still believes it is single-threaded: `__is_single_threaded()` → non-atomic reference counts on `std::locale` facets mixed with atomic ones in user code.

### 2.2 The options

**(a) Keep `aarch64-none-elf` + newlib, and provide pthreads and the glue.** It is possible:
- shadow `pthread.h` and `sys/_pthreadtypes.h` through `-isystem $SYSROOT/include`;
- shadow `gthr-default.h` with a copy of GCC's `gthr-posix.h`;
- override `__emutls_get_address` (libgcc) with a `TPIDR_EL0`-based per-thread table;
- override `__cxa_guard_acquire/release/abort`;
- compile libstdc++'s `thread.cc`, `condition_variable.cc`, `mutex.cc`, `future.cc` and `shared_ptr.cc` from GCC 14.2 sources into a side library.

It stays fragile: archive-member clashes with `libstdc++.a`, single-threaded refcounts inside `libstdc++.a`, `errno` shared, `chrono` / `this_thread` configured without `clock_gettime` / `nanosleep`. It is acceptable for **C libraries** (SQLite, libxml2, curl, mbedTLS, FreeType, HarfBuzz with pthreads, cairo/pixman with the emutls override). It is not a sound base for ICU + Skia + WebKit.

**(b) A custom toolchain `aarch64-onyx-elf`** (vendor `onyx`, OS `elf`): GCC 14.2 + binutils 2.43 + newlib 4.4.
- **No GCC source patch.** GCC's `config.gcc` matches `aarch64*-*-elf` **(verify)**, autotools' `config.sub` accepts any vendor, and `--host=aarch64-onyx-elf` works for every third-party configure script.
- GCC: `--enable-threads=posix --enable-tls --disable-shared --enable-languages=c,c++ --with-newlib --enable-libstdcxx-time=yes`.
- newlib: `--enable-newlib-reent-thread-local` (errno and `_reent` per thread), `--enable-newlib-retargetable-locking`, `--enable-newlib-io-long-long`, `--enable-newlib-io-c99-formats`, `--disable-newlib-supplied-syscalls`.
- The only Onyx-specific input is a **header overlay** installed into `$PREFIX/aarch64-onyx-elf/include` **after** newlib and **before** the final GCC stage, so that libgcc and libstdc++ build against our `pthread.h`. The overlay is exactly `user/libc/posix/include/pthread.h`, `sys/_pthreadtypes.h`, `semaphore.h` and `sched.h` (if needed); `pthread.h` also defines `_POSIX_TIMEOUTS` / `_POSIX_THREADS` / `_POSIX_READER_WRITER_LOCKS`, which `gthr-posix.h` tests. **The pthread types are then ABI-frozen.**
- Result: native TLS (`mrs tpidr_el0` + local-exec offsets), real gthreads (`std::thread` and `std::mutex` work and libstdc++ is internally thread-aware), thread-safe statics, per-thread errno, `steady_clock` on `clock_gettime` (checked in the installed `c++config.h`).

### 2.3 Recommendation: (b), staged after an (a) interim

**Phase 1** uses (a). WP-LIBC, its tests and the C smoke ports (SQLite, libxml2, curl + mbedTLS) are developed on the existing toolchain with the `-isystem` shadowing and the emutls override. Nothing in WP-LIBC's code depends on the choice: the same headers and the same `libonyxposix.a` sources.

**Phase 2** builds **WP-TC**, the `aarch64-onyx-elf` toolchain. It is required before ICU, Skia and WebKit.

Reasons:
- ICU and Skia need a real threaded libstdc++;
- WebKit's `std::filesystem`, `chrono` and `thread_local` must be right;
- the patch surface is tiny (a header overlay and a build script);
- the kernel and the existing apps keep `aarch64-none-elf` unchanged.

WP-TC deliverables:
- `tools/toolchain/build-onyx-toolchain.sh`: pinned tarball URLs + sha256; stages binutils → GCC stage 1 (C only) → newlib → overlay → GCC final; `-j$(nproc)`. About **45–60 min** on 8 cores, about 600 MB installed, about 150 MB as `.tar.xz`.
- Installed at `/opt/toolchains/aarch64-onyx-elf-14.2`.
- `tools/toolchain/fetch.sh` downloads a prebuilt asset from a GitHub Release (`stephaneweg/Onyx`, tag `toolchain-14.2-onyx1`, Linux x86_64 host) and checks its sha256.
  - On Windows the user runs it in **WSL**, as docs/03 §1 already prescribes.
  - The cloud environment's setup script runs it too.
  - GPL: the release also carries the exact source tarballs and the script.
- Acceptance checks:
  - `aarch64-onyx-elf-gcc -v` shows `Thread model: posix`;
  - `c++config.h` has `_GLIBCXX_HAS_GTHREADS`, `_GLIBCXX_USE_CLOCK_MONOTONIC`, `_GLIBCXX_USE_NANOSLEEP`, `_GLIBCXX_USE_SCHED_YIELD`, `_GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT`, `_GLIBCXX_HAVE_TLS`;
  - `posixtest cxx` passes on the Pi.
- **Risk:** libstdc++'s cross configure for newlib (`crossconfig.m4`) may not enable the time features → a 1-file patch or `--enable-libstdcxx-time=yes` must be confirmed.

#### WP-TC resolutions (what was built; docs/03 §1.1, §5.4)

1. **The script**: `tools/toolchain/build-onyx-toolchain.sh` (MIT): binutils 2.43, GCC 14.2.0 (GMP
   6.3.0, MPFR 4.2.1, MPC 1.3.1 built in its tree: no host libraries), newlib 4.4.0.20231231 —
   pinned URLs + sha256; stages fetch → binutils → GCC stage 1 (C, `--without-headers
   --disable-threads`) → newlib → the overlay → GCC final → final (strip, `ONYX-TOOLCHAIN.txt`, the
   patch and the script copied into `share/onyx-toolchain/`, the build trees removed) → check (the
   acceptance, printed). Stamps in `$WORK/stamps`: resumable. **27 minutes on 4 cores** (no
   bootstrap: a cross compiler), 240 MB installed, about 10 GB of build space at the peak.
   Linux and WSL (the build tree on the Linux side). `config.gcc` takes `aarch64-*-elf` with any
   vendor: no GCC target patch.
2. **The configuration** (all stages `--target=aarch64-onyx-elf --disable-nls --disable-multilib`):
   - GCC final: `--enable-languages=c,c++ --enable-threads=posix --enable-tls --disable-shared
     --with-newlib --with-cpu=cortex-a72 --with-sysroot=$PREFIX/aarch64-onyx-elf
     --with-native-system-header-dir=/include --enable-libstdcxx-filesystem-ts
     --disable-libstdcxx-pch --enable-libstdcxx-backtrace=no --without-isl`, and off: libssp,
     libgomp, libquadmath, libsanitizer, libvtv, libatomic, libitm, libcc1. **The sysroot is
     needed**: without it GCC looks for the target headers in `sys-include` when it makes its
     `<limits.h>`, finds none, and installs one that does not chain to newlib's (no `PATH_MAX`).
     Under the prefix, so the toolchain stays relocatable.
   - newlib: `--enable-newlib-reent-thread-local --enable-newlib-retargetable-locking
     --enable-newlib-io-long-long --enable-newlib-io-c99-formats --disable-newlib-supplied-syscalls
     --enable-newlib-mb --enable-newlib-register-fini` (the last two as Arm's build), target flags
     `-O2 -g -ffunction-sections -fdata-sections`. **Only `newlib` is built, not libgloss**
     (`all-target-newlib`): Onyx has no use for semihosting's crt0 / rdimon / nosys, and libgloss's
     aarch64 rdimon does not compile with a thread-local `_reent`.
   - `--enable-libstdcxx-time=yes` is **not** usable: it runs link tests, impossible before the
     target's C library links (newlib without syscalls: `gcc_no_link`, a fatal configure error).
3. **The one GCC patch** (`tools/toolchain/patches/gcc-14.2.0-libstdcxx-onyx.patch`, applied by the
   script to `libstdc++-v3/configure` + `configure.ac` + `acinclude.m4`): (a) the `auto` time check
   states `*-onyx-*` like the BSDs (monotonic and realtime clocks, `nanosleep`, `sched_yield`); (b)
   the newlib block treats `*-onyx-*` like RTEMS: `HAVE_TLS` and `link`, `readlink`, `symlink`,
   `truncate`, `sleep`, `usleep`, `setenv`, `aligned_alloc`, `quick_exit`, `strerror_l`,
   `sockatmark` (all in newlib + libonyxposix).
4. **The overlay** (installed into `$PREFIX/aarch64-onyx-elf/include` after newlib, before GCC
   final): libonyxposix's `pthread.h`, `sys/_pthreadtypes.h`, `semaphore.h`, **`sys/dirent.h`**
   (newlib's is an `#error` stub on this target: without it libstdc++'s directory iterators are not
   built), and `sys/features.h` = libonyxposix's overlay with its `#include_next` turned into
   `#include <sys/_newlib_features.h>` (newlib's, renamed) and its own guard (so the sysroot's
   `sys/features.h`, still first on the path, chains to it). The pthread types are now frozen: a
   change to those five headers is a new toolchain revision (`ONYX_TC_REVISION`, `onyx1`).
5. **Acceptance** (`check` stage): `Thread model: posix`; `c++config.h` has `_GLIBCXX_HAS_GTHREADS`,
   `_GLIBCXX_USE_CLOCK_MONOTONIC`, `_GLIBCXX_USE_CLOCK_REALTIME`, `_GLIBCXX_USE_NANOSLEEP`,
   `_GLIBCXX_USE_SCHED_YIELD`, `_GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT`, `_GLIBCXX_HAVE_TLS`,
   `_GLIBCXX_HAVE_DIRENT_H` (also `USE_PTHREAD_MUTEX_CLOCKLOCK`, `USE_PTHREAD_RWLOCK_CLOCKLOCK`,
   `USE_SC_NPROCESSORS_ONLN`, `HAVE_FDOPENDIR`, `HAVE_OPENAT`, `HAVE_UNLINKAT`, `USE_ST_MTIM`; not
   `USE_REALPATH` / `USE_UTIMENSAT`: libstdc++ falls back); newlib `_WANT_REENT_THREAD_LOCAL`;
   `__thread` compiles to `mrs tpidr_el0`; `<limits.h>` has `PATH_MAX`. All pass.
6. **libonyxposix under it**: the Makefile picks by `$(CC) -dumpmachine` (`build-onyx/`, sysroot
   `out/sysroot-onyx`, specs without `-u __emutls_get_address`); the sources by newlib's
   `_WANT_REENT_THREAD_LOCAL` (`ONYX_NATIVE_TLS` in `posix_internal.h`): no emutls override, no
   `__errno` override (newlib's `_tls_errno` is per thread). The futex `__cxa_guard_*` and the
   `-u __onyx_pthread_anchor` stay (gthr-posix's weak references must find the thread layer; its
   "threads active" probe is `pthread_cancel`). The `aarch64-none-elf` path is unchanged.
   `tools/onyx-env.sh`, `tools/onyx-toolchain.cmake`, `tools/ports/build-all.sh` choose
   `aarch64-onyx-elf` when it is installed (`ONYX_TOOLCHAIN_PREFIX` overrides), with
   `out/sysroot-onyx` and `out/ports-onyx`.
7. **Distribution** (the user's choice: not a GitHub release): the repository
   `stephaneweg/onyx-toolchain`. `aarch64-onyx-elf-14.2/` holds the `.tar.xz` split in 90 MB parts
   (`split -b 90M -d -a 2`: `.part-00`, `-01`…; this revision is 38 MB, one part), `SHA256SUMS`
   (the whole tarball and each part) and `BUILDINFO.txt`; `sources/aarch64-onyx-elf-14.2/` the six
   upstream tarballs (GCC's, 92 MB, the largest), the patch, the script and the overlay headers —
   the GPL's corresponding source beside the binaries. `tools/toolchain/fetch.sh` shallow-clones it
   (sparse: the version's directory only), checks the sha256s, unpacks into `/opt/toolchains`
   (`PREFIX=` elsewhere; the toolchain is relocatable).
8. **Linking with it, what changed**: `onyx.specs` gains `-u abort` (libonyxposix's `signal.o` —
   `signal`, `raise`, `abort` — before newlib's: under the interim toolchain `emutls.o` pulled it
   in first; without it newlib's `assert` → `abort.o` → `signal.o` clashed). Objects built by the
   interim toolchain do not link with this newlib (no `__errno`, no `_impure_ptr`: errno and the
   stdio pointers are thread-local), so the ports' in-tree zlib, nghttp2 and brotli are compiled
   from their sources under it (`onyx_dep_lib`, `tools/ports/common.sh`).
9. **Results on the posixsim bench** (`tools/tests/posixsim/tc.sh`, `ports.sh`): with
   `aarch64-onyx-elf`, posixtest 119 passed / 0 failed (v75; 1 SKIP: no network), 101 / 0 on the v74
   fallbacks; **posixtest-cxx 82 passed / 0 failed** (v75 and v74); `posixtest cxx` passes; the
   ports rebuilt with it (sqlite3, xmllint, curl): 13 / 13. The `aarch64-none-elf` path unchanged:
   posixtest 119 / 0, ports 13 / 13.

---

## 3. The design: work packages

Order: **WP-0** (skeleton, first, on `main`) → **WP-MEM ∥ WP-FILE/PROC ∥ WP-NET** (kernel, parallel worktrees) ∥ **WP-LIBC** (starts at once against the skeleton) ∥ **WP-TC** (independent). Merge order: WP-MEM, then WP-FILE/PROC, then WP-NET, then WP-LIBC.

### WP-0: the v75 ABI skeleton (half a day, one commit, before anything else)

The goal is that **no two WPs edit the same lines**. WP-0 lands the whole v75 ABI with stub bodies, so each WP then only fills functions in files it owns.

- `kern/kapi_abi.h`:
  - `KAPI_ABI_VERSION 75`;
  - all the structs and constants of §3.1–§3.3, plus `KAPI_E*`;
  - the three blocks at the end of `TKApiTable`, after `proc_stats` (slot 198), **in this order**: WP-MEM (slots 199–206), WP-FILE/PROC (207–228), WP-NET (229–241).
  - Use `long long` / `unsigned long long` for 64-bit values, never `long`: the PC build (`pc/`, `fakekapi.cpp`) has a 32-bit `long`.
- New files with stub kapis that return `-KAPI_ENOSYS`, already assigned in `kapitable.cpp`:
  - `sys/vm.cpp` (WP-MEM);
  - `sys/ofile.cpp` and `sys/procx.cpp` (WP-FILE/PROC);
  - `sys/bsdsock.cpp` (WP-NET);
  - added to `kernel/Makefile`.
- `user/kapi.h`: one wrapper per entry,
  `static inline long long kapi_vm_map (…) { return KT->version >= 75 && KT->vm_map ? KT->vm_map (…) : -KAPI_ENOSYS; }`.
  The EL0 table maps a null kernel slot to 0 (`El0Init`), so the null test also covers the PC simulator, which leaves the new fields 0.
- `kern/iowait.h` + `sys/iowait.cpp`: the shared readiness wait, complete (about 60 lines):

  ```cpp
  u32  IoGen (void);                              // the I/O generation
  void IoWake (void);                             // ++gen, wake every IoWait sleeper (core 0, IRQ context allowed)
  int  IoWait (u32 nGen, unsigned nTimeoutMs);    // sleep while IoGen () == nGen -> 0 changed / 1 timeout
  void IoWaitAddTickHook (void (*pfn) (void));    // called at each 100 Hz tick (IRQ, core 0), up to 8 hooks
  void IoWaitTick (void);                         // PeriodicTick calls it (exception.cpp, next to WordWaitTick)
  ```

  `IoWait` uses a `CSynchronizationEvent` that is pulsed (Set, then Clear, as `kern/thread.h` objects do) with a waiter count.
- `kern/stream.h`: `virtual unsigned CStream::PollMask (void) { return KAPI_POLLIN | KAPI_POLLOUT; }`.
- `kern/handle.h`: `HANDLE_OFILE = 6`; `HandleObjectClose` routes it to `OFileClose (pObj, bTeardown)` (stub in `ofile.cpp`).
- `kern/addrspace.h`:
  - `struct TVmSpace *m_pVm;` and `struct TProcInfo *m_pProcInfo;` (0 initially; `~CAddressSpace` calls `VmTeardown (this)` and `ProcInfoTeardown (this)`, both stubs);
  - `void SetTermReason (int nReason, int nCode)` / `int GetTermReason (void) const` (default `KAPI_PROC_EXITED`).
- Regenerate `user/kapi_names.h` (`tools/gen_kapi_names.py`).
- Add this file as `docs/POSIX-PLAN.md`, and three empty subsections in docs/02 §8 ("v75: memory", "v75: files and processes", "v75: sockets and poll"), one per WP.

Error convention for **every** v75 call: ≥ 0 is success; **< 0 is `-KAPI_Exxx`**, where `KAPI_Exxx` equals newlib's errno value (`sys/errno.h`), so libc does `errno = -r`. The values used:

```
EPERM 1 ENOENT 2 EINTR 4 EIO 5 EBADF 9 ECHILD 10 EAGAIN 11 ENOMEM 12 EACCES 13 EFAULT 14 EBUSY 16
EEXIST 17 EXDEV 18 ENODEV 19 ENOTDIR 20 EISDIR 21 EINVAL 22 ENFILE 23 EMFILE 24 EFBIG 27 ENOSPC 28
ESPIPE 29 EROFS 30 EPIPE 32 ENOSYS 88 ENOTEMPTY 90 ENAMETOOLONG 91 EOPNOTSUPP 95 ECONNRESET 104
ENOBUFS 105 EAFNOSUPPORT 106 ENOTSOCK 108 ENOPROTOOPT 109 ECONNREFUSED 111 EADDRINUSE 112
ECONNABORTED 113 ENETUNREACH 114 ENETDOWN 115 ETIMEDOUT 116 EHOSTUNREACH 118 EINPROGRESS 119
EALREADY 120 EDESTADDRREQ 121 EMSGSIZE 122 EPROTONOSUPPORT 123 EADDRNOTAVAIL 125 EISCONN 127
ENOTCONN 128 ENOTSUP 134
```

Every v75 kapi follows docs/03 §10's EL0 rules: pointers checked at entry, no kernel pointer handed out, ≤ 8 integer arguments, no callbacks into the app.

#### WP-0 resolutions (what the skeleton actually landed; the WPs follow this)

1. **Slots:** 8 + 22 + 13 = 43 entries, 242 slots in all (0..241, `tools/gen_kapi_names.py` reports 242). Every slot is checked in `kapi_abi.h` (`KAPI_CHECK_SLOT (name, n)`: `offsetof / 8`), every structure's size and key offsets too (`KAPI_CHECK_SIZE`, `KAPI_CHECK_FIELD`). There is no assertion on the table's total size, so a later version can still append.
2. **Files per WP:** `sys/vm.cpp` slots 199–206; `sys/ofile.cpp` slots 207–221 (`file_*`, `path_*`, `dir_read`, `stream_write_nb`); `sys/procx.cpp` slots 222–228 (`spawn_ex`, `proc_wait`, `get_argv`, `get_env`, `getpid`, `clock_info`, `sleep_us`); `sys/bsdsock.cpp` slots 229–241. The `extern "C"` prototypes sit in `kapitable.cpp`, in three blocks, and the assignments are there too: a WP keeps the signatures and never touches that file.
3. **Hook headers:** `kern/vm.h` (WP-MEM: `VmTeardown`, `struct TVmSpace` forward-declared), `kern/ofile.h` (WP-FILE/PROC: `OFileClose (void *pObj, boolean bTeardown)`) and `kern/procx.h` (WP-FILE/PROC: `ProcInfoTeardown`, `struct TProcInfo` forward-declared) exist with only those declarations. Each WP owns its header.
4. **Teardown order in `~CAddressSpace`:** `ProcInfoTeardown (this)` runs just **before** the spawn record (`CProcess`) is marked done, so WP-FILE/PROC can copy the term reason into it (`GetProcess ()` and `GetExitStatus ()` accessors were added for that). `VmTeardown (this)` runs after `AppCoreReleaseAS` / `V3DReleaseAS` and **before** the page tables and frames are freed. It is not called when an app core failed to stop: that path already leaks the whole space on purpose.
5. **The term reason lives in `CAddressSpace`**, not in `TProcInfo`: `SetTermReason (nReason, nCode)`, `GetTermReason ()` (default `KAPI_PROC_EXITED`) and also `GetTermCode ()`. It only records. WP-0 added the `Fault` line itself (`sys/el0.cpp`: `SetTermReason (KAPI_PROC_FAULT, EL0_FAULT_STATUS)`, which is −11), so WP-FILE/PROC does not edit `el0.cpp`. The kill and OOM paths are still to be added by their WPs.
6. **The PC simulator does NOT leave new fields 0.** The host tables (`tools/tests/desktop_sim/fakekapi.cpp`, `pc/Koton`, `pc/Jet`, `pc/macOS`, `tools/tests/gpucomp/hostkapi.cpp`, `tools/tests/wtkhost/host_kapi.h`) fill every slot with an `unimplemented` that exits. WP-0 adds one line after each fill that zeroes the slots from `vm_map` on, so the `kapi.h` wrappers return `-KAPI_ENOSYS` there. A host that wants to implement a v75 call assigns its slot after that line.
7. **`IoWaitAddTickHook` returns `boolean`** (FALSE when its 8 hooks are taken) instead of `void`. In `IoWait`, `KAPI_WAIT_FOREVER` means no limit, 0 means only check, and longer timeouts are clamped to 30 min. `IoWake` pulses the event only when someone waits.
8. **`HANDLE_OFILE = 6`** is routed in `HandleObjectClose` (`sys/handle.cpp`) to `OFileClose`. `CStream::PollMask` is virtual with the default `KAPI_POLLIN | KAPI_POLLOUT`, and `kern/stream.h` now includes `kern/kapi_abi.h`.
9. **`KAPI_ESRCH 3`** is in the error list. `struct kapi_proc_status`'s fields are commented as the exit status (FAULT −11, KILLED and OOM −9), the reason, the pid and a reserved field.

---

### 3.1 WP-MEM (kernel): demand paging, mmap, TLS, stacks

**Owned files:** `sys/vm.cpp` (new), `kern/vm.h` (new), `mm/addrspace.cpp/.h` (Sbrk, MapStack, teardown hook), `sys/uaccess.cpp`, `sys/el0.cpp` (fault path, unpin after a system call), `arch/aarch64/exception.cpp` (`SyncHandlerEL1` safety net only), `sys/thread.cpp` (`thread_create_ex`, `thread_info`, WordPhys), `sys/appcore.cpp` (page-in, TLS), `kernel.cpp` (main stack VMA, the pager task); `user/bin/memtest.c`.

#### Address space

| Range | Use |
|---|---|
| ELF image at 8 GB | **eager**, as today (app-core jobs touch static arrays) |
| Heap `USER_HEAP_BASE` (10 GB)…12 GB | VMA kind HEAP, **lazy** |
| Main stack `[16 GB − size, 16 GB)` | VMA kind STACK, lazy |
| Thread stacks: slot `32 GB + (rec + 1) × 32 MB`, top of the slot | VMA STACK, lazy; the rest of the slot is unmapped (the guard) |
| **mmap arena `USER_MMAP_BASE` = 34 GB (0x8_8000_0000) … `USER_MMAP_END` = `USER_VA_END` (60 GB)** | 26 GB, only `vm_map` places mappings here. The thread slots end exactly at 34 GB (64 records × 32 MB) |
| Canvas, wallpaper, surfaces, kapi pages, code arena, full-screen buffers, sound ring, `gpu_vbuf` | VMA kind FIXED: **eager**, never touched by `vm_*` (refused with `-EINVAL`) |

`[16 GB, 32 GB)` stays unused (reserve).

#### Kernel structures (`kern/vm.h`)

```cpp
struct TVma { u64 ulStart, ulEnd; u16 nProt; u16 nKind; u32 nFlags; };   // sorted, non-overlapping
struct TVmSpace {
	TVma    *pVma; unsigned nVma, nCap;        // binary-searched array, grows by doubling, cap 4096 (-ENOMEM)
	TVmPin   Pin[32]; unsigned nPins;          // { CTask *pTask; u64 ulStart, ulEnd; }  kapi in-place ranges
	TVmZap  *pZapPending;                      // ranges unmapped while pinned (deferred)
	u64      nFaults, nResident;               // statistics
	u16      L3Count[L2 slots of the user range];   // valid PTEs per L3 table (free empty L3s: SHOULD)
	boolean  bEagerHeap;                       // set by core_acquire
};
boolean VmFaultIn (CAddressSpace *pAS, u64 ulVA, boolean bWrite, int *pErr);  // software walk, NEVER yields
```

#### Faults

**EL0** (`El0SyncHandler`, EC 0x24 data abort or 0x20 instruction abort from EL0; DFSC/IFSC from the ESR):
- **Translation fault, levels 1–3** (`0b0001xx`), with FAR in a lazy VMA whose prot allows the access (WnR → `PROT_WRITE`; read → `READ`; instruction → never, `EXEC` is not supported):
  - `VmFaultIn`: `palloc_high`, `memset` 64 KB, `MapPage` (owned, AP from the VMA's prot), `dsb ishst`;
  - return; the `eret` retries the access.
  - **No TLBI:** invalid entries are never cached.
- **Permission fault** (`0b0011xx`) where the VMA now allows the access (a spurious fault after an upgrade): `tlbi vale1, va|asid` locally, return.
- **Anything else:** `Fault()` as today. Its kmsg says "stack overflow" when FAR is within 1 MB below a STACK VMA, "PROT_NONE access" when FAR is in a VMA with prot 0, and "out of memory" for OOM.
- **The handler does not yield in v1.** It does no I/O, and zeroing 64 KB takes about 10 µs. Core 0 runs the kernel non-preemptively, so two threads faulting the same page are serialized: the second one finds the PTE valid and returns.

**EL1, kapi side** (`sys/uaccess.cpp`):
- `Probe` (`UserReadable` / `UserWritable`): translate with **`AT S1E0R/S1E0W`** instead of `S1E1R/W`, so EL0 permissions are checked (a `PROT_NONE` page is EL1-accessible: AP `RW_EL1`). On failure, `VmFaultIn` the page and check again. Then **pin** `[p, p + n)` for the calling task (`TVmSpace::Pin`).
- `El0SyncHandler`'s `Syscall()` drops the task's pins when the kapi returns (`VmUnpinTask`), and runs the deferred zaps that no pin covers any more.
- `UserCopyIn`/`UserCopyOut`/`UserStrOut`/`CUserStr`: when `UAccessCopy` fails, `VmFaultIn` every page of the range in C (not in the exception) and retry once. A fault in the routines themselves still goes through the fixup table unchanged (point 3 of §0).
- `WordPhys` (futex): `AT S1E0R`; populate first. **`WordWaitsZap (phys, len)`** wakes the waiters on a frame about to be freed (spurious wakes are allowed).
- **Safety net (SHOULD):** in `SyncHandlerEL1`, a fault at a user VA by a task with an address space, outside the fixup table, inside a lazy VMA → unmask the FIQ (`msr daifclr, #1`, as `IrqEntry` does), `VmFaultIn`, log once `vm: kernel touched an unpopulated user page at pc %lx`, and return. Otherwise panic as today.

**App cores** (`sys/appcore.cpp`): the decision is **pre-populate, plus a correct but slow fallback; no allocator on cores 2–3.**
- `core_acquire` sets `bEagerHeap`: from then on `Sbrk` populates what it maps. This protects the existing emulators, which `malloc` on the main thread and touch on the core.
- `core_run` pre-populates `[stack_top − 256 KB, stack_top)` (within its VMA).
- `emucore.h` and `onyx_rpc` users call `vm_advise (WILLNEED)` on their buffers.
- Fallback: `AppCoreOnEl0Sync` on a translation fault at a user VA stores `{ulPageInVA, bWrite}` in its `TAppCore`, sets `nPageIn = 1`, `dsb ish; sev`, then waits in `wfe` with IRQs enabled (the stop IPI still drops the job). It returns to retry when `nPageIn == 2`, and turns the job into `CORE_FAULT` when it is 3.
- Core 0: a tick hook (`IoWaitAddTickHook (AppCorePageInTick)`) sets the event of a kernel **pager** task (created at boot in `kernel.cpp`). The pager runs `VmFaultIn` for the owner's space by a **software table walk** (no `AT`: its `TTBR0` is the kernel's), stores 2 or 3, then `dsb ish; sev`. Latency ≤ 10 ms plus one scheduling.
- SHOULD: an SGI from the app core to core 0 for an immediate wake.

#### TLB rules
- invalid → valid: `dsb ishst` only.
- valid → invalid (`unmap`, DONTNEED) or a permission change (`protect`): write the PTE, `dsb ishst`, `tlbi vae1is, (va >> 12) | (asid << 48)` per page (`tlbi aside1is` above 64 pages), `dsb ish; isb`, and **only then** free the frames. Inner-shareable reaches cores 2–3 and the network core.
- Freeing an L3 table of a live space (SHOULD): `tlbi vmalle1is` (walk caches).
- Permission changes do not need break-before-make; output-address changes never happen.

#### Pins and the deferred zap
- `vm_unmap`, DONTNEED and a protect downgrade that overlap another task's pin: the VMA change happens at once (new faults see the new state); the PTEs and frames under the pin stay until the pin is released, then they are zapped (`pZapPending`).
- No waiting, so no deadlock, and the kernel never touches a freed frame. A kapi of the same task that pinned the range is not affected (its pins end with its call).

#### OOM policy: heuristic overcommit
- No commit accounting.
- A single `Sbrk` growth or writable `vm_map` (without `MAP_NORESERVE`) larger than the free app pool (the high-zone free counters `ram_detail` reads) minus `VM_RESERVE` (16 MB) fails with `-ENOMEM` (sbrk returns −1 as today). Apps that check `malloc` keep working.
- `VmFaultIn` refuses when the free pages fall under `VM_RESERVE`:
  - at EL0, the faulting process is killed: kmsg `vm: <name> (pid N) killed: out of memory (page fault at %lx, %u KB resident)`, `IpcNotify ("Application error", "<name> ran out of memory")`, `SetTermReason (KAPI_PROC_OOM, -9)`, `kapi_exit`;
  - in a kapi, the call fails (`-ENOMEM` / its error value);
  - on an app core, `CORE_FAULT`.
- SHOULD: a per-process limit, `app.txt` `memlimit = 1G`.
- On a 1 GB Pi `palloc_high` falls back to the low pager, so the reserve also protects the page tables.

#### Page-table cost (64 KB granule)
- One L3 table (64 KB, from the **low** pager, `GetOrCreateL3`) per 512 MB slot that has at least one present page. That is about 6 per process (image, heap, canvas, kapi, stack, threads), at most 104 for the user range (6.5 MB).
- Reservations cost nothing until touched.
- The 64 KB granule costs at least 64 KB resident per touched page: a thread's first stack page, a lone malloc page.

#### Other changes
- **Lazy main stack:** `kernel.cpp` `CUserProcessTask::Run` replaces `MapStack (USER_STACK_TOP, nUserStack)` with a STACK VMA. `AppUserStack`'s default becomes **8 MB** (lazy: it costs nothing); the maximum stays 64 MB.
- **Lazy thread stacks:** `kapi_thread_create` replaces `MapStack (ulTop, nStackSize)` with a STACK VMA (the old ABI benefits too). A reused slot whose size changes has its VMA replaced and its pages zapped. SHOULD: zap an ended thread's stack in `kapi_thread_exit`.
- **Sbrk:** grows the HEAP VMA (lazy unless `bEagerHeap`). A shrink zaps the pages above the new break (respecting pins).
- **TLS:** see §0.1. `CUserThreadTask::Run` writes `msr tpidr_el0, m_ulTls` before `El0Enter`. `kapi_core_run` reads the caller's `TPIDR_EL0` (`mrs`: the kernel never changes it) and the core's loop writes it before `El0Enter`, so a job shares its caller's TLS (errno included). The main thread's value is set by `crt0posix` at EL0.

#### ABI (slots 199–206)

```c
#define KAPI_PROT_NONE 0
#define KAPI_PROT_READ 1
#define KAPI_PROT_WRITE 2
#define KAPI_PROT_EXEC 4            /* refused: -KAPI_ENOTSUP */
#define KAPI_MAP_FIXED 0x10
#define KAPI_MAP_NORESERVE 0x4000
#define KAPI_MAP_POPULATE 0x8000
#define KAPI_MAP_FIXED_NOREPLACE 0x100000
#define KAPI_MADV_NORMAL 0
#define KAPI_MADV_RANDOM 1
#define KAPI_MADV_SEQUENTIAL 2
#define KAPI_MADV_WILLNEED 3
#define KAPI_MADV_DONTNEED 4
#define KAPI_MADV_FREE 8
#define KAPI_VMK_ANON 1             /* vm_map */
#define KAPI_VMK_HEAP 2
#define KAPI_VMK_STACK 3
#define KAPI_VMK_IMAGE 4
#define KAPI_VMK_FIXED 5            /* canvas, surface, sound ring, GPU memory, code arena, kapi pages */
#define KAPI_VMF_LAZY 1
#define KAPI_THREAD_DETACHED 1      /* no join: the kernel frees its record when it ends */

struct kapi_vm_region {             /* 32 bytes */
	unsigned long long start, end;  /* 0, 8: [start, end), 64 KB-aligned */
	unsigned prot;                  /* 16: KAPI_PROT_* */
	unsigned kind;                  /* 20: KAPI_VMK_* */
	unsigned resident;              /* 24: pages present */
	unsigned flags;                 /* 28: KAPI_VMF_* */
};
struct kapi_vm_stats {              /* 48 bytes */
	unsigned long long resident;    /* 0: bytes of owned frames, page tables included */
	unsigned long long lazy;        /* 8: bytes of VA in lazy regions */
	unsigned long long writable;    /* 16: bytes of writable VA (all lazy regions touched) */
	unsigned long long faults;      /* 24: pages filled on demand (EL0 + kernel + app cores) */
	unsigned long long pt_bytes;    /* 32: page tables */
	unsigned long long limit;       /* 40: per-process limit, 0 = none */
};
struct kapi_thread_attr {           /* 64 bytes */
	unsigned long long fn;          /* 0: int (*) (void *) */
	unsigned long long arg;         /* 8 */
	unsigned long long stack_size;  /* 16: 0 = 8 MB; 16 KB..16 MB (lazy) */
	unsigned long long tls;         /* 24: the thread's initial TPIDR_EL0 */
	const char *name;               /* 32: may be 0; 31 characters kept */
	unsigned flags;                 /* 40: KAPI_THREAD_DETACHED */
	int prio;                       /* 44: 0, or 1 = "real time" (as thread_priority) */
	unsigned long long reserved[2]; /* 48: 0 */
};
struct kapi_thread_info {           /* 32 bytes */
	unsigned long long stack_lo;    /* 0: lowest usable byte of its stack VMA */
	unsigned long long stack_hi;    /* 8: its top (the initial SP) */
	int tid;                        /* 16 */
	int state;                      /* 20: 0 running, 1 ended (joinable) */
	unsigned long long guard;       /* 24: unmapped bytes below stack_lo */
};

/* --- v75 WP-MEM --- (slots 199..206) */
long long (*vm_map) (unsigned long long addr, unsigned long long len, unsigned prot, unsigned flags);
	/* -> the address (>= USER_VA_BASE), -EINVAL (len 0, FIXED not aligned / outside the arena),
	   -ENOMEM (no room / heuristic overcommit / > 4096 regions), -ENOTSUP (EXEC),
	   -EEXIST (FIXED_NOREPLACE overlaps). len rounded up to 64 KB; addr a hint unless FIXED;
	   POPULATE fills now (yields every 64 pages). Zero-filled. */
int (*vm_unmap) (unsigned long long addr, unsigned long long len);
	/* -> 0 / -EINVAL (not inside KAPI_VMK_ANON regions); splits */
int (*vm_protect) (unsigned long long addr, unsigned long long len, unsigned prot);
	/* ANON regions -> 0 / -EINVAL / -ENOMEM (split cap) / -ENOTSUP (EXEC); present pages re-protected
	   + TLBI */
int (*vm_advise) (unsigned long long addr, unsigned long long len, int advice);
	/* any lazy region: WILLNEED populates (-ENOMEM); DONTNEED / FREE zap (zero on the next touch;
	   ANON and HEAP only); the others no-op -> 0 / -EINVAL */
int (*vm_query) (unsigned long long addr, struct kapi_vm_region *out);
	/* -> 0 the region holding addr, 1 the next region above it, -ENOMEM none above, -EFAULT */
int (*vm_stats) (int pid, struct kapi_vm_stats *out);
	/* pid 0 = self -> 0 / -ESRCH(3) / -EFAULT */
int (*thread_create_ex) (const struct kapi_thread_attr *attr);
	/* -> tid >= 2 / -EAGAIN (32 running) / -ENOMEM / -EINVAL / -EFAULT */
int (*thread_info) (int tid, struct kapi_thread_info *out);
	/* tid 0 = self, 1 = main -> 0 / -ESRCH / -EFAULT */
```

(`ESRCH` is 3 in newlib: add `KAPI_ESRCH 3`.)

#### Tests and acceptance

`/bin/memtest` (freestanding, kapi level), each check prints PASS/FAIL:
- lazy `vm_map`: resident 0 before, 1 page after one touch;
- 1 GB `PROT_NONE` reservation, then commit 64 KB with `vm_protect`, then write;
- partial unmap in the middle (split): `vm_query` shows 2 regions;
- DONTNEED zero-fill; `FIXED` / `FIXED_NOREPLACE`;
- a child (spawned with an argument) writes a READ page → `proc_wait` reason FAULT; a child overflows its 1 MB thread stack → FAULT "stack overflow";
- in-place kapi I/O into fresh lazy memory: `kapi_read` of a 1 MB file, `tcp_recv`, `get_args`;
- futex on a lazy page; unmap of a buffer while another thread blocks in a `kapi_read` into it → no panic (deferred zap);
- app-core job touching unpopulated memory (fallback page-in, timed);
- TLS: two threads `msr tpidr_el0` distinct values, sleep and yield 1000×, check; an app-core job sees its caller's value;
- `thread_create_ex` / `thread_info` bounds contain a local's address;
- OOM: a child that touches until killed → reason OOM, the system alive, `memmon` back.

Also run `tools/el0scan.sh` (no new instructions in user code).

**On the Pi:**
- `memtest`, `threadtest`, `futextest`, `coretest`, `el0test`, `faulttest`;
- every emulator that uses an app core (gb, gba, nes, snes, n64, gc), Doom (the RPC on an app core), Jet, Writer/Spreadsheet, Media, Mail, Photos;
- `ps`/`memmon`: page counts should **drop** (lazy stacks: 1–64 MB less per app);
- a kill under load.

**Risks:**
- a kernel in-place access that bypasses the helpers → panic (mitigated by the audit `grep` for app pointers used without `User*`, and by the safety net);
- performance of page-ins (64 KB zeroing on first touch of the heap);
- emulator regressions on app cores (`bEagerHeap`);
- deferred-zap bookkeeping;
- futex waiters on zapped frames.

#### WP-MEM resolutions (what landed; docs/02 §4 *Demand paging* and §8 "v75: memory")

The ABI above is unchanged. Where the implementation differs from the text above:

1. **`VmFaultIn`** returns an `int`: 1 filled, 0 already present and allowed, −`EFAULT` (no lazy region), −`EACCES` (the protection), −`ENOMEM` (the reserve) — not `boolean` + `*pErr`.
2. **`TVmSpace`**: one pin per **task** (the union of what its call probed; 40 slots: a task has one call in progress), not 32 ranges. The deferred work is marked **in the PTE** (software bits 56 `ZAP` and 57 `SYNC`, next to the owned bit 55) plus a list of ranges to look at; a range is settled when no pin overlaps it. No `L3Count`: empty L3 tables are not freed (the SHOULD; at most 104 per process). No `nResident`: `vm_stats.resident` is the space's owned pages (frames + its page tables).
3. **`PROT_NONE` pages that are present** are mapped `AP = RO_EL1` (EL1 read-only, EL0 nothing), not `RW_EL1`: no kernel write can reach them. The probes (`AT S1E0*`) refuse them; a fault-safe **copy-in** from one still succeeds (the copies use plain `LDR`, kern/uaccess.h) — the app's own memory, not a leak.
4. **Deferral of a protection change** happens only when the page loses its **write** access under another task's pin (a kapi writing there in place); other changes apply at once.
5. **`vm_unmap`** accepts holes inside the arena (as `munmap`); `-EINVAL` only for an unaligned address or a range outside the arena. **`vm_advise`** with `len` 0 → 0. **`vm_map (POPULATE)`** does not report a fill failure (as Linux).
6. **The app pool** for the reserve and the overcommit check is the high zone **plus** the low pager (where `palloc_high` falls back); the reserve is 16 MB of the total.
7. **App cores**: `core_acquire` fills the heap that **already exists** too (not only its later growth: the emulators allocate before they acquire), so `emucore.h` / `onyx_rpc` needed no `WILLNEED`. The **pager task** is made at the first `core_acquire` (not at boot: no `kernel.cpp` change), with its tick hook; one request fills up to 8 pages of the region. No SGI.
8. **Stacks**: `CAddressSpace::MapStack` itself became lazy (`VmMapStack`), and `EL0_USTACK_MIN` is now 8 MB (the default and the minimum: an `app.txt` `stack` below it gets 8 MB) — so `kernel.cpp` is untouched. An ended thread's stack pages are dropped in `thread_exit` (the SHOULD). `thread_create` keeps its 256 KB default (lazy); `thread_create_ex`'s is 8 MB.
9. **Regions noted but eager**: `MapContig` (canvases, surfaces, the sound ring, the kapi pages, the full-screen buffers), the code arena and the ELF loader note `FIXED` / `IMAGE` regions, so `vm_query` shows the whole map.
10. **Descriptors** (`MapPage`, `GetOrCreateL3`) are now built aside and stored as one 64-bit word (an app core may walk the table meanwhile), the zeroed frame or table made visible first (`DSB ISHST`).
11. **The EL1 safety net** (the SHOULD) is in: `VmKernelFault`, after `UAccessFixup`; its kmsg line is written at the next system call's end (not in the exception).
12. **`/bin/memtest`** runs the tcp_recv check only with `memtest net` (it needs the network), and the real OOM kill only with `memtest oom` (it takes the whole app pool for a moment): by default it checks the overcommit refusal instead (bounded). The children's term reason is checked through `proc_wait` when WP-FILE/PROC has landed, else their exit status (−11, −9) through `wait`.
13. **Pi round 1: the "leak" of `memtest oom`** (~1.1 GB of `meminfo`'s free memory per OOM kill) was Circle's `CPageAllocator::Allocate` (`circle/lib/pageallocator.cpp`): it advances `m_pNext` before it sees the region is full and returns 0 without stepping back, so every failed attempt leaves the bump pointer one page past `m_pLimit`, and `GetFreeSpace ()` (`m_pLimit - m_pNext` as a `size_t`) wraps. `palloc_high` tries the high segments in order: once segment 0 is full, every page served by another segment or the low pager adds one page of overshoot to it. No frame was lost (an exhausted region's pages are all handed out, freed ones go to its free list), but the counters `meminfo`, `ram_detail` and `VmPoolFree` read fell for ever, and the OOM check fired with about half the memory still free. **Fix:** one line in the Circle fork (`m_pNext -= PAGE_SIZE;` before that `return 0`, docs/05 §24); the interim Onyx-side repair (`VmPagerRepair`) was removed. `memtest` now also checks that a child touching 256 MB (512 MB with `oom`) and exiting normally gives it all back, and runs the OOM kill twice, each ending where it started.

---

### 3.2 WP-FILE/PROC (kernel): file descriptors, stat, pipes, environment, spawn/wait, clock

**Owned files:** `sys/ofile.cpp` (new), `sys/procx.cpp` (new), `sys/stream.cpp`/`kern/stream.h` (pipe), `sys/ramfs.cpp`/`kern/ramfs.h` (random access, deferred delete), `sys/kapi.cpp` (kill reasons in `kapi_kill`/`kapi_kill_pid`, `kapi_spawn` env inheritance), `kernel.cpp` (`SpawnProcess`/`ExecPath`/`LaunchApp`: env and argv blocks; boot: the default environment and the cleanup of hidden deleted files), `sys/el0.cpp` (one line: `SetTermReason (KAPI_PROC_FAULT, -11)` in `Fault`; coordinate with WP-MEM or put it in WP-0); `user/bin/filetest.c`.

#### Open files (`sys/ofile.cpp`)

- **`CFileNode`**, one per open file:
  - key: the absolute path from `ResolvePath`, upper-cased (FAT and `RAM:` are case-insensitive);
  - kind FATFS or RAMFS (a VFS provider path: `-ENOTSUP` in v1);
  - **one `FIL`** for all its openers: opened `FA_READ`, or re-opened `FA_READ|FA_WRITE` when the first writer comes; the offsets are per description, so re-opening is invisible;
  - a reference count; a sleeping lock (a busy flag + `Yield` loop, as `sys/fslock.cpp`) held across one call, because FatFs calls yield (`OnyxDriverWait`);
  - `bDeleteOnClose` + its hidden name.
  - One `FIL` per file avoids FatFs' double-open corruption (`FF_FS_LOCK 0`) and keeps the sector caches coherent.
- **`COpenFile`**, the `HANDLE_OFILE` object: node, 64-bit offset, access, `bAppend`. **`dup` is user-space** (libc counts references to descriptions), so it needs no kernel call.
- Read and write: `f_lseek (offset)` then `ChunkedRead` / `ChunkedWrite` (64 KB pieces with `Yield`, reused from `sys/kapi.cpp`) after `UserWritable` / `UserReadable`. `O_APPEND` writes at the node's size. A short `f_write` → `-ENOSPC` if nothing was written.
  - **Never use FatFs fast seek (`CREATE_LINKMAP`, as `kapi_seek` does) on a writable node:** FatFs cannot expand a file in that mode.
- `O_CREAT|O_EXCL` → `FA_CREATE_NEW` (`FR_EXIST` → `-EEXIST`). `O_TRUNC` (writable only) → `f_truncate` at 0. A directory → `-EISDIR` (libc gives `open (dir, O_RDONLY)` a directory fd of its own, which SQLite's directory `fsync` accepts).
- `file_truncate` growing writes zeros in 64 KB chunks (FatFs leaves the extension undefined). `file_sync` → `f_sync`.
- **Unlink while open:** rename the file to `<dir>/.~onyx-deleted-<n>` and delete it at the last close. Leftovers are removed at boot. **Rename onto an open target:** the same hiding first, then `f_rename`. Rename of an open source updates the node's key.
- **stat:**
  - `st_ino` = FNV-1a-64 of the key; `st_dev` = volume (1 `SD:`, 2–4 `SD1:`–`SD3:`, 5+ `USB:`…, 64 `RAM:`);
  - mode `S_IFREG 0644` / `S_IFDIR 0755`, and `0444` with the read-only attribute;
  - `mtime` from FatFs `fdate`/`ftime` (local time) converted to UTC with the current `CTimer` zone; `RAM:` keeps a write time;
  - `blksize` = the cluster size; root paths (`SD:/`) are synthesized as directories.
- FRESULT → errno:

  | FRESULT | errno |
  |---|---|
  | `NO_FILE`/`NO_PATH` | `ENOENT` |
  | `EXIST` | `EEXIST` |
  | `DENIED` | `EACCES` (`ENOTEMPTY` for a directory that is not empty) |
  | `WRITE_PROTECTED` | `EROFS` |
  | `INVALID_NAME` | `EINVAL` / `ENAMETOOLONG` |
  | `DISK_ERR`/`INT_ERR`/`NOT_READY` | `EIO` |
  | `LOCKED` | `EBUSY` |
  | `TOO_MANY_OPEN_FILES` | `EMFILE` |
  | `NOT_ENOUGH_CORE` | `ENOMEM` |
  | `INVALID_DRIVE`/`NOT_ENABLED`/`NO_FILESYSTEM` | `ENODEV` |

- `RAM:`: add `RamFsPRead/PWrite/Truncate/Stat` and a deferred delete to `sys/ramfs.cpp` (the same semantics).
- The old kapis (`open`/`read`/`save_file`/`file_out`) keep their own `FIL`s. Mixing them with `file_*` on the same file is not coherent (documented).

#### Pipes
- `CPipeStream::PollMask`: IN when not empty, or HUP|IN when the write end is closed; OUT when there is room.
- `IoWake ()` in `Write`, `Read` (room appeared) and `CloseWrite`.
- `stream_write_nb`: `-EAGAIN` when full.
- `PIPE_CAP` stays 8 KB (64 KB: SHOULD).
- `EPIPE` without readers is not detected (the reference count cannot tell readers from writers). A blocking write to a full pipe nobody drains waits, as today.

#### Processes (`sys/procx.cpp`)
- `TProcInfo`: an env block and an argv block (kernel heap, ≤ 64 KB each: strings separated by NUL, ended by an empty string) and the term reason.
- Inheritance: `kapi_spawn` / `exec` / `exec_as` children get the parent's **initial** env block. Desktop-launched apps get the system default, read at boot from `SD:/etc/environment` (`KEY=VALUE` lines). Without it: `HOME=SD:/home`, `PATH=SD:/bin`, `TMPDIR=RAM:/tmp`, `LANG=C.UTF-8`.
- `spawn_ex` passes argv and envp explicitly. The old `get_args` string of such a child is argv[1..] joined with spaces, an argument containing spaces wrapped in double quotes.
- Term reasons:
  - `Fault` → `KAPI_PROC_FAULT` (code −11);
  - `kapi_kill` / `kapi_kill_pid` and the cascade of a dead parent → `KAPI_PROC_KILLED` (−9). Today a killed process reports status 0 (`m_nExitStatus` default, `~CAddressSpace`);
  - OOM (WP-MEM) → `KAPI_PROC_OOM`.
- `proc_wait` blocks on an event set at the child's end instead of `kapi_wait`'s `MsSleep (5)` loop (SHOULD).
- `clock_info`: `CNTPCT` / `CNTFRQ` and `CTimer::GetUniversalTime ()` sampled together (±10 ms). libc rebases its realtime clock every 60 s, which follows NTP.
- `sleep_us`: under 1000 µs, a `Yield` loop on `GetClockTicks`; otherwise `usSleep`.

#### ABI (slots 207–228)

```c
#define KAPI_O_RDONLY 0
#define KAPI_O_WRONLY 1
#define KAPI_O_RDWR 2
#define KAPI_O_ACCMODE 3
#define KAPI_O_CREAT 0x40
#define KAPI_O_EXCL 0x80
#define KAPI_O_TRUNC 0x200
#define KAPI_O_APPEND 0x400
#define KAPI_SEEK_SET 0
#define KAPI_SEEK_CUR 1
#define KAPI_SEEK_END 2
#define KAPI_S_IFMT 0170000
#define KAPI_S_IFDIR 0040000
#define KAPI_S_IFREG 0100000
#define KAPI_UNLINK_DIR 1           /* rmdir semantics */
#define KAPI_WAIT_NOHANG 1
#define KAPI_WAIT_KEEP 2            /* do not close the process handle */
#define KAPI_PROC_EXITED 0
#define KAPI_PROC_FAULT 1
#define KAPI_PROC_KILLED 2
#define KAPI_PROC_OOM 3
#define KAPI_CLOCK_REALTIME_VALID 1 /* the date is real (NTP / RTC), not "since boot" */

struct kapi_stat {                  /* 64 bytes */
	unsigned long long size;        /* 0 */
	long long mtime;                /* 8: UTC seconds since 1970 */
	unsigned long long ino;         /* 16: FNV-1a 64 of the upper-cased absolute path */
	unsigned mode;                  /* 24: KAPI_S_IF* | permission bits */
	unsigned dev;                   /* 28: volume number */
	unsigned blksize;               /* 32: cluster size (RAM: 65536) */
	unsigned attr;                  /* 36: FAT attributes (1 RO, 2 HID, 4 SYS, 0x10 DIR, 0x20 ARC) */
	unsigned long long blocks;      /* 40: 512-byte blocks allocated */
	long long ctime;                /* 48: = mtime (FF_FS_CRTIME 0) */
	unsigned long long reserved;    /* 56 */
};
struct kapi_dirent2 {               /* 288 bytes */
	char name[256];                 /* 0: up to 255 characters (kapi_dirent cut at 127) */
	unsigned long long size;        /* 256 */
	long long mtime;                /* 264 */
	unsigned mode;                  /* 272 */
	unsigned attr;                  /* 276 */
	unsigned long long ino;         /* 280 */
};
struct kapi_spawn_attr {            /* 64 bytes */
	const char *path;               /* 0: the program (resolved against cwd) */
	const char *argv;               /* 8: block "a\0b\0\0" (argv[0] first), <= 64 KB */
	const char *envp;               /* 16: same format; 0 = the caller's initial environment */
	const char *cwd;                /* 24: 0 = the caller's */
	void *in, *out;                 /* 32, 40: stream handles of the caller, or 0 */
	unsigned long long reserved;    /* 48: 0 (a future stderr) */
	unsigned flags;                 /* 56: 0 */
	unsigned reserved2;             /* 60 */
};
struct kapi_proc_status {           /* 16 bytes */
	int code; int reason; int pid; int reserved;
};
struct kapi_clock_info {            /* 48 bytes */
	unsigned long long cnt;         /* 0: CNTPCT_EL0 at the sample */
	unsigned long long freq;        /* 8: CNTFRQ_EL0 */
	long long utc_us;               /* 16: UTC microseconds since 1970 at cnt */
	int tz_minutes;                 /* 24: local - UTC (set_timezone) */
	unsigned flags;                 /* 28: KAPI_CLOCK_* */
	unsigned long long boot_cnt;    /* 32: CNTPCT at boot */
	unsigned long long reserved;    /* 40 */
};

/* --- v75 WP-FILE/PROC --- (slots 207..228) */
long long (*file_open) (const char *path, unsigned flags, unsigned mode);  /* -> handle > 0 / -errno */
long long (*file_read) (long long h, void *buf, unsigned long long len, long long off);
	/* off -1: at the handle's offset (advanced); else pread -> bytes, 0 = end / -errno */
long long (*file_write) (long long h, const void *buf, unsigned long long len, long long off);
	/* off -1: at the offset (or the end with APPEND) */
long long (*file_seek) (long long h, long long off, int whence);  /* -> new offset / -EINVAL / -EBADF */
int (*file_truncate) (long long h, long long size);               /* grow with zeros */
int (*file_sync) (long long h);
int (*file_stat) (long long h, struct kapi_stat *out);
int (*file_close) (long long h);
int (*path_stat) (const char *path, struct kapi_stat *out);
int (*path_unlink) (const char *path, unsigned flags);
	/* file: -EISDIR on a directory; KAPI_UNLINK_DIR: -ENOTDIR / -ENOTEMPTY */
int (*path_mkdir) (const char *path, unsigned mode);              /* -EEXIST / -ENOENT (parent) */
int (*path_rename) (const char *from, const char *to);            /* replaces to; -EXDEV across volumes */
int (*path_utime) (const char *path, long long mtime);
int (*dir_read) (void *dir, struct kapi_dirent2 *out);
	/* an opendir handle -> 1 / 0 end / -EBADF / -EFAULT */
int (*stream_write_nb) (void *h, const void *buf, unsigned len);   /* -> n (> 0) / -EAGAIN / -EBADF */
long long (*spawn_ex) (const struct kapi_spawn_attr *a);          /* -> process handle / -errno */
int (*proc_wait) (void *proc, unsigned flags, struct kapi_proc_status *out);
	/* -> 1 ended (handle closed unless KEEP) / 0 running (NOHANG) / -EBADF */
int (*get_argv) (char *buf, unsigned cap);  /* -> the block's size (filled up to cap); argv[0] = the path */
int (*get_env) (char *buf, unsigned cap);   /* -> the block's size */
int (*getpid) (int which);                  /* 0 pid, 1 parent pid */
int (*clock_info) (struct kapi_clock_info *out);
int (*sleep_us) (unsigned long long us);
```

#### Tests and acceptance

`/bin/filetest`, run on `SD:` and on `RAM:`:
- the open-flag matrix; pread/pwrite at random offsets compared with a model (10 000 operations, two handles);
- `O_APPEND` from two handles; truncate grow (zeros) and shrink; sync;
- stat fields (mtime within 2 s of `clock_info`);
- unlink while open (still readable, gone from the listing, no leftover after close);
- rename replace; mkdir/rmdir `ENOTEMPTY`; 200-character names in `dir_read`;
- a 64 MB write/read with the MB/s printed;
- `write_nb` on a full pipe → `-EAGAIN`, then a reader thread drains it; HUP after `stream_eof`;
- `spawn_ex` of itself with argv/env → child checks → exit 42 → `proc_wait` (42, EXITED); a child that faults → FAULT; NOHANG;
- `clock_info` against `get_datetime`; `sleep_us (1000/5000/20000)` statistics.

**On the Pi:**
- `filetest`, then a reboot (no `.~onyx-deleted-*` left);
- `fsbench` unchanged;
- the terminal's pipes and redirections, cp/mv/rm, the file manager, Writer save/load (old kapis);
- a USB stick if present.

**Risks:**
- FatFs performance for many small `pwrite`s (SQLite pages) with `f_lseek` cluster-chain walks: measure;
- old and new kapis on one file;
- hidden-file cleanup after a crash;
- time zone in `mtime`.

#### WP-FILE/PROC resolutions (what was implemented; docs/02 §8 "v75: files and processes")

The ABI of slots 207–228 is unchanged. Where the code differs from the text above:

1. **The hidden files** of an unlink while open are `<volume>:/.~onyx-deleted/<n>` (one hidden
   folder per volume, removed once empty), not `<dir>/.~onyx-deleted-<n>`: the boot cleanup empties
   one folder per volume (`SD:`–`SD3:`; a volume mounted later at its first hide) instead of walking
   the whole card. `dir_read` does not list that folder.
2. **A file renamed or hidden while open** has its `FIL` closed, the entry moved, then re-opened: FatFs
   keeps the location of the directory entry in the `FIL` (`dir_sect`, exFAT's `c_scl` / `c_ofs`).
   A folder renamed while files in it are open: their nodes' paths follow.
3. **`RAM:`'s `ino`** is the node's own number (unique, kept by a rename), not the path's hash.
4. **`proc_wait` with `KAPI_WAIT_NOHANG`** on a running child returns 0 **and fills `pid`** (`reason`
   −1); `spawn_ex` returns once the child has its pid (its first time slice, ≤ 1 s). So libc's
   `posix_spawn` gets the pid with `proc_wait (h, NOHANG | KEEP)` right after `spawn_ex`.
5. **`IoWait` (WP-0's `sys/iowait.cpp`) sleeps in no-kill slices of ≤ 100 ms.** A task killed while it
   slept on the shared event stayed on its wait list and the reaper freed it there (the next
   `IoWake` walked freed memory). The interface is unchanged; WP-NET's `poll` benefits too.
6. **Shared files touched** (minimal): `sys/kapi.cpp` (`ResolvePath`, `CurCwd`, `ChunkedRead`,
   `ChunkedWrite` no longer `static`, declared in `kern/ofile.h`; `kapi_opendir` calls
   `OFileNoteDir` so that `dir_read` knows a FatFs `DIR`'s path for the `ino`; `kapi_kill` /
   `kapi_kill_pid` set `KAPI_PROC_KILLED`), `sys/handle.cpp` (`HandlesRunDeferred` calls
   `OFileRunDeferred`: a teardown only queues its open files), `kernel.cpp` (the `TProcInfo` handed to
   `CUserProcessTask`; `SpawnProcess` got a last `TProcInfo *` parameter; the orphan cascade sets
   `KAPI_PROC_KILLED`; `StartAutostart` calls `ProcInfoBootInit` and `OFileBootCleanup`),
   `kern/stream.h` (`CStream::WriteNonBlocking`, `CProcess::nReason` / `nPid`).
7. **Provider paths** (`FTP:`…): `path_unlink`, `path_mkdir`, `path_rename` go to the provider (as the
   old calls); `file_open`, `path_stat`, `path_utime` → `-ENOTSUP`.
8. **An empty argument** cannot be passed in an argv block (it would end the block).
9. **`kapi_wait`** (the old call) now returns −9 for a killed child and −11 for a crash (it returned 0
   for a kill), as `proc_wait`'s `code`.
10. **The tests:** `/bin/filetest` (files and pipes) and `/bin/proctest` (processes and the clock) on
    the Pi; on the PC `tools/tests/run_ofile_test.sh` (the real `ofile.cpp` over the fork's FatFs,
    FAT32 and exFAT, and the real `RAM:`) and the extended `run_ramfs_test.sh`.

---

### 3.3 WP-NET (kernel): BSD sockets and `poll`

**Owned files:** `sys/net.cpp`, `kern/net.h`, `sys/bsdsock.cpp` (new: the kapis and `poll`), the Circle fork (`include/circle/net/socket.h`, `lib/net/socket.cpp`: `boolean AcceptReady (void) const` and `u16 GetForeignPort (void) const`, plus docs/05 §23), `user/bin/nettest.c`, `tools/tests/nettest_peer.py`.

#### Slots

`MAX_SOCKETS` goes to **256** (shared by every process, today 64). `TSocketSlot` gains:
- `nType` (TCP/UDP), `nState` (NEW, BOUND, LISTEN, CONNECTING, CONNECTED, FAILED, CLOSED), `bNonBlock`, `nError` (pending `SO_ERROR`);
- `nLocalPort`, peer address and port, the receive/send timeouts;
- a **carry buffer** (`FRAME_BUFFER_SIZE`, allocated at the first receive): each `Receive` goes into it, the caller gets what fits, the rest stays. This fixes the data loss of small receives and gives `MSG_PEEK`;
- `nStatus`: the readiness snapshot bits.

The old `tcp_*` kapis keep working on the same table (they create CONNECTED / LISTEN TCP slots).

#### Asynchronous connect
- Non-blocking `sock_connect` sets the slot to CONNECTING, posts a **detached** `NR_CONNECT_ADDR` request and returns `-EINPROGRESS`. The worker stores CONNECTED or FAILED + `nError` (Circle errors mapped: refused → `ECONNREFUSED`, timeout → `ETIMEDOUT`, no route → `ENETUNREACH`), then frees the request itself and wakes (`IoWake`, or the generation word on the network core).
- With `netcore=1` this uses the existing core-3 worker pool.
- With `netcore=0`, a **core-0 worker pool** (the same `CNetWorker` code, 2 tasks growing to 16, created at the first asynchronous request) serves the detached requests. Blocking calls keep running directly in the app's task, as today.
- Closing a CONNECTING slot sets the request to `RQ_ORPHAN`; `Finish` already closes what it made.

#### Accept
`sock_accept` without a ready connection (`CSocket::AcceptReady ()`, the Circle patch: any backlog connection `IsConnected`) → `-EAGAIN` when non-blocking; blocking mode waits (`IoWait` loop) and only then calls `Accept`, which no longer blocks.

#### Readiness and `poll`
- **`netcore=1`:** the network core's main loop (`NetCoreMain`), at each turn, computes `nStatus` for every open slot (`GetStatus`, `AcceptReady`, the carry buffer, CONNECTING/FAILED) and bumps a generation word when any changed. Core 0's tick hook (`IoWaitAddTickHook (NetPollTick)`) turns a changed generation into `IoWake`. Latency ≤ 10 ms.
- **`netcore=0`:** `poll` evaluates the sockets directly on core 0 and waits in steps of one tick when sockets are in the set.
- Bits:
  - `POLLIN`: data, carry, FIN, or a pending accept;
  - `POLLOUT`: `bTxReady` and connected, or a connect that completed;
  - `POLLERR`: FAILED;
  - `POLLHUP`: was connected and no longer is.
- `poll`: copy the array in (≤ 1024 entries), evaluate each entry (sockets through `NetSockPoll`, stream handles through `CStream::PollMask`, file handles always IN|OUT, an invalid one `POLLNVAL`, kind 0 or `h < 0` ignored), and return as soon as any is ready. Otherwise `IoWait (gen, step)` and loop until the timeout (`-1` = forever, `0` = only check). Copy `revents` out → the number of ready entries.

#### Other calls
- UDP: `sock_bind` creates a `CSocket (IPPROTO_UDP)` and binds it (port 0 = ephemeral). Send/recv use `SendTo` / `ReceiveFrom` with the peer.
- `sock_name`: local IP from `CNetConfig` plus the own port; the peer from `GetForeignIP` + `GetForeignPort` (patch).
- DNS stays the existing `net_resolve` (v43): no new call.

#### ABI (slots 229–241)

```c
#define KAPI_AF_INET 2
#define KAPI_SOCK_STREAM 1
#define KAPI_SOCK_DGRAM 2
#define KAPI_SOCKF_NONBLOCK 1
#define KAPI_MSG_PEEK 0x2
#define KAPI_MSG_DONTWAIT 0x40
#define KAPI_MSG_WAITALL 0x100
#define KAPI_SHUT_RD 0
#define KAPI_SHUT_WR 1
#define KAPI_SHUT_RDWR 2
#define KAPI_SO_ERROR 1             /* get: pending error (positive errno), cleared */
#define KAPI_SO_NONBLOCK 2          /* get / set 0/1 */
#define KAPI_SO_RCVTIMEO_MS 3
#define KAPI_SO_SNDTIMEO_MS 4
#define KAPI_SO_BROADCAST 5
#define KAPI_SO_NREAD 6             /* get: bytes in the carry buffer, 1 if more is ready */
#define KAPI_SO_TYPE 7
#define KAPI_SO_ACCEPTCONN 8
#define KAPI_POLLIN 0x001
#define KAPI_POLLPRI 0x002
#define KAPI_POLLOUT 0x004
#define KAPI_POLLERR 0x008
#define KAPI_POLLHUP 0x010
#define KAPI_POLLNVAL 0x020
#define KAPI_PK_NONE 0
#define KAPI_PK_SOCKET 1
#define KAPI_PK_STREAM 2
#define KAPI_PK_FILE 3
#define KAPI_POLL_MAX 1024

struct kapi_sockaddr {              /* 16 bytes, IPv4 only */
	unsigned short family;          /* 0: KAPI_AF_INET */
	unsigned short port;            /* 2: host byte order */
	unsigned char addr[4];          /* 4: a.b.c.d */
	unsigned char zero[8];          /* 8 */
};
struct kapi_pollfd {                /* 16 bytes */
	int kind;                       /* 0: KAPI_PK_* */
	int h;                          /* 4: socket number, or a handle's value (<= 0xFFFFFF) */
	short events;                   /* 8 */
	short revents;                  /* 10 */
	int reserved;                   /* 12 */
};

/* --- v75 WP-NET --- (slots 229..241) */
int (*sock_open) (int type, unsigned flags);
	/* -> socket >= 0 / -EPROTONOSUPPORT / -ENFILE (table full) / -ENETDOWN */
int (*sock_connect) (int s, const struct kapi_sockaddr *to);
	/* -> 0 / -EINPROGRESS / -EALREADY / -EISCONN / -ECONNREFUSED / -ETIMEDOUT / -ENETUNREACH / -EBADF */
int (*sock_bind) (int s, const struct kapi_sockaddr *addr);   /* -> 0 / -EADDRINUSE / -EINVAL */
int (*sock_listen) (int s, int backlog);                       /* backlog clamped 1..32 */
int (*sock_accept) (int s, struct kapi_sockaddr *peer, unsigned flags);
	/* flags KAPI_SOCKF_NONBLOCK for the new socket -> socket / -EAGAIN */
long long (*sock_send) (int s, const void *buf, unsigned long long len, unsigned flags,
			const struct kapi_sockaddr *to);
	/* -> bytes / -EAGAIN / -EPIPE / -ENOTCONN / -EDESTADDRREQ */
long long (*sock_recv) (int s, void *buf, unsigned long long len, unsigned flags,
			struct kapi_sockaddr *from);
	/* -> bytes, 0 = orderly end / -EAGAIN / -ECONNRESET / -ENOTCONN / -ETIMEDOUT */
int (*sock_shutdown) (int s, int how);
int (*sock_close) (int s);
int (*sock_getopt) (int s, int opt, int *value);
int (*sock_setopt) (int s, int opt, int value);
int (*sock_name) (int s, int peer, struct kapi_sockaddr *out);  /* peer 0 local, 1 remote; -ENOTCONN */
int (*poll) (struct kapi_pollfd *fds, unsigned n, int timeout_ms);  /* -> ready count / 0 / -EINVAL / -EFAULT */
```

Ownership, adoption and `NetCloseByPid` reclaim work as today. Every buffer is checked (`UserReadable` / `UserWritable`: with WP-MEM merged they populate and pin). With `netcore=1` the copies stay on core 0 (`TNetReq::Buf`), as today.

#### Tests and acceptance

`/bin/nettest`:
- `net_resolve ("example.com")`;
- blocking HTTP GET on port 80; the same with a non-blocking connect + `poll(POLLOUT)` + `SO_ERROR`;
- connect to a closed port → `ECONNREFUSED`;
- a 10-byte `recv` loop over a 100 KB response (no loss, compared with one big read);
- `MSG_PEEK` then read; `poll` timeout accuracy (100 ms ± 15);
- UDP: a DNS query to the gateway or `8.8.8.8:53` with `sendto`/`recvfrom`;
- `getsockname`/`getpeername`; 200 sockets open and closed.
- `nettest serve <port>` + `tools/tests/nettest_peer.py <pi-ip> <port>` on the PC: non-blocking accept, echo, `POLLHUP`.

**On the Pi: run it twice, with `netcore=0` and with `netcore=1`** (`cmdline.txt`). Also check Jet, Mail, `ftpd`, `telnetd`, `vncd`, `rdpd`, `pkg` (the `tcp_*` compatibility) and `netstat` (`net_info` lists the new slots).

**Risks:**
- Circle's TCP quirks under non-blocking use;
- cancelling asynchronous connects;
- the snapshot cost with 256 slots on core 3 (only open slots are scanned);
- the 10 ms readiness latency on core 3 (SHOULD later: an SGI or a weak Circle hook in `CTCPConnection` for immediate wakes).

#### WP-NET resolutions (what was implemented; the ABI above is unchanged)

1. **The slot layer never waits.** `net.cpp`'s `Slot*` functions (where the stack runs) answer `-EAGAIN`; every wait (blocking connect, accept, send, recv, `poll`) is `bsdsock.cpp`'s, on `IoWait`. A blocking send/recv is a loop of non-blocking calls, so a 32 KB request at a time with `netcore=1`.
2. **Every TCP connect has a task of its own**, a blocking one too: `netcore=0` a one-shot kernel task per connect (`CNetConnectTask`), **not** a core-0 worker pool (idle workers would yield in a loop and keep core 0 from idling); `netcore=1` a detached request. The caller waits on the slot's state. A close of a CONNECTING slot (or of one inside a send / a `tcp_accept`) sets `bCancel`; the connector (the last user) frees it. `NetCloseByPid` skips detached requests.
3. **Circle patch** (`tools/circle-patches/wp-net.patch`, against fork commit `b357fa58`): `CSocket::AcceptReady ()` is **not const** (it also replaces backlog connections that died before being accepted, e.g. a SYN whose handshake timed out); **no `GetForeignPort`** (the peer's port is kept in the slot at connect/accept, where it is known); and a fix found on the way: a terminated connection is deleted only once its socket let it go (`CNetConnection::SetReleased` / `IsReleased`, set by `CTransportLayer::Disconnect` and a failed `Connect`) — before, a reset connection was deleted at the next `Process` and its handle reused by the next connection, which the old socket then read, wrote and closed (a socket kept open after a reset, as a BSD socket is until `close`, made that likely). `CSocket::Accept` disconnects a failed backlog connection; `CTransportLayer::IsTerminated (h)` added. The host test `run_circlenet_test.sh` still passes (its stub got `IsTerminated`).
4. **`shutdown (SHUT_WR)` sends no FIN** (as the plan said): Circle's TCP answers a receive in FIN-WAIT with a reset. `SHUT_RD` makes `recv` return 0.
5. **Readiness bits:** a TCP socket never connected is `POLLOUT | POLLHUP` (as Linux); a failed connect `POLLOUT | POLLERR | POLLHUP`; a reset or timed-out connection `POLLIN | POLLHUP` (`recv` then gives the error). `POLLERR/HUP/NVAL` are always reported.
6. **Waits:** `netcore=0` looks again every 10 ms when sockets are involved (only a connect's end and a close call `IoWake`); `netcore=1` sleeps until the tick hook's `IoWake`, at most 100 ms per step (a missed wake costs that much).
7. `sock_bind` accepts 0.0.0.0 or the Pi's address (`EADDRNOTAVAIL` otherwise); port 0 → TCP 61000–61999 (ours), UDP Circle's 60000–60999. `EADDRINUSE` against bound / listening sockets of the same protocol (accepted connections share the listener's port).
8. `tcp_recv` now reads through the carry buffer (no data lost to a small buffer with `netcore=0`); `net_info` adds `udp …` lines (`netstat` shows them).
9. **For WP-IPC (AF_UNIX, WebKit2):** socket numbers `0..255` are the IP table; `bsdsock.cpp` keeps numbers from `SOCK_LOCAL_BASE` (0x10000) for local sockets and has one readiness dispatch (`SockPoll`) that `poll` and every wait use. A local socket = a core-0 kernel object (per process, its own buffers) that answers the same non-blocking calls and calls `IoWake` on a change: the waits and `poll` serve it unchanged. `sock_open` has no family argument: a family in the type's high bits (refused today with `EPROTONOSUPPORT`) or a new `socketpair` entry.

---

### 3.4 WP-LIBC (user space): `libonyxposix`, the sysroot, the tests, the smoke ports

Can start at once:
- pthreads, TLS, time and environment fallbacks work on v67/v68 kapis plus the skeleton's `-ENOSYS` (`thread_create` + a trampoline that sets `TPIDR_EL0` works today, §0.1);
- files, sockets and mmap light up as WP-FILE, WP-NET and WP-MEM merge.

**Tree:** `user/libc/posix/` (MIT):
- sources: `fd.c` (the descriptor table), `file.c`, `dir.c`, `stat.c`, `path.c` (realpath, `/tmp` → `RAM:/tmp`, `/dev/*`), `mman.c`, `pthread.c`, `pthread_sync.c` (mutex, cond, rwlock, once, barrier, spin), `sem.c`, `tls.c`, `emutls.c` (option a only), `guard.c` (option a only), `time.c`, `env.c`, `proc.c` (spawn, waitpid, getpid, kill), `signal.c`, `socket.c`, `netdb.c`, `poll.c` (poll, select, pselect), `misc.c` (sysconf, uname, gethostname, getrandom, getentropy, rlimit, rusage, pwd, uid stubs, `dl*` / `backtrace` / `syslog` stubs), `newlib_syscalls.c` (`_read`, `_write`, `_open`, `_close`, `_lseek`, `_fstat`, `_stat`, `_unlink`, `_link`→`EMLINK`, `_isatty`, `_getpid`, `_kill`, `_gettimeofday`, `_times`, `_sbrk`, `_exit`, `_execve`/`_fork`→`ENOSYS`, `_wait`, newlib's retargetable locks as today, the app-core RPC of `onyx_syscalls.c`, plus `rename`, `mkdir`, `posix_memalign` defined here);
- `crt0posix.S`;
- `onyx-posix.ld` (from `user.ld`, plus a `PT_TLS` program header, `.tdata`/`.tbss`, `.eh_frame` kept with `__onyx_eh_frame_start` (from `stk.ld`), the main thread's TLS block reserved in `.bss`, `__tls_align`);
- `include/` (below);
- `Makefile` (builds `libonyxposix.a` + `crt0posix.o`; `make install SYSROOT=…`).
- The existing apps keep `onyx_syscalls.c`: a program links one or the other, never both.

**Headers** (`include/`, shadowing through `-isystem` under (a), and the frozen subset in the toolchain overlay under (b)):
- `pthread.h` and `sys/_pthreadtypes.h`:
  - `pthread_t` = `unsigned long`;
  - mutex `{lock, type, owner, count}` (16 B); cond `{seq, clock, waiters, pad}` (16 B); rwlock (16 B); `once {state}`; barrier (16 B); attr (48 B); `key` unsigned; spin int.
- `semaphore.h` (`sem_t {value, waiters}`), `sys/mman.h`, `poll.h`, `sys/poll.h`, `sys/socket.h` (Linux layouts: `sa_family_t` 16-bit, `sockaddr_storage` 128 B, `MSG_NOSIGNAL` 0x4000), `netinet/in.h`, `netinet/tcp.h`, `arpa/inet.h`, `netdb.h`, `sys/uio.h`, `sys/un.h`, `sys/utsname.h`, `sys/ioctl.h`, `net/if.h`, `ifaddrs.h` (stub), `sys/random.h`, `sys/statvfs.h`, `endian.h`, `byteswap.h`, `sys/sysmacros.h`, `dlfcn.h`, `execinfo.h`, `syslog.h`.
- `FD_SETSIZE 1024`, defined by the wrapper flags.

**Design points:**
- **Descriptors:** `fds[1024]` → refcounted open-file descriptions `{type FILE|DIR|PIPE_R|PIPE_W|STREAM|SOCKET|DEV_NULL|DEV_ZERO|DEV_RANDOM|CONSOLE, kernel handle or socket, O_* flags, refs, path}` + `FD_CLOEXEC`, under a lock.
  - `dup`/`dup2`/`F_DUPFD` share a description.
  - A pipe = one kernel stream handle shared by two descriptions; closing the last write end calls `stream_eof`.
  - `fd 0` = `stdin_stream ()` (or the console); `1` and `2` = `stdout_stream ()`.
- **TLS (variant 1):** `TPIDR_EL0` → a 16-byte TCB, then the TLS block (aligned to `__tls_align`). `struct pthread` sits just below the TCB, so `pthread_self ()` = `TP − sizeof (struct pthread)`.
  - `crt0posix`: sets the main thread's `TP` (the `.bss` block, `.tdata` copied, `.tbss` zeroed) **before any C code**, registers the EH frames (`__register_frame_info`, as `onyx_eh.c`), builds argc/argv/envp (`get_argv`, `get_env`; falls back to `get_args` split on older kernels), sets `TZ` when it is unset, then `exit (main (argc, argv, envp))`.
  - Option (a): `__emutls_get_address` keeps a per-thread vector in `struct pthread`, indexed by an atomically assigned object index.
- **pthread_create:** `malloc` `struct pthread` + the TLS block → `thread_create_ex {fn = __onyx_thread_start, arg = self, stack_size (default 8 MB, max 16 MB), tls = TP, name}`. The trampoline runs the start routine, then the key destructors (4 rounds) and the emutls frees, stores the result, and calls `kapi_thread_exit`.
  - `join`: `kapi_thread_join` then free.
  - Detached threads are freed lazily by later creates, when `thread_info` says they ended.
  - `pthread_exit` from main waits for the other threads, then exits.
- **Sync objects:** a lock-free fast path (CAS), the slow path through `wait_word`/`wake_word`. On an app core (`kapi__core () != 0`) the slow path spins with `wfe` (no kapi call there). Timed waits convert the absolute `timespec` (REALTIME, or the condattr's clock) into ms on each loop turn.
- **mmap:** anonymous → `vm_map`; a `MAP_PRIVATE` file → `vm_map (RW)` + `pread` + `vm_protect` to the requested prot; `MAP_SHARED` anonymous = private (no fork); `MAP_SHARED` + write on a file → `ENOTSUP`; `msync` → 0; `mlock` → 0; `mincore` from `vm_query`.
- **Time:**
  - `clock_gettime`: `CNTPCT` read at EL0 (`isb; mrs cntpct_el0`), scaled with `clock_info` (MONOTONIC from `boot_cnt`; REALTIME from `utc_us`, resampled every 60 s);
  - `nanosleep`/`usleep`/`sleep` → `sleep_us`;
  - `gettimeofday` rebuilt on the same base.
- **Signals:** a handler table; `sigaction`/`signal`/`raise`/`kill (self)` call the handler synchronously. Defaults: `SIGABRT`/`SIGSEGV`/`SIGTERM`/`SIGKILL` → `_exit (128 + sig)`; `CHLD`/`WINCH`/`URG` ignored. `SIGPIPE` is never raised (`send` → `EPIPE`).
  - `kill` of another pid: `SIGKILL`/`SIGTERM` → `kapi_kill_pid`; signal 0 → `proc_stats` existence check.
  - `waitpid` encoding: exited → `code << 8`; FAULT → `SIGSEGV`; KILLED/OOM → `SIGKILL`.
- **Sockets:** a thin mapping to WP-NET.
  - `getaddrinfo`: numeric parse, else `net_resolve`. Services: numeric, `http`, `https`, `ftp`, `smtp`, `imap(s)`, `pop3(s)`, `dns`. One `AF_INET` `addrinfo` per requested socktype.
  - `EAI_NONAME` on a failure; `EAI_FAMILY` for `AF_INET6`.
  - `select` over `poll`; `socketpair (AF_UNIX)` = two pipes (SHOULD).

**Sysroot and building third-party code:**
- `make -C user/libc/posix install SYSROOT=$ONYX_SYSROOT` (default `out/sysroot`, git-ignored) installs `include/`, `lib/libonyxposix.a`, `lib/crt0posix.o`, `lib/onyx-posix.ld`, `lib/onyx.specs` and `lib/pkgconfig/`.
- `onyx.specs`: `*startfile: crt0posix.o`; `*lib: --start-group -lonyxposix -lc -lm -lstdc++ -lgcc --end-group`; `*link: -T onyx-posix.ld -z max-page-size=0x10000 --gc-sections`.
- `tools/onyx-env.sh` exports:
  - `CC`/`CXX`/`AR`/`RANLIB`;
  - `CFLAGS=-mcpu=cortex-a72 -O2 -ffunction-sections -fdata-sections -fno-pic -fno-pie -isystem $SYSROOT/include -DFD_SETSIZE=1024`;
  - `LDFLAGS=-specs=$SYSROOT/lib/onyx.specs -L$SYSROOT/lib`;
  - `PKG_CONFIG_LIBDIR=$SYSROOT/lib/pkgconfig`, `PKG_CONFIG_SYSROOT_DIR=`;
  - `HOST=aarch64-onyx-elf` (or `aarch64-none-elf` under (a)).
- Autotools: `./configure --host=$HOST --prefix=$SYSROOT --disable-shared --enable-static`.
- CMake: `-DCMAKE_TOOLCHAIN_FILE=tools/onyx-toolchain.cmake`, which sets `CMAKE_SYSTEM_NAME Onyx` (with `tools/cmake/Platform/Onyx.cmake`: `UNIX 1`, static only, `TARGET_SUPPORTS_SHARED_LIBS FALSE`), `CMAKE_SYSROOT`, the compilers, the flags, and `CMAKE_FIND_ROOT_PATH_MODE_*`.
- Ports: `tools/ports/<name>/build.sh` plus sources in `third_party/` (as today).
  - **SQLite** (amalgamation, public domain): `/bin/sqlite3`.
  - **libxml2** 2.13 (MIT): `/bin/xmllint`.
  - **curl** 8.x (curl licence) + mbedTLS 3.6.3, nghttp2, zlib and brotli (all in tree): `/bin/curl`, using the CA bundle the card already has for Jet.
  - Staging any of these on the card triggers the onyx-packages skill.

**Tests:** `/bin/posixtest [group]` (C) and `/bin/posixtest-cxx` (needs WP-TC). PASS/FAIL per check, a summary, exit code = failures. Groups:
- `mem`: every WP-MEM case through `mmap`/`mprotect`/`madvise`/`munmap`; the JSC reserve/commit/decommit pattern; a 4 GB-aligned reservation; a `MAP_PRIVATE` file.
- `thread`: 100 threads in waves of 30; mutex counter under contention (recursive, errorcheck, timedlock); condvar ping-pong 10 000×; timedwait accuracy; rwlock; once; keys + destructors; `__thread` and errno per thread (errno only under b); semaphores; barrier; `pthread_getattr_np` contains a local; `sched_yield`.
- `file`: the filetest matrix through `open`/`read`/`write`/`lseek`/`pread`/`pwrite`/`fstat`/`ftruncate`/`fsync`/`rename`/`unlink`/`mkdir`/`rmdir`/`opendir`/`access`/`realpath`/`getcwd`/`chdir`/`dup`/`dup2`/`fcntl`/`mkstemp`, on `SD:` and `RAM:` and through `/tmp`.
- `io`: pipe `O_NONBLOCK`; poll wake latency from another thread; poll and select timeouts; `/dev/urandom`.
- `time`: monotonic strictly increasing; REALTIME against `gettimeofday`; `nanosleep` statistics; `localtime_r` with `TZ`.
- `proc`: `posix_spawn` of itself; `waitpid` 42; crash → `WIFSIGNALED`; environment inheritance; `getenv`/`setenv`.
- `net`: the nettest cases through BSD calls; curl-style non-blocking connect; `getaddrinfo`.
- `misc`: `sysconf` (page size 65536, 1 CPU), `uname`, `getrandom`, signals.
- `cxx`: `std::thread`, `mutex`, `condition_variable`, `call_once`, `thread_local` with destructors, static-init race, `std::filesystem`, `steady_clock`, `sleep_for` accuracy.

**On the Pi:**
- `posixtest` with `netcore=0` and `netcore=1`;
- `sqlite3 SD:/x.db` (create/insert 100k/select; the same on `RAM:`);
- `xmllint --noout` on large samples;
- `curl -o RAM:/x https://www.wikipedia.org` (TLS + HTTP/2) and `curl -I` on several sites;
- `posixtest-cxx` once WP-TC lands.

**Risks:**
- newlib internals: under (a), errno is shared and `__errno` cannot be overridden cleanly;
- header-shadowing conflicts with newlib's `sys/types.h`;
- the link order of the emutls and guard overrides;
- the libstdc++ hacks under (a), hence WP-TC.

#### WP-LIBC resolutions (what parts 1 and 2 landed; docs/03 §5.4, §5.5)

1. **Every v75 call has a fallback** when the kernel answers `ENOSYS`, so a libonyxposix program
   runs on a v74 kernel too: files → the old calls (read-only through `kapi_open`/`kapi_seek`, a
   writable file held whole and written back at `fsync`/`close`); dirs → `kapi_readdir`; threads →
   v67 `thread_create` (eager stacks: 1 MB default) with the trampoline setting `TPIDR_EL0`; clock →
   `get_datetime` once; sleeps → `msleep`; argv/env → `get_args` and fixed defaults; spawn →
   `kapi_spawn` + `kapi_wait`; TCP → the `tcp_*` calls (blocking connect / accept, a carry buffer);
   `poll` → a user-space loop (non-blocking reads into the carry); `mmap` → 64 KB-aligned heap blocks.
   `posixtest` reports each check that needs the real piece as `SKIP (kernel ENOSYS)`.
2. **Sources** sit in `user/libc/posix/` itself (`start.c` is crt0's C half, `libgen.c` the POSIX
   `basename`/`dirname`); the build tree is `user/libc/posix/build/`.
3. **Specs:** `*startfile` = `crt0posix.o` **and `libonyxposix.a`**, `*link` adds
   `-u __emutls_get_address -u __cxa_guard_acquire -u __onyx_pthread_anchor`: the archive is
   searched before the program's objects and before g++'s `-lstdc++`, so the emutls / guard
   overrides always win, and the whole pthread layer is in every program (libstdc++'s weak
   gthr-posix references under WP-TC must find it). `*lib` = `--start-group libonyxposix.a -lc -lm
   --end-group` + the original libs.
4. **`CMAKE_SYSROOT` is not set** by `tools/onyx-toolchain.cmake` (a `--sysroot` would hide the
   interim toolchain's newlib): the sysroot is an overlay (`-isystem`, `-L`, the specs,
   `CMAKE_FIND_ROOT_PATH`). CMake must not also get `CFLAGS`/`LDFLAGS` from the environment (the
   specs twice fail: `%rename`).
5. **Header overlays** (`#include_next`): `sys/features.h` defines the `_POSIX_*` options (and
   `__onyx__`), plus small ones for `time.h` (`timegm`), `unistd.h` (`pipe2`, `dup3`),
   `sys/stat.h` (`lstat`, `mknod`, `UTIME_*`), `signal.h` (`SA_*`), `sys/file.h` (`flock`); new
   `sys/termios.h` (newlib's `termios.h` includes it and it is missing) and `sys/resource.h`
   (rlimits, a full `rusage`).
6. **errno:** `__errno ()` overridden; the main thread keeps `_impure_ptr->_errno` (single-threaded
   programs behave as before), the other threads have their own; newlib's direct `ptr->_errno`
   writes (`strtol`, libm) still land in the shared one. Full per-thread errno: WP-TC's newlib.
7. **ABI notes for the frozen types:** `pthread_t` = the control block's address
   (`unsigned long`); `pthread_mutex_t` 16 B {lock, type, owner (tid / −core), count},
   `pthread_cond_t` 16 B {seq, clock, waiters, pad}, `pthread_rwlock_t` 16 B {state, seq, waiters,
   writers waiting}, `pthread_once_t` {state}, `pthread_barrier_t` 16 B {seq, in, count, pad},
   `pthread_attr_t` 48 B, `pthread_key_t` unsigned, `pthread_spinlock_t` int, `sem_t` 8 B {value,
   waiters}; the constants as glibc (`PTHREAD_CREATE_JOINABLE` 0, `PTHREAD_MUTEX_RECURSIVE` 1,
   `PTHREAD_MUTEX_DEFAULT` = NORMAL 0): every zero-filled object is a valid default one.
8. **Not done / stubs:** `socketpair` → `EOPNOTSUPP` (curl built with its wake-up on a pipe);
   `pthread_cancel` → `ENOSYS`; `pthread_kill` of another thread → `ENOTSUP`; `sigsuspend`,
   timers, `alarm` stubs; `getifaddrs` `ENOSYS`; `dlopen` fails; `posix_spawn` returns the child's
   pid when `proc_wait (NOHANG | KEEP)` reports it, else the process handle's value (waitpid
   knows both); newlib's `struct stat` keeps its 16-bit `st_ino` (the 64-bit id folded).
9. **mbedTLS for curl** is built out of tree with a configuration of its own
   (`tools/ports/mbedtls`: `FS_IO`, `HAVE_TIME(_DATE)`, `NO_PLATFORM_ENTROPY` +
   `ENTROPY_HARDWARE_ALT` over `getrandom`, PSA's RNG from the entropy module); the vendored tree
   and its prebuilt `.a` (the newlib apps') are untouched.

#### Ports for WebKit resolutions (§5 items 4–6: ICU, HarfBuzz, Skia, fonts; docs/03 §5.5)

1. **What was ported** (all with `aarch64-onyx-elf`, static, into `out/sysroot-onyx`, one
   `tools/ports/<name>/build.sh` each, `sh tools/ports/build-all.sh webkit`; the smoke tools go to
   `/bin` with `make -C user/bin ports`):

   | Library | Version (pin) | Options | `.a` | Vendored (repo, uncompressed / gzip) |
   |---|---|---|---|---|
   | ICU | 78.3 (Debian's orig of the release, sha256 `3a2e7a47…`; data sources: tag `release-78.3` = `21d1eb0f`) — WebKit wants ≥ 70.1 | our CMake (ICU's autoconf has no Onyx host); `U_STATIC_IMPLEMENTATION`, no dyload, no mmap, newlib's `_timezone` / `_tzname`, UTF-8 default code page | uc 4.8 MB, i18n 10 MB, data 15.9 MB | `third_party/icu-78.3`: common + i18n + the filtered `.dat`, 34 MB / 11 MB |
   | libpng | 1.6.44 (in tree) | NEON | 0.5 MB | (the apps' copy) |
   | FreeType | 2.14.3 (in tree) | TrueType, CFF, hinters, smooth / mono, OT-SVG, GX variations, COLR; zlib, brotli, libpng | 0.9 MB | (the apps' copy) |
   | HarfBuzz | 14.5.1 (tag, `eb033319`) — WebKit wants ≥ 2.7.4 with ICU | hb-ft, hb-icu; no subset / raster / vector / gpu / utils | 3.0 MB + 7 KB | `third_party/harfbuzz-14.5.1`, 6.9 MB / 1.5 MB |
   | libjpeg-turbo | 3.1.4 (tag, `e352b02f`) | NEON intrinsics, arithmetic coding; no TurboJPEG API | 1.0 MB | `third_party/libjpeg-turbo-3.1.4`, 3.3 MB / 0.7 MB |
   | libwebp | 1.4.0 (in tree) | NEON, threads, demux, mux, sharpyuv | 0.9 MB | (the apps' copy) |
   | Skia | m154: **WebKit's copy** (`Source/ThirdParty/skia` at WebKit `b8a7a626`, = Skia `588b550a`), sparse checkout | CPU raster only (no Ganesh / Graphite / GL), WebKit's source list, FreeType text, PNG / JPEG / WebP codecs + encoders, `SK_BUILD_FOR_UNIX`, `SK_R32_SHIFT=16` | 15.7 MB | `third_party/skia-m154`, only what is compiled and included: 12.6 MB / 2.9 MB |

   Repository growth: about 58 MB uncompressed, 16 MB compressed. WebKit's own tree will carry its
   Skia again; `third_party/skia-m154` is the same sources, kept so the port and its tests build without
   a WebKit checkout.
2. **ICU's data** (`tools/ports/icu/data-filter.json`, `gen-data.sh`: ICU's data filter with a host build
   of ICU's tools — reproducible, byte-identical): 15.9 MB against 32.4 MB full. In: 42 languages with all
   their regional variants (locales, units, currencies, languages / regions / zones display names);
   break iterators (rules, the Thai / Lao / Khmer / Burmese / CJ dictionaries, the Japanese phrase
   model for `word-break: auto-phrase`); collation (implicit Han order instead of the 600 KB Unihan
   tables); 50 converters — every legacy encoding WebKit's `TextCodecICU` registers (ISO-8859-x, KOI8,
   windows-125x, Mac encodings, EUC-TW) plus Shift_JIS, EUC-JP, ISO-2022-JP/KR/CN, GBK, GB18030, GB2312,
   Big5(-HKSCS), EUC-KR, windows-949 (their `icu:base` tables included); normalization, properties,
   time zones. Out: transliteration, rbnf, confusables (WebKit has no `uspoof`), stringprep (its IDNA is
   UTS 46), character names, the LSTM models. ISO-8859-16 has no ICU table (WebKit's single-byte codec
   covers it). Linked in as the symbol `icudt78_dat` (one `.incbin`): no data file, no `ICU_DATA`.
3. **Fonts: no fontconfig** (§5 item 5 decided). WebKit's Skia font code uses fontconfig in exactly two
   places (`FontCacheSkia.cpp`: `SkFontMgr_New_FontConfig`; `SkiaSystemFallbackFontCache.cpp`: the
   per-character fallback through `FcFontSort` / charsets), and already has a non-fontconfig path for
   Android and Windows (`matchFamilyStyleCharacter` on the `SkFontMgr`). Porting fontconfig would add
   expat, a configuration, a cache on `RAM:` built or read by **every web process** at start (WebKit2),
   and Skia's 1 000-line fontconfig manager — for seven font families. Instead
   `tools/ports/skia/SkFontMgr_onyx.cpp` (MIT, 210 lines, compiled into `libskia.a`):
   `SkFontMgr_New_Onyx ("SD:/res/fonts/")` = Skia's custom directory manager + the CSS generic families
   and common web names mapped to the card's fonts + case-insensitive names + `matchFamilyStyleCharacter`
   (the asked family, then the card's families in a fixed order, cached per character and style). The
   WebKit port adds an `OS(ONYX)` branch to `FontCache::fontManager()` and takes the Android / Windows
   fallback branch (a few lines). Revisit only if a user-installed font folder with hundreds of fonts
   appears (then: a small index file, still not fontconfig).
4. **The smoke tools** (bench `sh tools/tests/posixsim/ports.sh webkit`, v75 and v74 fallbacks):
   `icutest` 52 / 52, `hbtest` 10 / 10 (Devanagari with the PC's FreeSerif; on the card SKIP: none of its
   fonts covers Devanagari), `skiatest` 18 / 18 (the 800 × 600 scene in ~0.5 s under qemu, its PNG
   identical when read back), `skiademo` links (a window: the Pi). The C ports: 13 / 13 unchanged. Sizes
   (unstripped ELF): icutest 20.5 MB, hbtest 2.8 MB, skiatest 25 MB, skiademo 8.5 MB (no ICU: HarfBuzz's
   own Unicode functions) — the ICU data dominates; the kernel's whole-file ELF loading (§5 item 2) holds
   such a file in kernel memory while it loads.
5. **Gaps met** (none blocking, none fixed in libonyxposix): (a) newlib has **no `<uchar.h>`**: ICU's
   headers include it from C (`ptypes.h`, C11 `char16_t`) — C code using ICU fails; C++ is fine (WebKit is
   C++). Fix later: a `uchar.h` in libonyxposix's overlay (`char16_t` / `char32_t`, `mbrtoc16` & co.);
   (b) `aarch64-onyx-elf-gcc` **rejects `-pthread`** (a Linux / BSD target option): libwebp's CMake adds it
   unconditionally — worked around with a compiler launcher (`tools/ports/drop-pthread-flag.sh`); WebKit's
   CMake uses `THREADS_PREFER_PTHREAD_FLAG` → FindThreads probes the flag and falls back, but the next
   toolchain revision should accept `-pthread` as a no-op (a target `.opt` + spec, as `gnu-user.opt`);
   (c) the compiler defines no `__unix__`: Skia needs `SK_BUILD_FOR_UNIX` (in `skia.pc`), and other
   libraries will want `__unix__` too — consider defining it in the toolchain's specs (`-D__unix__`);
   (d) `getauxval` absent: Skia only calls it on LoongArch, AArch64 features are compile-time.
6. **What WebKit's build needs next** from these: `OptionsOnyx.cmake` finding them through the
   sysroot's pkg-config files (`ICU` components data / i18n / uc, `HarfBuzz` with `ICU`, `Freetype`, `PNG`,
   `JPEG`, `WebP` with demux) and building Skia from its own tree with `tools/ports/skia/sources.cmake`'s
   list (CPU only at first: `USE_SKIA` without `USE_LIBEPOXY` / GL — WebKit's `Skia` CMake currently
   compiles Ganesh GL unconditionally on non-Windows: an `OS(ONYX)` / no-GPU branch is needed, or the V3D
   later), `SkFontMgr_onyx` in place of fontconfig, `libxml2` (done), `SQLite` (done), `curl` (done),
   **libxslt** (optional), **woff2** (WebKit decodes WOFF2 itself: brotli is in tree), **libavif / JPEG XL**
   (optional), **lcms2** (optional), and the host tools of §1.7 (Perl, Python 3, Ruby, gperf — `gperf` is
   not on this build machine). Card space: the static WebKit binaries will each carry ICU's data (16 MB)
   unless it becomes a file (`udata_setCommonData` / `ICU_DATA` on `SD:`) shared by the processes — worth
   doing once several web processes run (§13).

---

## 4. Sequencing and effort

| Step | Content | Agent effort | Notes |
|---|---|---|---|
| 0 | WP-0 skeleton on `main` | 0.5 d | Boot test only on the Pi (no behaviour change) |
| 1 (parallel) | WP-MEM | 7–9 d | the largest kernel risk |
| | WP-FILE/PROC | 6–8 d | |
| | WP-NET | 5–7 d | Circle fork patch |
| | WP-LIBC part 1 (headers, fd table, pthreads/TLS on v67, time, env, stubs) | 6–8 d | |
| | WP-TC | 3–5 d + about 1 h per build | independent |
| 2 | Merge MEM → FILE/PROC → NET; Pi round 1 (memtest, filetest, nettest, regressions) | 2–4 d + the user's test day | |
| 3 | WP-LIBC part 2 (files, sockets, mmap, posixtest); smoke ports SQLite, libxml2, curl + mbedTLS; Pi round 2 | 6–8 d | |
| 4 | On WP-TC: `posixtest-cxx`, ICU, HarfBuzz, FreeType, Skia (or Cairo/pixman) as static libraries; Pi round 3 | 5–8 d | |

**Total:** about 45–60 agent-days. With 3–4 agents in parallel and the user's Pi rounds, about **6–8 weeks elapsed** to "a POSIX sysroot in which SQLite, libxml2, curl, ICU, HarfBuzz and Skia build and pass smoke tests on the Pi".

---

## 5. What remains before WebKit itself can start

1. **WP-TC** in place (above).
2. **Kernel:**
   - a **streaming ELF loader**: today the whole file is read into a kernel buffer, then copied (`CUserProcessTask::Run`), which for a 60–100 MB static WebKit means as much kernel heap at load;
   - later, file-backed lazy mapping of the image (`.rodata` with the ICU data);
   - per-thread CPU time; optionally `thread_suspend` / `thread_regs` (JSC sampling, a multi-VM GC);
   - a high-resolution one-shot timer (sleeps below 10 ms when idle);
   - an EL0 crash record (today kmsg only).
3. **malloc:** replace newlib's sbrk-only, single-lock `mallocr` with dlmalloc 2.8.6 (CC0) or mimalloc (MIT) on `vm_map` (it returns memory and scales with threads).
4. **ICU** — *done: ICU 78.3, a 15.9 MB filtered data file ("Ports for WebKit resolutions" 1–2).* (74 or later, Unicode licence): a host build for the tools; a **filtered data file** (locales, collation, break iterators, the converters WebKit's `TextCodecICU` needs). Expect about 8–15 MB (full: about 30 MB), linked as a static object.
5. **Fonts** — *decided and done: no fontconfig, `SkFontMgr_New_Onyx` ("Ports for WebKit resolutions" 3).* Was: fontconfig (HPND/MIT) + expat (in tree) with a config for the card's fonts (`SD:/res/fonts`, the DejaVu set) and a cache on `RAM:`; or a small fontconfig shim implementing the subset WebKit's FreeType font cache calls.
6. **Graphics: decision Skia or Cairo** — *done: Skia m154 (WebKit's copy), CPU raster, with HarfBuzz 14.5.1, FreeType, libpng, libjpeg-turbo, libwebp ("Ports for WebKit resolutions" 1).* Recommended: **Skia** (BSD-3, the library WebKit's GTK/WPE ports moved to and maintain, vendored in WebKit's tree with CMake; CPU raster backend). Cairo is LGPL-2.1/MPL-1.1 and leaving WebKit's main ports.
7. **TLS in WebKit's curl backend:** adapt `CurlSSLVerifier`/`CurlSSLHandle` to mbedTLS (curl's mbedTLS backend gives an `mbedtls_ssl_config *` in `SSL_CTX_FUNCTION`), or bring in OpenSSL 3 / BoringSSL **(verify** the current upstream files).
8. **The embedding (superseded by §9: WebKit2):** upstream WebKitLegacy is now essentially Cocoa-only (the Windows WebKitLegacy was removed) **(verify)**. So "single process" means an **Onyx WebView written over WebCore** (`Page`/`LocalFrame`, `FrameLoaderClient`, `ChromeClient`, `EditorClient`, …; the removed Windows WebKitLegacy in git history is a template). WebKit2 multi-process would add `AF_UNIX` socketpair with fd passing and cross-process shared memory (`MAP_SHARED`), none of which is in this minimum.
9. **The Onyx port layer:**
   - WTF `OS(ONYX)`: `PlatformOS`/`Have`/`Use`; `PageBlock` 64 KB; `StackBounds` via `pthread_getattr_np`; `CPUTime`; `MemoryFootprint` via `vm_stats`; RunLoop (Generic, or Onyx on `pump_wait` + `post`); `RandomDevice` via `getrandom`; `Language` from `LANG`;
   - WebCore: the graphics context on the window canvas (`present`), events from Onyx GUI events, cursors, clipboard (`clipd`), drag and drop (`drag_*`), popup menus, `RenderTheme`/scrollbars, MIME types, curl networking, `CookieJarDB` (SQLite), storage, caches on `RAM:`/`SD:`, screen and DPI;
   - a CMake port: `Source/cmake/OptionsOnyx.cmake` with JIT/WASM/VIDEO/WEB_AUDIO/WEBGL/MEDIA_STREAM/REMOTE_INSPECTOR off;
   - host build time: hours.
10. **Performance and memory expectations:** C_LOOP JavaScript is several times slower than LLInt asm and far slower than a JIT on the A72. A 4 GB Pi 4 is the realistic target; a 1 GB Pi has about 250 MB of app pool (docs/02 §4).

---

## 6. Licences (CLAUDE.md rule: ours under MIT; ask before a library forces a licence on an app)

| Component | Licence | Decision needed? |
|---|---|---|
| `libonyxposix`, the kernel WPs' new code, the tests | ours, **MIT** (the kernel binary stays GPL-3.0 with Circle) | no |
| GCC/binutils (tools), libgcc/libstdc++ (GPL-3 + **GCC Runtime Library Exception**: linking does not impose GPL), newlib (BSD-style mix) | — | no. Distributing the toolchain binaries: ship the sources with the release |
| SQLite | public domain | no |
| libxml2, libxslt | MIT | no |
| curl | curl licence (MIT-like) | no |
| mbedTLS (in tree) | Apache-2.0 OR GPL-2.0-or-later (take Apache-2.0) | no |
| nghttp2, brotli, expat, woff2 | MIT; zlib: zlib licence | no |
| ICU | Unicode-3.0 (permissive, keep the notice) | no |
| HarfBuzz | "Old MIT" | no |
| FreeType (in tree) | FTL (BSD-like with credit) or GPL-2 | no |
| fontconfig | HPND/MIT-style | no |
| Skia | BSD-3 | no |
| **Cairo** | **LGPL-2.1 or MPL-1.1** (pixman MIT) | **yes**, if chosen over Skia |
| **OpenSSL 3 / BoringSSL** (only if WebKit's curl backend is not adapted to mbedTLS) | Apache-2.0 / ISC + OpenSSL | **yes** |
| **WebKit** (WebCore and JavaScriptCore largely LGPL-2.1+, the rest BSD-2) | the WebKit app would be distributed under **LGPL-2.1+** (its own files MIT). Static linking: the source is published, which satisfies relinking | **yes** (HANDOFF already notes it as the user's decision) |
| dlmalloc (CC0) / mimalloc (MIT) | — | no |

---

### Critical files for implementation

- `/home/user/Onyx/kernel/include/kern/kapi_abi.h`: the v75 blocks (WP-0), the structs and constants above.
- `/home/user/Onyx/kernel/mm/addrspace.cpp` (+ `kern/addrspace.h`): lazy VMAs, `Sbrk`, `MapStack`, page-in, zaps, TLBI.
- `/home/user/Onyx/kernel/sys/uaccess.cpp`: probes with `AT S1E0*`, populate, pins, copy retry.
- `/home/user/Onyx/kernel/sys/el0.cpp`: the EL0 fault path (demand paging, OOM kill) and unpin after a system call.
- `/home/user/Onyx/kernel/sys/net.cpp`: socket slots, asynchronous connect, the carry buffer, the readiness snapshot. Also `kernel/sys/kapi.cpp`, `kernel/sys/thread.cpp`, `kernel/sys/appcore.cpp`, `kernel/sys/stream.cpp` and `user/libc/onyx_syscalls.c` as described per WP.

---

## 7. After the minimum: an external yardstick (the user, 2026-10-02)

Once WP-MEM, WP-FILE/PROC, WP-NET and WP-LIBC are merged and pass on the Pi: run the **Open POSIX
Test Suite** (OPTS, part of LTP) — a selection built against the sysroot as static binaries
(grouped several tests per binary to save card space, a runner `/bin/optsrun` writing a report
file to compare across versions). Areas: pthreads (the bulk of OPTS, the most important for WebKit),
mmap/munmap/mprotect, clock_gettime/nanosleep/sched_yield, open/read/write/lseek/fstat. Expected
UNSUPPORTED/FAIL by design: everything built on `fork()` (Onyx has posix_spawn; some tests can be
adapted), real signals, `timer_create`, `mq_*`, named semaphores, real-time scheduling. Each failure
is triaged as a bug, a deliberate absence, or a Linux assumption. Complement: musl's **libc-test**
(the libc functions themselves).

## 8. The goal: WebKit replaces Jet (the user, 2026-10-02)

The WebKit port is meant to **replace Jet (NetSurf)** as Onyx's browser engine, not to sit beside it.
Consequences for the plan:
- **Parity before the switch**: everything Jet does today must exist in the WebKit browser — the
  padlock and certificate viewer, the per-site version (Standard / Mobile / Desktop) and User-Agent,
  per-site zoom, find in page, history, downloads, cookies, clipboard and context menu, the caches on
  `RAM:`, the status bar, and **video/audio with MSE**: WebCore's media normally uses GStreamer — Onyx
  needs its own `MediaPlayerPrivate` on `user/av` (FFmpeg, dav1d, libvpx, Opus). `docs/07-BROWSER-GAPS.md`
  becomes the parity checklist; Jet's PC bench (shots against Chromium) the regression bench.
- **JavaScript speed**: C_LOOP only for bring-up; then JSC's **LLInt** (offlineasm, ARM64; generated
  at build time, no runtime code generation, no JIT needed) — the target is not to be slower than
  Jet's QuickJS; later the Baseline JIT on `vm_protect`.
- **Transition**: Jet stays until parity (both installed, e.g. `jet` and a WebKit build), then the
  switch. Open question for the user: keep the name "Jet Browser" on the WebKit engine (the app's
  licence would go from GPL-2.0 to LGPL-2.1+).
- **Memory**: a 4 GB Pi 4 is the realistic target; demand paging (WP-MEM) is a prerequisite.
- Reused from Jet: the browser UI (toolbar, dialogs, `jet.ini` settings, site-version logic).

## 9. Decision: WebKit2 (the user, 2026-10-02)

The target is **WebKit2** (multi-process), on the model of WebKit's **PlayStation** port (WebKit2
without GLib, curl networking, its own platform layer) — not a single-process embedding over
WebCore: WebKitLegacy is maintained upstream for Cocoa only, so a single-process port would mean
maintaining our own embedding against WebCore's moving APIs alone. With WebKit2, upstream's
architecture is kept (UI process, one web process per tab/site with WebCore + JSC, a network
process), a crashing page kills only its process (matching Onyx's EL0 isolation), and upstream
updates can be followed. In both cases the engine (DOM, CSS, layout, JS) is WebKit's; Onyx owns the
platform port, the media backend on `user/av`, the compositor on the V3D, the browser UI and the
system below.

What it adds to this plan — **WP-IPC** (after the POSIX minimum is merged; its spec to be written
then, against WebKit's `Source/WebKit/Platform/IPC/unix/` and the PlayStation port's process
launcher):
- local stream/datagram sockets between processes: `socketpair(AF_UNIX, SOCK_STREAM|SOCK_SEQPACKET)`
  (and named `AF_UNIX` sockets if WebKit needs them), readable by `poll`;
- **passing handles between processes** (`sendmsg`/`recvmsg` with `SCM_RIGHTS`): a file, a pipe end,
  a socket, a shared-memory object moved into the receiver's handle table;
- **shared memory between processes**: anonymous shared objects (a `memfd_create` / `shm_open`
  equivalent) mapped `MAP_SHARED` in several address spaces (pages pinned in the object, refcounted,
  never zapped by one process's `munmap` while another maps them), sealing/size fixed after creation;
- the process launcher (`posix_spawn` with inherited handles: the IPC socket's end given to the child);
- memory budget: the network and UI processes add roughly 30–60 MB (WPE runs multi-process on
  512 MB devices).
The single-process path stays possible only as an optional bring-up step (a first page sooner).

## 10. Self-hosting: a native toolchain on the Pi (the user, 2026-10-02)

Goal: compile Onyx apps **on the Pi** (and, later, Onyx itself). Path, after WP-TC (the cross
`aarch64-onyx-elf` toolchain):
1. **Native GCC + binutils** by a Canadian cross (`--build=x86_64-linux --host=aarch64-onyx-elf
   --target=aarch64-onyx-elf`): static `gcc`, `cc1`, `cc1plus`, `as`, `ld` on the card (a `sdk`
   package). The driver starts `cc1` / `as` / `ld` through libiberty's pex: use its `posix_spawn`
   path (verify for GCC 14; else a small pex patch) — Onyx has no `fork()`.
2. **Build tools**: Ninja (posix_spawn) + CMake first; GNU make ≥ 4.4 (posix_spawn) needs a
   `/bin/sh` for its recipes, and a POSIX shell (dash/ash) forks for subshells → either the shell's
   no-MMU mode (busybox on uClinux uses **`vfork`**) with a `vfork` in the kernel (the child borrows
   the parent's address space until its exec — much simpler than fork), or a make that runs
   simple recipes directly.
3. **The SDK on the card** (`SD:/sdk`): newlib, libstdc++, libonyxposix, `kapi.h`, wtk, the CMake
   toolchain file for native builds, an app template.
4. **Milestone**: a wtk app compiled on the Pi and launched from the desktop. Later: a code editor
   with a Build command; the kernel itself built on the Pi (the native GCC with `-ffreestanding`).
Expect seconds to a minute for a small C app, minutes for a larger C++ one; WebKit stays a PC build.

## 11. Symbolic links on FAT (the user's design, 2026-10-02)

FAT has no symlinks; ports (`make install`, CMake trees, `libfoo.so -> libfoo.so.1`) and the
self-hosted toolchain want them. Design (the Cygwin way):
- A link = a small file with the FAT **System attribute** set, starting with a magic
  (`!<onyx-link>\n`) followed by the target path (UTF-8). The attribute is the fast filter: it
  comes free in the directory entry (`FILINFO.fattrib`), so only flagged files have their header
  read — no cost on ordinary files. On `RAM:`: a real link node type.
- Resolved **in the kernel's path lookup**, so every app benefits (old ones too): component by
  component (a link in the middle of a path to a directory), a relative target resolved from the
  link's directory, cross-volume targets (SD: → RAM:) allowed, at most 40 hops (`ELOOP`).
- POSIX semantics: `open`/`stat` follow; `lstat` reports `S_IFLNK`; `readlink`, `symlink`;
  `unlink`/`rename` act on the link; `O_NOFOLLOW`. New kapis (a v76 block): `path_symlink`,
  `path_readlink`, `path_lstat` (or a NOFOLLOW flag on `path_stat`), `KAPI_O_NOFOLLOW`; libonyxposix
  maps the POSIX calls.
- Limits: hard links (`link()`) stay impossible on FAT (`EPERM`, as Cygwin on FAT); on a PC the
  link shows as a small text file holding its target. The File Viewer shows links (an arrow
  overlay); `cp` / `zip` / the Archiver choose link or target (`-P` / `-L`).

## 12. Later: `fork()` and `vfork()` (the user, 2026-10-02: "for later")

Not needed for launching programs (`posix_spawn` covers fork+exec with redirections) nor for
WebKit2; useful for compatibility (shells such as dash/bash and make's recipes — so mainly for
self-hosting, §10 —, servers, part of OPTS). When a port needs it, **WP-FORK**: `fork` by an
**eager copy of the resident pages** (cheap thanks to demand paging: only touched pages), the
handles duplicated with a shared open-file description (POSIX: parent and child share a file's
offset), only the calling thread copied (a new task from its trap frame, x0 = 0 in the child),
no windows / app cores / GPU / sound / surfaces in the child; libc `fork` + `pthread_atfork`, the
malloc/stdio locks made sane in the child; plus `vfork` (the child borrows the parent's address
space until exec). Copy-on-write (per-frame refcounts, a write-fault path, TLBI of the parent)
only if a real performance need appears.

## 13. Dynamic loading: not now; shared code pages instead (the user, 2026-10-02)

Onyx stays with **static binaries**. Real dynamic loading (`.so`, `dlopen`) would need: a real OS
target in GCC/binutils (`aarch64-onyx` with `config/aarch64/onyx.h`, shared libgcc/libstdc++,
`crtbeginS.o`: GCC patches, as SerenityOS did) and everything built `-fPIC`; a user-side dynamic
linker (`ld.so`: load order, AArch64 relocations, `DT_GNU_HASH` symbol lookup, dynamic TLS with
TLSDESC / `__tls_get_addr`, init/fini, `dlopen`/`dlsym`/`dlclose`; musl's as the model); the kernel
allowing `PROT_EXEC` mappings (refused today outside `code_alloc`), `PT_INTERP` and the aux vector,
and file pages shared between processes; shared builds of newlib, libonyxposix, libstdc++ and the
ports. Plugins do not need it (Koton's are processes, safer).
**What WebKit2 needs instead** (its several identical web processes, each a 60–100 MB static
binary): the kernel **sharing the read-only code (and rodata) pages of the same ELF file between
the processes that run it** — a small page cache keyed by the file, the image's text/rodata mapped
read-only from it instead of copied per process. Much simpler, most of the memory gain. To do when
WebKit2 runs; real dynamic loading only if an indispensable piece of software demands it.
