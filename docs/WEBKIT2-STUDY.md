# WebKit port, the WebKit2 layer (`Source/WebKit`) for Onyx: a study

*Status: a study made on 2026-10-02 by reading the pinned WebKit revision and the Onyx tree —
nothing compiled. It is the input of the roadmap's step 1 ("a usable browser") in
`docs/08-WEBKIT-PORT.md`, which says what was decided and what was done since. Decided by the
user after it was written, where it differs from the study's own recommendations: **one executable
for the three roles** (UI, web, network: a multi-call program whose role is chosen at launch; the
launcher starts its own file with a role argument) — the study's "three programs first" is not
followed; **no tabs**; the web view as a component other programs embed (the C API it recommends
suits this). Followed as recommended: the software drawing path opened by a guard patch, the
`select` monitor thread with a `shutdown` before the close, no WAL for SQLite on Onyx, the disk
cache off at first, the full-screen client stubbed, the data directories set by the embedder.*

Nothing was built or modified; every statement comes from reading the pinned checkout (`/home/troll/webkit`, branch `onyx`) and the Onyx tree (`/home/troll/src/Onyx`, branch `webkit-port`). Paths below are relative to `/home/troll/webkit/Source/WebKit` unless they start with another root. Sizes of files to write are estimates.

Three findings change the plan and are worth reading first:

1. **The generic run loop cannot watch a descriptor.** `ConnectionUnix.cpp` only keeps reading the socket under `PLATFORM(PLAYSTATION)` (a `select` thread per connection). Onyx must take that branch, and needs one extra fix because Onyx's `poll` holds a reference on the descriptor while it waits (section B).
2. **The software drawing path exists but is hidden behind `USE(COORDINATED_GRAPHICS) || USE(TEXTURE_MAPPER)`.** About 17 guard sites in 10 files, plus one line in `WebPage.cpp` that forces accelerated compositing on for every port not in a short list (section C).
3. **WebCore opens every writable SQLite database in WAL mode**, which needs `mmap(MAP_SHARED, PROT_WRITE)` of a file: `ENOTSUP` on Onyx. Cookies, IndexedDB, local storage and ITP would all fail to open without a small patch. The HTTP disk cache also depends on hard links and writable file mappings (section E).

---

## A. The build

### What `PlatformPlayStation.cmake` lists (182 lines)

| Area | Content | Lines |
|---|---|---|
| Includes | `Headers.cmake`, `Platform/Curl.cmake`; later `Platform/Skia.cmake` or `Cairo.cmake`, `Platform/WC.cmake` if `USE_GRAPHICS_LAYER_WC` | 1-2, 129-159 |
| Executables | `WebProcess`, `NetworkProcess`, `GPUProcess`, each with one entry file `*/EntryPoint/playstation/*Main.cpp`; libraries `${EGL_LIBRARIES}`, `${ProcessLauncher_LIBRARY}`, `OpenSSL::Crypto` | 6-29 |
| `WebKit_SOURCES` | 45 files (table below) | 31-101 |
| Include directories | `Platform/IPC/unix`, `Platform/classifier`, `Platform/generic`, `Shared/libwpe`, `UIProcess/API/C/playstation`, `UIProcess/API/libwpe`, `UIProcess/API/playstation`, `UIProcess/CoordinatedGraphics`, `UIProcess/playstation`, `WebProcess/WebPage/CoordinatedGraphics`, `WebProcess/WebPage/libwpe` | 103-115 |
| Conditional | gamepad (libwpe), WebDriver (libwpe), `BackingStoreSkia.cpp` / `BackingStoreCairo.cpp`, the Coordinated Graphics compositor files, the WPE backend process provider | 117-170 |
| Public C headers | 7 PlayStation `WK*.h` | 173-182 |

`Platform/Curl.cmake` adds 15 generic files: `NetworkProcess/curl/*` (6), `Cookies/curl/*` (2), `cache/NetworkCacheDataCurl.cpp`, `cache/NetworkCacheIOChannelCurl.cpp`, three C API curl files, `WebsiteDataStoreCurl.cpp`, `WebFrameNetworkingContext.cpp`. `Platform/Skia.cmake` adds `WKImageSkia.cpp`, `WebAutomationSessionSkia.cpp` and 7 `.serialization.in` files.

### Reusable as they are

- `Platform/IPC/unix/ArgumentCodersUnix.cpp` (52), `IPCUtilitiesUnix.cpp` (61), `IPCSemaphoreUnix.cpp` (297; a no-op off Linux, see B), `UnixMessage.h` (171).
- `Platform/unix/LoggingUnix.cpp` (44), `ModuleUnix.cpp` (45, an empty stub: no `dlopen`).
- `Shared/unix/AuxiliaryProcessMain.cpp` (116): parses `argv[1]` = process identifier, `argv[2]` = socket descriptor; ignores `SIGPIPE` with `sigaction`.
- `WebProcess/EntryPoint/unix/WebProcessMain.cpp` and `NetworkProcess/EntryPoint/unix/NetworkProcessMain.cpp` (32 lines each: `main` calls `WebKit::WebProcessMain` / `NetworkProcessMain`).
- Everything in `Platform/Curl.cmake` and `Platform/Skia.cmake`. No OpenSSL include exists anywhere in `Source/WebKit` (the only mention is the PlayStation `dlopen` list).
- `UIProcess/skia/BackingStoreSkia.cpp` (123), `UIProcess/CoordinatedGraphics/DrawingAreaProxyCoordinatedGraphics.cpp` (376), `WebProcess/WebPage/CoordinatedGraphics/DrawingAreaCoordinatedGraphics.cpp` (712), after the guard patch of section C.
- `UIProcess/libwpe/WebPasteboardProxyLibWPE.cpp` (58): it only calls `WebCore::PlatformPasteboard`, no libwpe function.
- `UIProcess/DefaultUndoController.cpp`, `LegacySessionStateCodingNone.cpp`, `WebGrammarDetail.cpp`, `WebMemoryPressureHandler.cpp`, `WebViewportAttributes.cpp`, `API/C/WKUserScriptRef.cpp`, `API/C/WKViewportAttributes.cpp`, `NetworkProcess/NetworkDataTaskDataURL.cpp`, `Classifier/WebResourceLoadStatisticsStore.cpp`, `Platform/classifier/ResourceLoadStatisticsClassifier.cpp`.
- `Platform/IPC/unix/ConnectionUnix.cpp` (576), with a small patch (B).

### PlayStation-specific: each needs an Onyx counterpart

