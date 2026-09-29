# Idea: threads in the BASIC VM

*Status: an idea, not started (2026-09-29). Builds on the kernel's threads (kapi v67,
docs/02 §7, docs/03 §5.2).*

**The idea**: a BASIC thread is a kernel thread running the VM's fetch-execute loop with its
own registers — its own value stack, call frames and program counter — over the program's shared
globals. Useful for the same reason as the kernel threads: **concurrency** — a thread waits (a
file, the network, `SLEEP`, a long computation) while the main one keeps the screen and the
keyboard alive.

## 1. Split the VM's state (`user/basic/basvm.cpp`, class `VM`)

| Per thread (a new `Ctx`, the VM works on `cur->…`) | Shared (stays in `VM`) |
|---|---|
| the value stack (`stack`, `sp`) | the program (`P`: bytecode, types, procs, DATA) |
| the SUB / FUNCTION frames (`frames`, `nf`) | the globals (`G`) |
| the GOSUB stack (`gosubs`, `ngs`) | the files (`files[]`), `ENVIRON` |
| `pc`, `opPc`, `failed`, `ended` | the graphics state (mode, VIEW, WINDOW, DRAW), `Scene3D` |
| the error state (`ON ERROR`, `ERR`, `ERL`, `RESUME`) | the events (`ON TIMER`, `ON KEY`) |
| the current PRINT / INPUT channel, `inBuf` | `DATA` pointer, `RND` state (or per thread) |

The main part of the work: mechanical, but the VM is ~2,400 lines.

## 2. A global interpreter lock (GIL)

The reference counts of strings, arrays and records (`Str::ref`, `Arr::ref`, `Rec::ref`) are
plain `++` / `--`: two threads touching the same global string under preemption would corrupt
the heap. So one lock (a kapi mutex, or `kapi_lock`): **one BASIC thread runs bytecode at a
time**; it gives the lock up every N instructions and around every blocking host call
(`SLEEP`, `INPUT`, `INKEY$` waits, file and network I/O, `PLAY` waits).

It costs nothing here: all of a process's threads run on core 0 anyway — what threads bring is
the concurrency while one waits, which the GIL keeps.

## 3. Syntax (QBasic has none — to invent)

- `T = THREAD(MySub, arg)` — start `SUB MySub (arg)` in a new thread → its handle.
- `JOIN T` (or `JOIN T, timeout`) — wait for it; `THREADDONE(T)` to poll.
- `LOCK m` / `UNLOCK m` — mutexes (`m = MUTEX`).
- `SIGNAL e` / `WAITFOR e [, timeout]` — events (`e = EVENT`).
- In the spirit of the event pump: **`ON THREAD T GOSUB Done`** — a thread's end becomes an
  event of the main thread, like `ON TIMER` (the kernel's `kapi_post`).

Compiler (`bascomp.cpp`): the statements and functions above, a few new opcodes; `.bax`
(`basbax.cpp`) gets a version bump.

## 4. Rules

- The screen, the keyboard and the events (`INKEY$`, `ON KEY`, `ON TIMER`) belong to the main
  thread; the others post to it (`ON THREAD`, shared variables under `LOCK`).
- `ON ERROR` is per thread; an error not trapped in a thread ends that thread (its `JOIN`
  reports it: `ERR`), not the program.
- The program ends with its main thread (as the kernel's processes).

## 5. Where it runs

The same VM serves `SD:/bin/basic`, `qbasic.app` and the `.bax` programs compiled by
`tools/basc` — all three get it at once.
