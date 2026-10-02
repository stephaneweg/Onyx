# Onyx: loading large programs faster (a study and a plan)

*Status: a study (2026-10-02), read from the code — nothing measured on the Pi, nothing built.
Why: the WebKit port's programs are 33 MB (`jsc`) to 80 MB (`wctest`, WebCore) static images; on
the Pi `wctest` takes 5 to 6 seconds from the command to its end, `jsc` 1 to 2 before it runs
anything, and the browser to come starts three processes (docs/08-WEBKIT-PORT.md). The user asked
whether a lazy loader (pages read from the file when first touched) would help. Decisions are the
user's: listed at the end.*

## Findings

1. **The time is almost certainly the SD read.** The card reads about 16 MB/s end to end
   (docs/04, High Speed mode; PIO, 512-byte blocks through the data port, no DMA:
   `circle/addon/SDCard/emmc.cpp`): 80 MB → 5 s, 33 MB → 2 s, which is what is observed. The copy
   into the frames and their zeroing are noise. An inference until measured (below).
2. **The image is 99.8 % read-only.** `wctest`: one `LOAD R E` segment of 79.9 MB (`.text` 50.8 MB,
   `.rodata` 22.1 MB with ICU's data, 15.9 MB — `tools/ports/icu` assembles it into `.rodata` —,
   `.eh_frame` 6.8 MB) and a `LOAD RW` of 169 KB in the file. `jsc` has the same shape. So nearly
   everything can be shared between processes, and no copy-on-write is needed.
3. **File pages map 1:1 onto memory pages**: `p_offset` and `p_vaddr` are multiples of 64 KB
   (`onyx-posix.ld`, `-z max-page-size=0x10000`): a page is 128 whole sectors, read in place.

## The loader today

`CUserProcessTask::Run` (`kernel/kernel.cpp`): a new address space; `f_open`; **the whole file**
into a kernel heap buffer (`new u8[f_size]`, sections never loaded included), read in 128 KB pieces
with a yield after each (Circle's FatFs over its SD driver); `LoadELF` (`kernel/proc/elf.cpp`)
maps a zeroed frame per 64 KB page and copies the segment into it; one cache synchronisation; the
buffer freed; the stack and heap as lazy regions; `El0Enter`. Each byte moves twice.

The demand paging that exists (kapi v75 / v76, `kernel/sys/vm.cpp`): lazy regions for anonymous
memory, the heap, stacks and shared-memory objects, filled by `VmFaultIn`, which **never yields**;
data aborts only (an instruction abort kills the process); no page cache, no eviction; no
file-backed mapping (the POSIX layer's file `mmap` is an eager copy in user space). The v76
shared-memory objects — reference-counted frames mapped, not owned, in several address spaces — are
the template for a shared image.

## The stages

| Stage | What | Size | kapi | Gain | Risk |
|---|---|---|---|---|---|
| **(a) streaming loader** | read the headers, then each segment straight into its frames (no whole-file buffer, no copy, no unloaded sections) | ~200 lines | none | the 80 MB kernel-heap peak goes; time about 2 % | low; a host test on a RAM disk |
| **(c) shared image** | a reference-counted image object keyed by (volume, first cluster, size, mtime) holding the read-only segment's frames: the first process streams the file into it, the next ones of the same file map it; a start during the load waits | ~350 lines + tests | none | the 2nd and 3rd process start at once: three processes 15 s → 5 s, 240 MB → 80 MB | low: no fault path changes, programs stay whole in memory |
| **(d) kept after exit** | the object stays at zero references until memory is wanted or the file is unlinked, renamed or written | ~100 lines + hooks | none | a relaunch is near-instant | low |
| **(b) lazy fill** | image regions filled from the file on first touch: per-page absent / loading / ready, an extent map of the file and direct sector reads, instruction aborts handled, cache maintenance per page, a size threshold | 800–1000 lines + tests | none | the only stage that shortens the *first* start: to (the fraction touched) × 5 s | real, below |

The risks of (b): the kernel's probes and fault-safe copies of user memory promise never to yield
(a string literal in an unread page passed to a system call: restart the call, or audit every
kapi); the EL1 safety net cannot do I/O; two faults on one page while the first read has yielded;
and **file replacement** — `pkg` replaces a program by unlink + rename and relies on programs
being whole in memory: a freed cluster could be reused under a running process (hide the file as
the open-file layer does, or read the rest before the unlink). With FatFs's cluster map set,
multi-cluster reads are not merged: page-ins must call the disk layer from the extent map.

## Recommendation

1. **Measure first** (an hour): in `CUserProcessTask::Run`, the time of the read loop (with the SD
   driver's wait / copy counters that `ChunkedRead` already logs), of `LoadELF`, and the process's
   lifetime at teardown; `wctest` printing its own clock at `main` and at its end; `fsbench` on the
   file itself; and, on the PC, the fraction of the image a run touches (resident 64 KB windows
   under qemu).
2. **(a) + (c) together, then (d)**: no fault path, no kapi change, `pkg`'s assumption kept — and
   most of what the browser needs: one read and one copy in memory for its processes. On a 1 GB Pi
   three private 80 MB images do not fit: sharing is a condition there.
3. **(b) only if the touched fraction is clearly under half**, built on (c)'s image object.
4. The small host front end the web view attaches to (docs/08) covers the perceived first start,
   whatever is done here.

## For the user to decide

- **One multi-call executable for the browser's processes, or three**: sharing is per file — one
  executable is what makes (c) pay.
- Whether a first start of about 5 s is acceptable with instant further processes and relaunches
  (and the host window at once); if not, (b).
- For (b): restart the system call or let the probes yield; hide or complete the file on
  replacement; the size threshold.
- How much to keep cached after exit, on 1 GB above all.
- The SD driver as a separate lever (UHS-I, DMA): every read would gain; not studied.

## Not verified

Any timing on the Pi (the 16 MB/s figure is the docs', the conclusion arithmetic); the fraction of
the image a run touches; the card's cluster size and the file's fragmentation; which UHS mode the
SD driver reaches; how many kapis have a side effect before a copy-in; whether stale instruction
cache on the app cores is already an issue.