| PlayStation file | Lines | What it does | Onyx counterpart |
|---|---|---|---|
| `WebProcess/EntryPoint/playstation/WebProcessMain.cpp` | 101 | `dlopen` of every library, then `WebProcessMain` with the SDK's connection descriptor | use `EntryPoint/unix` (32) or a multi-call `main` |
| `NetworkProcess/EntryPoint/playstation/NetworkProcessMain.cpp` | 81 | same for the network process | same |
| `UIProcess/Launcher/playstation/ProcessLauncherPlayStation.cpp` | 122 | socketpair + SDK `PlayStation::launchProcess` | `ProcessLauncherOnyx.cpp` on `posix_spawn` (~110) |
| `UIProcess/playstation/PageClientImpl.{h,cpp}` | 181 + 371 | the view's `PageClient`, nearly all empty | `UIProcess/onyx/PageClientImpl.{h,cpp}` (~180 + ~380) |
| `UIProcess/playstation/PlayStationWebView.{h,cpp}` | 99 + 180 | owns the `WebPageProxy`, view size and activity state, forwards to an `API::ViewClient` | `OnyxWebView.{h,cpp}` (~100 + ~180) |
| `UIProcess/API/playstation/APIViewClient.h` | 57 | the C++ client interface of the view | same (~60) |
| `UIProcess/playstation/WebPageProxyPlayStation.cpp` | 83 | `platformInitialize`, user agent, recent searches, `didUpdateEditorState`: stubs | `WebPageProxyOnyx.cpp` (~80) |
| `UIProcess/playstation/WebProcessPoolPlayStation.cpp` | 69 | five empty `platform*` hooks | `WebProcessPoolOnyx.cpp` (~60) |
| `UIProcess/playstation/WebPreferencesPlayStation.cpp` | 38 | accelerated compositing, forced compositing and threaded scrolling off | `WebPreferencesOnyx.cpp` (~40), same content |
| `UIProcess/WebsiteData/playstation/WebsiteDataStorePlayStation.cpp` | 63 | default directories under `/app0` | `WebsiteDataStoreOnyx.cpp` (~70) |
| `UIProcess/API/C/playstation/WKView.{h,cpp}`, `WKViewClient.h` | 59 + 206 + 70 | the C view: create, size, focus / active / visible, the view client | Onyx copies (~60 + ~200 + ~70) |
| `UIProcess/API/C/playstation/WKPagePrivatePlayStation.{h,cpp}` | 43 + 214 | `WKPageHandleKeyboardEvent` / `MouseEvent` / `WheelEvent` built on libwpe structs; `WKPagePaint` into a caller's pixel buffer | `WKPagePrivateOnyx.{h,cpp}` (~45 + ~220), no libwpe |
| `UIProcess/API/C/playstation/WKContextConfigurationPlayStation.{h,cpp}` | 46 + 60 | process paths, user id | Onyx copy (~45 + ~55) |
| `UIProcess/API/C/playstation/WKRunLoop.{h,cpp}` | 43 + 59 | `WKRunLoopInitializeMain` (= `InitializeWebKit2`), `RunMain`, `StopMain`, `CallOnMainThread` | Onyx copy (~45 + ~60) |
| `UIProcess/API/C/playstation/WKAPICastPlayStation.h` | 53 | `WKViewRef` to the view class | `WKAPICastOnyx.h` (~55) |
| `Shared/API/c/playstation/WKEventPlayStation.{h,cpp}`, `WKBasePlayStation.h` | 101 + 78 + 35 | C event structs, `WKViewRef` typedef | Onyx copies (~100 + ~80 + ~35) |
| `WebProcess/playstation/WebProcessMainPlayStation.cpp` | 54 | `AuxiliaryProcessMainBase<WebProcess>`, `SkGraphics::Init()` | `WebProcessMainOnyx.cpp` (~50) |
| `WebProcess/playstation/WebProcessPlayStation.cpp` | 77 | empty `platform*` hooks | `WebProcessOnyx.cpp` (~70) |
| `WebProcess/WebPage/playstation/WebPagePlayStation.cpp` | 95 | stubs; `handleEditingKeyboardEvent` is `notImplemented` | `WebPageOnyx.cpp` (~250): take the key handling from `WebProcess/WebPage/win/WebPageWin.cpp` (259) |
| `WebProcess/InjectedBundle/playstation/InjectedBundlePlayStation.cpp` | 61 | loads a bundle library | `InjectedBundleOnyx.cpp` (~40): `initialize` returns false |

### Depends on things Onyx does not have: do not list

- **libwpe types**: `Shared/libwpe/NativeWeb{Keyboard,Mouse,Wheel,Touch}EventLibWPE.cpp` (44-57 each) and `Shared/libwpe/WebEventFactory.{h,cpp}` (73 + 378) take `wpe_input_*` structs. `UIProcess/Gamepad/libwpe`, `UIProcess/Automation/libwpe`, `UIProcess/API/libwpe/TouchGestureController.cpp`, `Launcher/libwpe/ProcessProviderLibWPE.cpp`, `Launcher/playstation/ProcessProviderPlayStation.cpp`, `UIProcess/playstation/DisplayLinkPlayStation.cpp`.
- **EGL / GL / compositor**: `AcceleratedSurfacePlayStation.cpp` (431, listed unconditionally at line 94), `LayerTreeHostPlayStation.cpp` (592), `ThreadedCompositorPlayStation.cpp` (496), `ThreadedDisplayRefreshMonitorPlayStation.cpp` (130), `CompositingRunLoop.cpp`, `CoordinatedSceneState.cpp`; `${EGL_LIBRARIES}`.
- **GPU process and media**: `GPUProcess/playstation/*`, `GPUProcess/media/playstation/*`, `WebProcess/GPU/media/playstation/VideoLayerRemotePlayStation.cpp`.
- **GLib**: nothing in the PlayStation list.

### Static executables: three programs or one

- Upstream sets `WebKit_LIBRARY_TYPE SHARED` (`Source/cmake/OptionsPlayStation.cmake:374`; `WebKitCommon.cmake:251` is the default). `CMakeLists.txt:792-794` makes each process link `WebKit`; `:843-849` and `:1559-1566` declare the `WebProcess` and `NetworkProcess` targets whenever `USE_EXTENSIONKIT` is off, and `:843-846` makes them link dependencies of `WebKit`. With `WebKit_LIBRARY_TYPE STATIC` the same targets link `libWebKit.a`; I found no CMake obstacle but did not run it.
- The entry points are plain functions: `WK_EXPORT int WebProcessMain(int argc, char** argv)` (`WebProcess/WebProcessMain.h`), `NetworkProcessMain` (`NetworkProcess/curl/NetworkProcessMainCurl.cpp`). `AuxiliaryProcessMainBase::run` (`Shared/AuxiliaryProcessMain.h`) sets the process type first, then parses the two arguments, calls `InitializeWebKit2`, and runs the loop. Upstream already puts the three roles' code in one image (the shared library), so **a single multi-call binary is possible**: a `main` that picks the role from an argument and calls one of the two functions or the UI's main. `ProcessLauncherOnyx` then starts the same file with a role flag.
- Trade-off, not measured: three separately linked programs each drop unused code and are smaller than the union, which matters today because the loader copies the whole image (docs/08: 5-6 s for 80 MB). One binary is one file on the card and is what the planned shared code pages and lazy loader reward.
- `PlayStation`'s `MAKE_PROCESS_PATH` and `launchOptions.processPath` (`ProcessLauncher.h:102-105`, `WebProcessProxy.cpp:799-802`, `APIProcessPoolConfiguration.h:135-140`, `WebProcessPool.h:545-547`) are the hooks for a configurable path; they are under `PLATFORM(PLAYSTATION)` and need an Onyx branch.

### Generators run by `Source/WebKit/CMakeLists.txt`

| Generator | Tool | Input / output | Line |
|---|---|---|---|
| `Scripts/generate-message-receiver.py` | Python | 186 `.messages.in` entries → `*MessageReceiver.cpp`, `*Messages.h`, `MessageNames.{h,cpp}`, `MessageArgumentDescriptions.cpp`; also run at configure time | 1035-1116 |
| `Scripts/generate-serializers.py` | Python | the `.serialization.in` lists (395 such files in the tree) → 16 `GeneratedSerializers*.cpp`, `SerializedTypeInfo.cpp` (compiled `-O0`: "a single 29000-line function") | 1210-1259 |
| `generate-inspector-protocol-bindings.py`, `generate-combined-inspector-json.py` | Python | Automation and WebDriver BiDi dispatchers, generated unconditionally | 1307-1337 |
| `jsmin.py` + `xxd.pl` | Python, Perl | `WebAutomationSessionProxy.js` → header | 1357-1364 |
| `generate-log-declarations.py` | Python | `Platform/LogMessages.in` | 1371-1377 |
| `GeneratePreferences.rb`, `GenerateSecurityFlags.rb` | Ruby | `UnifiedWebPreferences.yaml`, `SecurityFlags.yaml` → preferences sources and two more `.serialization.in` | 1400-1439 |
| `generate-bindings.pl`, `GenerateImports.pl` | Perl | Web Extension IDL, generated unconditionally | 1124-1170 |
| Unified sources, forwarding headers | Ruby, Perl | `Scripts/generate-forwarding-headers.pl:39` has a fixed list of platform directory names: add `onyx` | — |

Host tools: Perl, Python 3, Ruby, already required by steps 1 and 2. I found no new one.

---

## B. IPC and process launching

### How the socket is watched (the critical point)

- `Source/WTF/wtf/generic/RunLoopGeneric.cpp` (363 lines) waits only on condition variables (lines 136, 148, 156). It has no descriptor-watching facility. `WorkQueueGeneric.cpp` (85) is one thread with its own `RunLoop` per queue.
- `ConnectionUnix.cpp`:
  - **`USE(GLIB)`**: not this file at all. GLib ports compile `Platform/IPC/glib/ConnectionGLib.cpp` (631), which uses `GSocketMonitor` (`Connection.h:89-90, 844-850`).
  - **`PLATFORM(PLAYSTATION)`** (`:338-357`): `platformOpen` starts a thread "SocketMonitor" that loops `select(fd + 1, &readSet, 0, 0, 0)` and calls `readyReadHandler()` on that thread. `platformInvalidate` detaches it (`:117-122`). The member is `Connection.h:868-869`.
  - **Otherwise** (`:359-362`): one `m_connectionQueue->dispatch(readyReadHandler)` and nothing more. The socket is read once and never again.
- So Onyx must add `|| PLATFORM(ONYX)` at `Connection.h:868`, `ConnectionUnix.cpp:117` and `:338`.
- **Risk found in Onyx's libc**: `poll` takes a reference on each description for the whole wait (`user/libc/posix/poll.c`: `__onyx_fd_get` before `kapi_poll`, `__onyx_fd_put` after), and `close` only drops the table's reference (`fd.c:168-185`). When `platformInvalidate` closes the socket from another thread while the monitor sleeps in `select`, the kernel end stays open until the peer sends something. The peer never sees the end of the connection, and the monitor thread and its `Connection` leak. Fix in the same patch: `shutdown(fd, SHUT_RDWR)` before the close (POSIX-PLAN §14.3 says every change wakes waiters and `SHUT_RD` makes receives return 0). This reproduces on the PC bench, since the reference is taken in libc.
- Second small patch: `ConnectionUnix.cpp:466-471` treats `EPIPE` as "peer gone" only under `OS(LINUX)`. Onyx's kernel returns `EPIPE` for a send to a closed peer (§14.3), so add `OS(ONYX)`; otherwise the send path logs an error and the close is only noticed by the reader.

### Primitives used

- **Socket**: `socketpair(AF_UNIX, SOCK_SEQPACKET, 0)` (`ConnectionUnix.cpp:55-59`, `IPCUtilitiesUnix.cpp`). The `SOCK_CLOEXEC` form is Linux-only; elsewhere `setCloseOnExec` after the call.
- **Non-blocking**: `setNonBlock` = `fcntl(F_GETFL)` / `F_SETFL | O_NONBLOCK` (`:325-331`).
- **Send**: `sendmsg(fd, &msg, MSG_NOSIGNAL)` with up to 3 iovecs and one `SCM_RIGHTS` block; on `EAGAIN`, `poll(POLLOUT, -1)` and retry (`:453-464`).
- **Receive**: `recvmsg(fd, &msg, MSG_NOSIGNAL)` into a 4096-byte buffer with a control buffer of `CMSG_SPACE(sizeof(int) * 254)`; `MSG_CTRUNC` is a failure; each received descriptor gets `FD_CLOEXEC` (`:223-288`).
- **Large bodies**: above 4096 bytes the body goes into a `SharedMemory` whose descriptor is appended (`UnixMessage.h` `setBodyOutOfLine`). On Onyx each such message costs at least one 64 KB frame and about eight kernel calls; raising `messageMaxSize` for Onyx is a possible later optimisation (the kernel takes packets up to `SO_SNDBUF`, 256 KB by default).
- **Buffers**: the PlayStation launcher sets `SO_SNDBUF` / `SO_RCVBUF` to 32 KB on both ends (`ProcessLauncherPlayStation.cpp:67-73`).
- **Shared memory** (`Source/WebCore/platform/unix/SharedMemoryUnix.cpp`, 198 lines, already compiled in step 2): `memfd_create` only under `HAVE(LINUX_MEMFD_H)` through `syscall`; else `shm_open(SHM_ANON)` under `HAVE(SHM_ANON)`; else, which is Onyx's path, `shm_open("/WK2SharedMemory.<random>", O_CREAT | O_RDWR, 0600)` followed at once by `shm_unlink`. Then `ftruncate`, `mmap(PROT_READ | PROT_WRITE, MAP_SHARED)`, `munmap`; a handle is a `dup` of the descriptor. No sealing here.
- **Semaphores** (`IPCSemaphoreUnix.cpp`): the real implementation (a sealed memfd page and `syscall(SYS_futex)`) is entirely under `OS(LINUX)`. Elsewhere `signal()` does nothing and `wait()` returns false. PlayStation ships with this stub; it only matters for stream connections (GPU process), which are off.
- **Child side**: the socket is a descriptor number in `argv[2]` (`AuxiliaryProcessMain.cpp:62-95`).

### The launchers

- PlayStation (`ProcessLauncherPlayStation.cpp:63-103`): `createPlatformConnection(SOCK_SEQPACKET, SetCloexecOnServer)`, the two `setsockopt` pairs, `argv = { processIdentifier }`, then `PlayStation::launchProcess(path, argv, { clientFd, userId })` from the SDK's `<process-launcher.h>` (not in the checkout), or `wpe_process_launch` with the WPE backend. Then `RunLoop::mainSingleton().dispatch(didFinishLaunchingProcess(pid, serverFd))`. `terminateProcess` calls `PlayStation::terminateProcess(pid)`. Everything runs inline from the constructor (`ProcessLauncher.cpp`), on the caller's thread.
- An Onyx `ProcessLauncherOnyx.cpp`, about 110 lines:
  1. `createPlatformConnection(SOCK_SEQPACKET, SetCloexecOnServer)`: the client end keeps no `FD_CLOEXEC`, so `posix_spawn` gives it to the child at the same number (`user/libc/posix/proc.c`: every descriptor without `FD_CLOEXEC` from 3 up is passed).
  2. `argv = { path, [role flag], "<processIdentifier>", "<clientFd>", 0 }`, `posix_spawn(&pid, path, 0, 0, argv, environ)`.
  3. Close the client end, dispatch `didFinishLaunchingProcess(pid, server)` to the main run loop.
  4. `terminateProcess`: `kill(pid, SIGKILL)` (maps to `kapi_kill_pid`, `signal.c:100-122`). Reap with `waitpid(pid, 0, WNOHANG)` somewhere, or libc's child list grows by one entry per launch.
- Any other descriptor the UI process holds without `FD_CLOEXEC` is inherited too; harmless, but worth knowing.

### POSIX functions used by these files, against libonyxposix

| Function | Used where | Onyx status |
|---|---|---|
| `socketpair(AF_UNIX, SOCK_SEQPACKET)` | `IPCUtilitiesUnix.cpp` | present (`ipc.c:168`); `SOCK_CLOEXEC` and `SOCK_NONBLOCK` accepted |
| `sendmsg`, `recvmsg`, `SCM_RIGHTS` | `ConnectionUnix.cpp` | present (`socket.c:458, 487`, `ipc.c:221, 311`); up to 256 handles; `MSG_CTRUNC` set |
| `CMSG_SPACE/LEN/DATA/FIRSTHDR/NXTHDR` | same | present (`include/sys/socket.h:63-76`) |
| `MSG_NOSIGNAL` | same | defined (0x4000), ignored; a dead peer gives `EPIPE`, no signal |
| `select` | PlayStation monitor | present, built on `poll` (`poll.c:338`), `FD_SETSIZE` 1024; holds a reference during the wait (risk above) |
| `poll(POLLOUT)` | send retry | present (`poll.c:139`) |
| `shutdown` | proposed fix | present (`socket.c:734`) |
| `setsockopt(SO_SNDBUF / SO_RCVBUF)` | launcher | present for local sockets (`socket.c:630`) |
| `fcntl(F_GETFD / F_SETFD / F_GETFL / F_SETFL / F_DUPFD_CLOEXEC)` | `UniStdExtrasUnix.cpp` | present (`fd.c:270`) |
| `fcntl(F_ADD_SEALS / F_GET_SEALS)` | semaphore, Linux path only | present for shm objects |
| `fcntl(F_SETLK)`, `flock`, `lockf` | SQLite, `FileHandlePOSIX.cpp:144` | accepted, do nothing (`fd.c:340-355`) |
| `dup`, `dup2`, `dup3`, `pipe`, `pipe2` | descriptors | present |
| `eventfd` | not used by these files | absent |
| `shm_open`, `shm_unlink` | `SharedMemoryUnix.cpp` | present (`ipc.c:436, 451`); names are a kernel table, 63 characters |
| `memfd_create` | Linux paths only | present (`ipc.c:423`), unused unless patched in |
| `ftruncate` | shared memory, files | present (`file.c:683`); shrinking a mapped shm is `EBUSY` |
| `mmap(MAP_SHARED)` of a shm object | shared memory | present |
| `mmap(MAP_PRIVATE)` or read-only `MAP_SHARED` of a file | cache reads | an eager copy of the file |
| `mmap(MAP_SHARED, PROT_WRITE)` of a file | SQLite WAL, cache blob writes | **`ENOTSUP`** (`mman.c:207-208`) |
| `munmap`, `mprotect`, `madvise` | memory | present; `msync`, `mlock` are no-ops |
| `fallocate` | `FileSystem.cpp:348-357` under `HAVE(FALLOCATE)` | absent; `posix_fallocate` exists |
| `posix_spawn`, file actions | new launcher | present (`proc.c:368`) |
| `fork` | not used | `ENOSYS` |
| `execve`, `execvp` | not used | defined as wrappers at `proc.c:516, 518`; the file header says the exec family fails with `ENOSYS` |
| `getpid`, `getppid` | logging | present |
| `kill` | new launcher | `SIGKILL`, `SIGTERM`, `SIGINT`, `SIGHUP`, `SIGQUIT` end the target; signal 0 probes; others `ENOTSUP` |
| `waitpid` | reaping | present; "any child" polls every 10 ms |
| `sigaction(SIGPIPE, SIG_IGN)`, `sigemptyset` | `AuxiliaryProcessMain.cpp:99-102` | present |
| `prctl`, `syscall` | Linux-only paths | absent; not reached without `OS(LINUX)` |
| `sem_init`, `sem_wait`, `sem_post`, `sem_timedwait` | not used by these files | present; `sem_open` is `ENOSYS` |
| `pthread_*`, `pthread_setname_np` | threads | present; default stack 8 MB, lazily mapped |
| `sysconf`, `getrlimit`, `getrusage` | WTF | present |
| `realpath` | WTF file system | present (`path.c:189`), gives the `SD:/a/b` form |
| `link` | cache blob storage | **fails, `EMLINK`** (`stat.c:382`) |
| `symlink`, `readlink` | `std::filesystem` | `EPERM` / `EINVAL` |
| `rename` | storage | present; replaces an existing target |
| `fsync`, `fdatasync`, `statvfs`, `utimensat`, `futimens`, `opendir`, `readdir` | cache, storage | present |
| `mkstemp`, `mkdtemp`, `getenv`, `gettimeofday`, `close`, `read`, `write` | misc | not in libonyxposix; expected from newlib over its syscall stubs (not checked) |
| `dlopen`, `dlsym` | PlayStation entry points and bundle only | stubs that fail |
| `getifaddrs` | not used by these files | `ENOSYS` |

---

## C. Drawing without a compositor

### Guards

| File | Guard |
|---|---|
| `WebProcess/WebPage/DrawingArea.cpp:63-83` | `DrawingArea::create` has a branch only for Cocoa, `USE(GRAPHICS_LAYER_WC)`, or `USE(COORDINATED_GRAPHICS) || USE(TEXTURE_MAPPER)`. With none set the function returns nothing. Same for `supportsGPUProcessRendering` (`:155-164`) and the includes (`:47-53`). |
| `DrawingArea.h:172-176, 229-234` | `updateGeometry`, `enterAcceleratedCompositingModeIfNeeded`, `backgroundColorDidChange`, `forceUpdate`, `didDiscardBackingStore` are declared only under `CG || TM` |
| `DrawingArea.messages.in:28-38` | `UpdateGeometry`, `ForceUpdate`, `DidDiscardBackingStore` under `CG || TM` |
| `Shared/UpdateInfo.h:28`, `UpdateInfo.serialization.in` | `!WPE && !GTK && (CG || TM)` |
| `UIProcess/DrawingAreaProxy.h:65, 172-175`, `DrawingAreaProxy.messages.in` | `Update` and `ExitAcceleratedCompositingMode` under the same condition |
| `UIProcess/BackingStore.h:28`, `skia/BackingStoreSkia.cpp:29` | `!WPE && !GTK`, `USE(SKIA)`: no compositor condition |
| `DrawingAreaCoordinatedGraphics.h:68`, `.cpp:230` | two overrides under `CG || TM` |
| `DrawingAreaCoordinatedGraphics.cpp:50-52, 58-60` | includes `LayerTreeHostPlayStation.h` or `LayerTreeHostTextureMapper.h`; with neither, `LayerTreeHost` is an incomplete type and the roughly 15 `m_layerTreeHost->…` calls do not compile |
| `DrawingAreaCoordinatedGraphics.cpp:447-454` | already has a no-compositor branch: `#else m_layerTreeHost = nullptr; return;` |
| `DrawingAreaProxyCoordinatedGraphics.{h,cpp}` | only `!WPE && !GTK` and `HAVE(DISPLAY_LINK)`: no compositor condition inside |
| `WebPage.cpp:5459-5464` | for every port except GTK, Windows, PlayStation and WPE, a false `acceleratedCompositingEnabled` is logged as unusable and **forced to true**. Onyx must be added to the list. |
| `WebPage.cpp:1024, 1732, 4742` | calls guarded by `CG || TM` (optional once the virtuals are visible) |

### Does the software path need Coordinated Graphics?

No. `DrawingAreaCoordinatedGraphics::display(UpdateInfo&)` (`.cpp:613-681`) creates a `ShareableBitmap`, paints the dirty rectangles with `webPage->drawRect`, and sends `DrawingAreaProxy::Update`. The UI side's `incorporateUpdate` (`DrawingAreaProxyCoordinatedGraphics.cpp:116-144`) hands it to `BackingStore::incorporateUpdate` (`BackingStoreSkia.cpp:63-95`), which paints into a raster `SkSurface` and scrolls with `readPixels` / `writePixels`. `paint(SkCanvas*, rect, unpaintedRegion)` (`:74-88`) draws the backing store into the embedder's canvas. None of this touches GL, TextureMapper or a layer tree. With `m_layerTreeHost` always null, `graphicsLayerFactory()` returns null and WebCore falls back to step 2's `GraphicsLayerOnyx`.

### Options

1. **Enable `USE_COORDINATED_GRAPHICS` for WebKit only**: not possible in isolation. It is a global define seen by WebCore, and in WebKit it pulls `LayerTreeHost*`, `ThreadedCompositor*`, `AcceleratedSurface*` and `NonCompositedFrameRenderer.cpp` (which includes `<epoxy/egl.h>`). Rejected.
2. **New `DrawingAreaOnyx` + `DrawingAreaProxyOnyx`**: about 350 + 250 lines. It would have to implement the pure virtuals `setNeedsDisplay`, `setNeedsDisplayInRect`, `scroll`, `updateRenderingWithForcedRepaintAsync`, `setRootCompositingLayer`, `triggerRenderingUpdate` (and handle `UpdateGeometry`, `DisplayDidRefresh`, `ForceUpdate`, `DidDiscardBackingStore`) on the web side, `sizeDidChange` and `deviceScaleFactorDidChange` on the UI side, and it would still need the guard changes for `UpdateInfo`, the `Update` message and `DrawingArea::create`. It duplicates 600 lines of upstream logic.
3. **Recommended: widen the guards and reuse the upstream classes.** One flag set by `OptionsOnyx.cmake` (or `PLATFORM(ONYX)`) beside `CG || TM` at about 17 sites in 10 files, a stub `LayerTreeHostOnyx.h` (~60 lines: a class with the dozen methods the drawing area calls, never instantiated), and `PLATFORM(ONYX)` at `WebPage.cpp:5459`. `PageClientImpl::createDrawingAreaProxy` returns `DrawingAreaProxyCoordinatedGraphics::create`. The upstream `#else` at `:447-454` shows the file is meant to work this way.

---

## D. The embedding API and the UI process side

### What PlayStation uses

- A view class, `PlayStationWebView` (an `API::Object` of type `View`): its constructor copies the page configuration, calls `processPool.createWebPage(*m_pageClient, configuration)` and `m_page->initializeWebPage(...)`. It keeps the size and the activity state (window active, focused, visible, in window) and calls `m_page->activityStateDidChange` on a change.
- A C layer: `WKViewCreate`, `WKViewGetPage`, `WKViewSetSize`, `WKViewSetFocus / Active / Visible`, `WKViewSetViewClient` (callbacks `setViewNeedsDisplay(rect)`, `setCursor`, full screen), and `WKPageHandleKeyboardEvent / MouseEvent / WheelEvent`, `WKPagePaint(page, pixels, size, rect)`.
- `Tools/MiniBrowser/playstation` (1685 lines) uses **only the C API**:
  - `main.cpp`: `WKRunLoopInitializeMain()`, the toolkit's update posted with `WKRunLoopCallOnMainThread`, then `WKRunLoopRunMain()`. The main thread is WTF's run loop.
  - `WebContext.cpp`: `WKContextConfigurationCreate`, process paths, a `WKWebsiteDataStoreConfiguration` with eight directories, `WKContextCreateWithConfiguration`, `WKContextSetPrimaryWebsiteDataStore`, `WKContextSetUsesSingleWebProcess(true)`, preferences.
  - `WebViewWindow.cpp`: `WKPageConfigurationCreate` / `SetContext` / `SetPreferences`, `WKPreferencesSetAcceleratedCompositingEnabled(false)`, `WKViewCreate`, a view client, `WKPageSetPageStateClient` (title, active URL, progress: this feeds the URL bar), `WKPageSetPageUIClient` (new page, JavaScript alert / confirm / prompt), `WKPageLoadURL`, `GoBack`, `GoForward`, `ReloadFromOrigin`, events through `WK*EventMake`, pixels through `WKPagePaint` into its own surface on `setViewNeedsDisplay`.
- Weak spots of the PlayStation code that Onyx should not copy: `WKPageHandleKeyboardEvent` ignores the text and the modifiers ("TODO: Handle modifiers", `WKPagePrivatePlayStation.cpp`), `handleEditingKeyboardEvent` is not implemented, and `createPopupMenuProxy` returns null (no `<select>` lists).

### Minimal set for Onyx

**`PageClientImpl`.** PlayStation's overrides about 60 methods. The ones that must do something:

- `createDrawingAreaProxy`, `setViewNeedsDisplay`, `viewSize`, `isViewWindowActive`, `isViewFocused`, `isActiveViewVisible`, `isViewInWindow`.
- `setCursor`, `toolTipChanged`.
- `doneWithKeyEvent` (hand unhandled keys back to the browser for its shortcuts).
- `createPopupMenuProxy`.
- `registerEditCommand`, `clearAllEditCommands`, `canUndoRedo`, `executeUndoRedo` (delegate to `DefaultUndoController`).
- `screenToRootView`, `rootViewToScreen` (the window's origin).
- `processDidExit`, `didRelaunchProcess` (a crash page).
- `requestDOMPasteAccess` (grant).
- `enterAcceleratedCompositingMode` and its two siblings: empty, never called.

Empty or returning null is fine for: `requestScroll`, `viewScrollPosition`, `pageClosed`, `preferencesDidChange`, `didCommitLoadForMainFrame`, `didChangeContentSize`, `setCursorHiddenUntilMouseMoves`, `wheelEventWasNotHandledByWebCore`, `convertToDeviceSpace / UserSpace`, the accessibility conversions, `createColorPicker`, `createDataListSuggestionsDropdown`, `createDateTimePicker`, the six navigation gesture methods, `didFirstVisuallyNonEmptyLayoutForMainFrame`, `didFinishNavigation`, `didFailNavigation`, `didSameDocumentNavigationForMainFrame`, `didChangeBackgroundColor`, `isPlayingAudio*`, `refView`, `derefView`, `didRestoreScrollPosition`, `didFinishLoadingDataForCustomContentProvider`. `userInterfaceLayoutDirection` returns left-to-right. With `ENABLE_FULLSCREEN_API` on (its state in the step 2 build), seven more full-screen methods to stub.

**Events.** `Shared/NativeWeb{Keyboard,Mouse,Wheel}Event.h` already have a generic constructor taking a `Web*EventInit` (the `#else` branches at `:133-137`, `:128-132`, `:110-114`); only the `create(...)` factories are per port (`PLATFORM(PLAYSTATION)` at `:91`, `:89`, `:77`). To write:

- `Shared/onyx/WebEventFactoryOnyx.{h,cpp}` (~60 + ~250): `createWebKeyboardEvent` (type, text, unmodified text, `key`, `code`, key identifier, Windows virtual key code, native code, auto-repeat, keypad, system key, modifiers, timestamp), `createWebMouseEvent` (button, buttons mask, position, global position, click count kept between calls, modifiers), `createWebWheelEvent` (delta, ticks, granularity).
- `NativeWebKeyboardEventOnyx.cpp`, `NativeWebMouseEventOnyx.cpp`, `NativeWebWheelEventOnyx.cpp` (~45 each), plus an `ONYX` branch in the three headers.
- Onyx does not define `USE(LIBWPE)`, so `Shared/WebKeyboardEvent.h:45-70` gives it the fuller event (with `unmodifiedText` and `isSystemKey`), as Windows.
- `WebProcess/WebPage/onyx/WebPageOnyx.cpp`: a real `handleEditingKeyboardEvent` with a key-to-editor-command table, on the model of `WebPageWin.cpp:225-257`.

**Pasteboard.** Step 2 shares WebCore's `PasteboardLibWPE.cpp` under `PLATFORM(ONYX)`. On the WebKit side the matching code is under `USE(LIBWPE)`: `WebPasteboardProxy.h:149`, `WebPasteboardProxy.cpp:115-122`, `WebPasteboardProxy.messages.in:83`, `Shared/Pasteboard.serialization.in:68`, `WebPlatformStrategies.h:97`, `WebPlatformStrategies.cpp:429-449`. Add `PLATFORM(ONYX)` at these six places, write `WebCore/platform/onyx/PlatformPasteboardOnyx.cpp` (~100, on `clipd`) in place of `PlatformPasteboardLibWPE.cpp`, and reuse `WebPasteboardProxyLibWPE.cpp` unchanged.

**The rest.**

- Popup menus: a `WebPopupMenuProxyOnyx` implementing `showPopupMenu(rect, direction, scale, items, data, selectedIndex)` and `hidePopupMenu`, answering through `valueChangedForPopupMenu` (~60 + ~150). Second milestone.
- Context menu: `ENABLE_CONTEXT_MENUS` is off in the current configuration; `createContextMenuProxy` exists only when it is on.
- Colour, date, data list pickers: null.
- File chooser, JavaScript dialogs, new windows: `WKPageUIClient` (`runOpenPanel`, `runJavaScriptAlert / Confirm / Prompt`, `createNewPage`).
- Navigation, TLS errors, authentication: `WKPageNavigationClient`. Title, URL, progress, back / forward state: `WKPageStateClient`.
- Downloads: `WKDownloadClient.h` / `WKContextDownloadClient.h`.
- Pixels to the window: on `setViewNeedsDisplay(rect)`, wrap the window canvas's pixels in an `SkCanvas` and call the drawing area proxy's `paint` (what `WKPagePaint` does: `SkSurfaces::WrapPixels` with N32 premultiplied sRGB), then present.
- Other small hooks: `Shared/API/c/WKBase.h:33-43` and `UIProcess/API/C/WKAPICast.h:447-449` select the platform header by compiler macro (`__SCE__`): add `__onyx__`. `WebPreferences.cpp:262`, `WebChromeClient.cpp:2341` / `WebChromeClient.h:279` (accessibility notification stubs), `WebInspectorBackendClient.cpp:154, 186` and `WebPage.cpp:2945` (inspector highlight, optional) have PlayStation lists to extend.

### C API or C++ classes

Recommendation: **an in-tree view layer in C++ (`UIProcess/onyx`, `UIProcess/API/C/onyx`), and the browser on the C API**. The C++ classes (`WebPageProxy`, `WebProcessPool`, `API::PageConfiguration`, the `API::*Client` interfaces) can only be used by code compiled inside WebKit's build: they need `config.h`, `cmakeconfig.h`, the prefix headers and identical feature flags. GTK, WPE and PlayStation all keep that code inside `Source/WebKit` for this reason. The C API is plain C types with forwarding headers, it is what MiniBrowser PlayStation uses, and it suits the "web view as a component other programs embed" direction recorded in the Onyx repository's log. Where the C API is too thin (key events with text, popups), extend the Onyx C files rather than reach into C++ from the app.

---

## E. The network process and storage

- **Compiled**: `NetworkProcess/curl/*` and `Cookies/curl/*` (1717 lines together), the cache with `NetworkCacheIOChannelCurl.cpp` (97) and `NetworkCacheDataCurl.cpp` (131). `NetworkProcessCurl.cpp`'s platform hooks are empty.
- **Cache I/O**: `IOChannel` uses `FileSystem::openFile`, `seek`, `read`, `write` on a work queue; no mapping. `Data` may hold a `MappedFileData`.
- **Cache blob storage** (`NetworkCacheBlobStorage.cpp:139-195`): a body is written with `Data::mapToFile` → `FileSystem::createMappedFileData` (`Source/WTF/wtf/FileSystem.cpp:329-357`: open, `truncate`, optional `fallocate`, then a shared writable mapping) and then **hard-linked** to the record's path (`FileSystem::hardLink`, `:164, 181`). `synchronize()` deletes blobs whose link count is 1 (`:116-119`). On Onyx the mapping is `ENOTSUP` and `link` is `EMLINK`, so bodies stored as blobs are never saved; only records with an inline body work. Reads use a private mapping, which works (as a copy).
- **Other storage**: `NetworkProcess/storage/*` uses `listDirectory`, `moveFile`, `fileModificationTime`, `updateFileModificationTime`, `fileCreationTime` (`NetworkStorageManager.cpp:432`), `hardLinkOrCopyFile` (`CacheStorageManager.cpp:183`, which falls back to a copy), and read mappings with a fallback to reading (`CacheStorageDiskStore.cpp:77-88`).
- **SQLite**: `Source/WebCore/platform/sql/SQLiteDatabase.cpp:190-192` calls `useWALJournalMode()` for every writable on-disk database and fails the open if it fails. WAL's index is a `-shm` file mapped shared and writable. Onyx's SQLite build notes say the same (`tools/ports/sqlite/build.sh:7-8`: use `locking_mode=EXCLUSIVE` with WAL, or the rollback journal). As written, `CookieJarDB` (`platform/network/curl/CookieJarDB.cpp:126`), IndexedDB, local storage and ITP would not open. This is read from the code, not run. Smallest fix: under `OS(ONYX)`, skip WAL in `SQLiteDatabase::open` (three lines) and keep the rollback journal. File locks are no-ops on Onyx; acceptable while one process owns each database.
- **Default directories**: `WebsiteDataStorePlayStation.cpp` puts everything under `/app0` unless a base directory is given; `WebsiteDataStore.cpp:2551-2700` picks the sub-directory names (`WebKitCache`, `storage`, `indexeddb`, `local`, `itp`, …) with PlayStation / GLib variants. `WebsiteDataStoreCurl.cpp` passes the cookie file and the proxy settings. Onyx supplies `WebsiteDataStoreOnyx.cpp` with a data base and a cache base (for example data on `SD:`, cache on `SD:` or `RAM:`), or the browser sets each directory through `WKWebsiteDataStoreConfiguration` as MiniBrowser does.
- **Certificates**: nothing OpenSSL-specific in `Source/WebKit`; the certificate info and protection space C files go through WebCore's types (step 2's mbedTLS glue and `CurlSSLHandleOnyx.cpp`).
- **FAT and path assumptions**:
  - Hard links: broken (above). Symbolic links: none; `std::filesystem::symlink_status` is only read.
  - File locking: no-op.
  - Writable shared file mappings: SQLite WAL and cache blobs (above). `IPC` and bitmaps use shm objects, not files.
  - Modification time: FAT's 2-second precision is harmless for the cache's hour-scale use.
  - Names: cache records are 40-character hex names; long file names are needed, mixed case is not.
  - `SD:/…` paths: `FileSystem::realPath` (`FileSystem.cpp:748-752`) uses `std::filesystem::canonical`, and `WebsiteDataStore.cpp:418-462` resolves every directory. A path starting with `SD:` is not absolute for libstdc++. `realPath` returns its input on error, so this is probably benign, but I did not verify it; rooted paths such as `/var/webkit` (resolved by the kernel against the working directory's volume, `path.c`) avoid the question.

---

## F. Feature flags

- PlayStation sets: `ENABLE_GPU_PROCESS` off, `ENABLE_WEBGL` off, `ENABLE_ASYNC_SCROLLING` off, `ENABLE_CONTEXT_MENUS` off, `ENABLE_WEB_AUDIO`, `GEOLOCATION`, `NOTIFICATIONS`, `MATHML`, `XSLT`, `USER_MESSAGE_HANDLERS` off; `ENABLE_FULLSCREEN_API`, `REMOTE_INSPECTOR`, `RESOURCE_USAGE`, `PERIODIC_MEMORY_MONITOR`, `SMOOTH_SCROLLING` on; `ENABLE_WEBDRIVER` only with the WPE backend; `ENABLE_MINIBROWSER` = `ENABLE_WEBKIT` in developer mode; `USE_LIBWPE`, `USE_OPENSSL`, `USE_TEXTURE_MAPPER`, `USE_COORDINATED_GRAPHICS` (or the WC layer with a GPU process), `USE_UNIX_DOMAIN_SOCKETS`, `USE_INSPECTOR_SOCKET_SERVER`.
- There is no `ENABLE(SERVICE_WORKER)` any more (zero occurrences in `Source/WebKit`): service workers are always compiled and are a runtime preference. `WebProcessPool.cpp:705-787` reuses an existing web process for them unless `s_useSeparateServiceWorkerProcess` is set (default false).
- For step 3, `OptionsOnyx.cmake` should:
  - add an option (say `ONYX_WEBKIT`) that sets `ENABLE_WEBKIT ON` (now `OFF` at line 179) and `WebKit_LIBRARY_TYPE STATIC`;
  - set and expose the flag that opens the software drawing path (section C);
  - keep off: `GPU_PROCESS`, `WEBGL`, `ASYNC_SCROLLING`, `VIDEO`, `WEB_AUDIO`, `REMOTE_INSPECTOR`, `WEBDRIVER`, `CONTEXT_MENUS`, `DRAG_SUPPORT`, `TOUCH_EVENTS`, `GAMEPAD`, `NOTIFICATIONS`, `GEOLOCATION`, `MINIBROWSER` (the browser lives in the Onyx repository), `API_TESTS`;
  - leave `ENABLE_FULLSCREEN_API` as it is (on in the step 2 build cache) and stub the seven methods, or turn it off at the price of rebuilding WebCore.
- At run time, from the embedder: `WKContextSetUsesSingleWebProcess(true)`, no process swap on navigation and no prewarming (`APIProcessPoolConfiguration.h:188-197`; these already default to false), accelerated compositing, forced compositing and threaded scrolling off (`WebPreferencesOnyx.cpp`), service workers disabled at first.
- **Unconditional GL / EGL / libwpe / GLib in `Source/WebKit`**: I found none in the generic sources. Of the 949 `.cpp` files in `Sources.txt`, 14 mention such headers and every occurrence I inspected is guarded (`ENABLE(GPU_PROCESS)`, `ENABLE(WEBGL)`, `USE(COORDINATED_GRAPHICS)`, `USE(SOUP)`, `PLATFORM(GTK)`). `CMakeLists.txt:799-815` adds `${EGL_INCLUDE_DIRS}` and `${EGL_LIBRARIES}` for ports without epoxy or ANGLE; both are empty for Onyx. So there is no equivalent of step 2's Skia guard patch; the patch here goes the other way (opening the software path). Caveat: I matched include lines in `.cpp` files only, not headers transitively.

---

## G. Size and order of work

| # | Work | Size |
|---|---|---|
| 1 | `Source/WebKit/PlatformOnyx.cmake`; `OptionsOnyx.cmake` changes; `onyx` in `generate-forwarding-headers.pl`; `tools/webkit/build-webkit.sh` | ~130 + ~30 + 1 line; a script |
| 2 | Guard patches: drawing area (17 sites / 10 files), `WebPage.cpp:5459`, connection (3 sites + `shutdown` + `EPIPE`), launcher options and process paths (6 sites), native events (3), pasteboard (6), `WKBase.h` / `WKAPICast.h`, small PlayStation lists (5) | ~45 hunks in ~25 files |
| 3 | Web process files: `WebProcessMainOnyx`, `WebProcessOnyx`, `WebPageOnyx`, `InjectedBundleOnyx`, `LayerTreeHostOnyx.h` | ~470 |
| 4 | UI process files: `ProcessLauncherOnyx`, `PageClientImpl`, `OnyxWebView`, `APIViewClient.h`, `WebPageProxyOnyx`, `WebProcessPoolOnyx`, `WebPreferencesOnyx`, `WebsiteDataStoreOnyx` | ~1250 |
| 5 | C API files: `WKView`, `WKViewClient.h`, `WKPagePrivateOnyx`, `WKContextConfigurationOnyx`, `WKRunLoop`, `WKAPICastOnyx.h`, `WKBaseOnyx.h`, `WKEventOnyx` | ~1100 |
| 6 | Events: `WebEventFactoryOnyx`, three `NativeWeb*EventOnyx.cpp` | ~450 |
| 7 | Get `libWebKit.a`, `WebProcess`, `NetworkProcess` to compile and link (`ninja -k 0`, errors by family, as steps 1 and 2) | the unknown part |
| 8 | A headless test program on the C API (`wk2test`: context, view, load, `WKPagePaint` to a PNG) and `test-webkit.sh` | ~300 |
| 9 | SQLite WAL patch; cache blob storage without links (copy and write instead of link and map), or the disk cache left off | 3 lines; ~80 |
| 10 | `PlatformPasteboardOnyx.cpp` on `clipd`; `WebPopupMenuProxyOnyx` | ~100; ~210 |
| 11 | The browser itself: window, canvas, events, run loop glue, URL bar, on the C API (Onyx repository) | not estimated |

Total new WebKit-side code: about 3,400 lines in about 30 files, plus about 45 patch hunks.

**Milestones.**

1. `libWebKit.a` and the two auxiliary programs link.
2. On the bench: the UI test program starts a network and a web process, loads `about:blank`, and all three exit cleanly when the page is closed (this exercises the launcher, the connection monitor and the close path).
3. On the bench: a `file:` page is painted through `UpdateInfo` into the UI process's backing store and saved as a PNG, compared with step 2's `page1.html` checks.
4. On the bench: an `http` / `https` page from a local server (first real use of the curl + mbedTLS path), then cookies and storage (the SQLite patch).
5. On the Pi: the same test program; then the window, input, clipboard.

**Risks**, most serious first:

- The compile-and-link pass over 949 generic files plus about 200 generated ones in a configuration no port uses (no compositor, no libwpe, no GLib). Step 2 needed 24 files of guards in WebCore; expect a comparable tail here.
- The connection close path (the `poll` reference) and thread use: one monitor thread per connection plus one thread per work queue.
- Start-up time and memory with three large static images, until the lazy loader and shared code pages exist.
- SQLite and the disk cache on FAT (section E).
- Run-loop integration in the UI process: IPC replies arrive on WTF's main run loop, and Onyx's window events must be fed into it (a forwarding thread, or a run-loop timer that drains the event queue). I did not study Onyx's window API.

**Bench versus Pi.** The posixsim bench (`tools/tests/posixsim/fakekapi.c` header) emulates v76: local sockets over Linux socketpairs with `SCM_RIGHTS`, shm over memfd, `spawn_ex2` with inherited descriptors, a spawned program run under qemu. So milestones 2 to 4 are bench work. The `link` failure, the `ENOTSUP` mapping and the `poll` reference are all in libonyxposix, so they reproduce there. The bench's DNS knows only `localhost` and numeric addresses, and its header says it does not emulate windows, sound, app cores, FAT's case-insensitivity or the RAM volume's limits. Only on the Pi: the real kernel's local sockets and shm under load, timing, the loader with three large images, memory, FAT, the window, input and clipboard.

---

## Recommended configuration and architecture

Build `Source/WebKit` as a static `libWebKit.a` with a new `PlatformOnyx.cmake` modelled on PlayStation's list minus everything libwpe, EGL and compositor: the Unix IPC files with the PlayStation `select` monitor extended to Onyx (plus a `shutdown` before the close), `SharedMemoryUnix` as it is, a `posix_spawn` launcher that passes the socket's descriptor number in `argv` to the stock `AuxiliaryProcessMain`, the curl network process unchanged, and the upstream software drawing path (`DrawingAreaCoordinatedGraphics` → `UpdateInfo` → `DrawingAreaProxyCoordinatedGraphics` → `BackingStoreSkia`) opened by a guard patch rather than rewritten. The UI side is an in-tree `OnyxWebView` + `PageClientImpl` exposed through a small C API beside WebKit's generic `WK*` API; the browser, in the Onyx repository, uses only that C API, paints with `WKPagePaint` into its window canvas, and sends key, mouse and wheel events through Onyx-specific event functions that carry text and modifiers. One UI process, one web process (`usesSingleWebProcess`), one network process; GPU process, compositing, WebGL, media, inspector and WebDriver off.

## Files to write

- **CMake**: `Source/WebKit/PlatformOnyx.cmake`; edits to `Source/cmake/OptionsOnyx.cmake`.
- **Web process**: `WebProcess/onyx/WebProcessMainOnyx.cpp`, `WebProcess/onyx/WebProcessOnyx.cpp`, `WebProcess/WebPage/onyx/WebPageOnyx.cpp`, `WebProcess/InjectedBundle/onyx/InjectedBundleOnyx.cpp`, `WebProcess/WebPage/CoordinatedGraphics/LayerTreeHostOnyx.h`.
- **UI process**: `UIProcess/Launcher/onyx/ProcessLauncherOnyx.cpp`, `UIProcess/onyx/PageClientImpl.{h,cpp}`, `UIProcess/onyx/OnyxWebView.{h,cpp}`, `UIProcess/API/onyx/APIViewClient.h`, `UIProcess/onyx/WebPageProxyOnyx.cpp`, `WebProcessPoolOnyx.cpp`, `WebPreferencesOnyx.cpp`, `UIProcess/WebsiteData/onyx/WebsiteDataStoreOnyx.cpp`, later `UIProcess/onyx/WebPopupMenuProxyOnyx.{h,cpp}`.
- **C API**: `UIProcess/API/C/onyx/WKView.{h,cpp}`, `WKViewClient.h`, `WKPagePrivateOnyx.{h,cpp}`, `WKContextConfigurationOnyx.{h,cpp}`, `WKRunLoop.{h,cpp}`, `WKAPICastOnyx.h`; `Shared/API/c/onyx/WKBaseOnyx.h`, `WKEventOnyx.{h,cpp}`.
- **Events**: `Shared/onyx/WebEventFactoryOnyx.{h,cpp}`, `NativeWebKeyboardEventOnyx.cpp`, `NativeWebMouseEventOnyx.cpp`, `NativeWebWheelEventOnyx.cpp`.
- **WebCore**: `platform/onyx/PlatformPasteboardOnyx.cpp`; the WAL patch in `platform/sql/SQLiteDatabase.cpp`.
- **Entry points**: reuse `EntryPoint/unix/*`, or one multi-call `main`.
- **Onyx repository**: `tools/webkit/build-webkit.sh`, `test-webkit.sh`, `wk2test.cpp`; the browser.
- **Patched upstream files**: `Platform/IPC/Connection.h`, `Platform/IPC/unix/ConnectionUnix.cpp`, `Shared/UpdateInfo.h`, `UpdateInfo.serialization.in`, `UIProcess/DrawingAreaProxy.{h,messages.in}`, `WebProcess/WebPage/DrawingArea.{h,cpp,messages.in}`, `DrawingAreaCoordinatedGraphics.{h,cpp}`, `WebProcess/WebPage/WebPage.cpp`, `Shared/NativeWeb{Keyboard,Mouse,Wheel}Event.h`, `UIProcess/WebPasteboardProxy.{h,cpp,messages.in}`, `Shared/Pasteboard.serialization.in`, `WebProcess/WebCoreSupport/WebPlatformStrategies.{h,cpp}`, `UIProcess/Launcher/ProcessLauncher.h`, `UIProcess/WebProcessProxy.cpp`, `UIProcess/API/APIProcessPoolConfiguration.{h,cpp}`, `UIProcess/WebProcessPool.h`, `UIProcess/WebPreferences.cpp`, `WebProcess/WebCoreSupport/WebChromeClient.{h,cpp}`, `Shared/API/c/WKBase.h`, `UIProcess/API/C/WKAPICast.h`, `Scripts/generate-forwarding-headers.pl`.

## Open decisions for the project owner

1. **Three programs or one multi-call binary.** Recommendation: build the three upstream targets for the bench milestones (no design risk), keep the launcher's path and role flag configurable, and ship one binary once the lazy loader and shared code pages exist; before that, three smaller programs load faster.
2. **Drawing path.** Recommendation: the guard patch reusing upstream classes (option 3), not a new drawing area pair.
3. **Embedding API.** Recommendation: the C API for the browser and other embedders, with the C++ view layer kept inside the WebKit tree.
4. **Connection monitor.** Recommendation: take the PlayStation `select` thread now with the `shutdown` fix; consider one shared `poll` thread for all connections only if thread count becomes a problem.
5. **SQLite journal.** Recommendation: skip WAL under `OS(ONYX)` in `SQLiteDatabase::open`. Alternatives: `locking_mode=EXCLUSIVE` before WAL, or writable shared file mappings in libonyxposix.
6. **HTTP disk cache.** Recommendation: leave it off for the first milestones, then patch the blob storage to copy and write instead of link and map (about 80 lines).
7. **Full-screen API.** Recommendation: leave the flag on and stub the client, to avoid a WebCore rebuild.
8. **Data directories.** Recommendation: rooted paths (`/var/webkit/...`) set by the browser through the data store configuration, cache separate from data, until the `SD:` behaviour of `std::filesystem` is checked.
9. **Shared memory creation.** Optional: an `OS(ONYX)` branch calling `memfd_create` directly in `SharedMemoryUnix.cpp` (saves the name table round trip), and a larger inline message size.
10. **UI run loop.** WTF's main run loop must own the UI thread; how Onyx window events reach it (forwarding thread or timer) needs someone who knows the window API. I have no recommendation without reading it.

## What I could not verify

- Nothing was compiled or run: the STATIC build of WebKit, the guard patch, the stub layer tree host and every size estimate are from reading.
- The PlayStation SDK pieces (`<process-launcher.h>`, the WPE backend, the toolkit) are not in the checkout; their behaviour is inferred from the call sites.
- The effect of closing a socket during `select` is derived from libonyxposix's `poll` and `close`; I did not read the kernel's local socket code or test it.
- The SQLite WAL failure is inferred from `SQLiteDatabase.cpp`, `mman.c` and the port's build notes, not observed.
- `std::filesystem` with `SD:/` paths, and newlib providing `mkstemp`, `mkdtemp`, `getenv`, `gettimeofday`.
- Whether pthreads of a process all run on core 0 (suggested by `user/kapi.h:622-634`, which reserves cores 2 and 3 for "app core" code): relevant to performance, not checked.
- The GL / libwpe / GLib scan covered include lines of the `.cpp` files in `Sources.txt`, not headers or generated code.
- I did not read the WPE and GLib launchers or `Tools/MiniBrowser/wpe`; for the argv convention I relied on `AuxiliaryProcessMain.cpp` and POSIX-PLAN §14.1. I did not read `Headers.cmake`, the full `PageClient.h` defaults, the network process's curl sources in detail, or Onyx's window, canvas and `clipd` interfaces.
- The WSL Onyx checkout's head (`228d1562`) differs from the commit list given for the Windows checkout; I read the WSL one.
