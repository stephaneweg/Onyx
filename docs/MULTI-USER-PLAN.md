# Onyx: several users — accounts, sessions, `/home`, rights on FAT, protected remote access

*Status (2026-10-05): **set aside by the user, as an idea.** After reading this study the user decided
that Onyx stays a **simple, single-user system: no multi-user** — do not start any of it unasked, and
do not propose it again. The page is kept as a record (its findings on the code — section 1 — remain
true: no caller check on the kapi, remote services without a password, `kapi_random` not
cryptographic). What follows is the study as written: nothing is built, no decision of section 2 was
taken.*

*The study: the user asked to "see how" Onyx can
become multi-user. This page gives the findings (section 1), a proposed design (sections 3–10), the
**decisions the user has to take** (section 2: none is taken yet — every "recommended" below is a
proposal), the steps (section 11) and the tests on the Pi (in each step). No system code is to be
written before the user has validated the plan. Its models: `docs/SHARED-LIBS-PLAN.md`,
`docs/GUI-USERSPACE-STUDY.md`. Answer the user in French; this page stays in English.*

## 0. What the user asked for

1. **User accounts and a login**: the desktop (menu bar, dock, notifications, session daemons) starts
   **when a user's session opens**, not at boot. A login screen at boot; log out; switch user.
2. **`/home/<user>/`**: the user's documents and own settings (today everything is global on the card).
3. **Rights, even on FAT**: an **index in `/etc`** of the particular rights given on files / folders to
   users, **enforced by the kernel** (the VFS), not by the apps.
4. **Remote access protected per user**: `telnetd`, Onyx Remote (`rdpd`), `vncd`, `ftpd`, and any other
   listener.

## 1. Findings (the code as it is, kapi v83)

### 1.1 Boot and "session"

- The kernel starts its own tasks (reaper, input, **compositor and window manager — in the kernel**,
  `kernel/gui/`; the network stack too), then `CKernel::StartAutostart` (`kernel/kernel.cpp:1929`)
  runs `init=` of `cmdline.txt` (`SD:/bin/init`).
- `user/BinUtils/init.c` (89 lines) reads `SD:/etc/autostart`, runs each line with `kapi_exec` (builtins
  `sleep`, `wait`) and **exits**: nothing is supervised or restarted.
- `sdcard/etc/autostart` mixes what is the machine's and what is a desktop's:

  | Line | Belongs to |
  |---|---|
  | `wait pkg commit`, `run pkgd`, `keyb FR`, `telnetd`, `vncd`, `rdpd`, `preload /boot` (+ `printd` when the printing work lands) | **the system** |
  | `run voronoy` (the wallpaper), `run menubar`, `run notifyd`, `run clipd`, `run dock`, `run agenda` | **a session** |
  | `run setup` (first boot; holds the `#setup:` lines back, then rewrites the file) | first boot |

- **There is no log out**: the only end of a session is a restart or a halt (`kapi_shutdown`,
  `kapi_reboot`: no check of the caller).
- `lock` (`user/Apps/lock/main.cpp`, 135 lines): a full-screen app; its PIN is **in clear** in
  `SD:/etc/lock.ini`; it is an ordinary process — any process (a telnet session) can kill it.
- `setup` (the first-boot wizard) creates no account. It turns `telnetd` / `vncd` / `rdpd` / `ftpd` on
  or off by commenting autostart lines.

### 1.2 Processes

- A process is a `CAddressSpace` (`kernel/include/kern/addrspace.h:49`): pid, parent pid, cwd, args,
  handles, one window, the `TProcInfo` (argv / environment, v75). **No owner, no privilege.**
- Environment variables exist (`SD:/etc/environment`, else `HOME=SD:/home PATH=SD:/bin
  TMPDIR=RAM:/tmp`); libc's `getuid ()` returns 0, its passwd name is `onyx`; `SD:/home` does not exist.
- Five ways to start a program, which do not inherit the same things — an identity has to go through
  **all** of them:

  | Call | Environment | cwd | Parent |
  |---|---|---|---|
  | `kapi_launch (name)` | the system's default | root | 0 |
  | `kapi_exec` / `exec_as` | the caller's | root | 0 |
  | `kapi_spawn` | the caller's | the caller's | the caller |
  | `spawn_ex` / `spawn_ex2` (`struct kapi_spawn_attr`, 64 bytes, `flags` and two reserved words free) | given or the caller's | given or the caller's | the caller |

  A process started by `launch` / `exec` has parent 0: it is never an orphan, so "the session's
  processes" cannot be found by the process tree — a **session number** is needed.
- EL0 (v74, `docs/EL0-PROTECTED-MODE.md`): each app in its own address space, every pointer checked,
  handles per process. But **no call is filtered**: any app may call `reboot`, `shutdown`, `kill_pid`
  (any process), `inject_pointer` / `inject_key`, `screen_grab`, `win_list` / `win_read` / `win_close` /
  `win_raise`, `vfs_register`, `tcp_listen` on any port. That document already says "need a permission
  model". Windows and surfaces record their owner's pid (`CWindow::m_nOwnerPid`).
- IPC: named services (`ipc_register`: the first live process to ask gets the name), mailbox messages
  carry the sender's pid, set by the kernel (`mailbox_recv (&from_pid, ...)`) — a service can therefore
  learn who is asking, once a pid has an owner.

### 1.3 Files

- **One choke point**: `ResolvePath` (`kernel/sys/kapi.cpp:85`) — every path-taking call of `kapi.cpp`,
  `ofile.cpp` (the v75 layer) and `procx.cpp` goes through it; it yields `SD:/a/b` (volume upper-cased,
  `.` / `..` resolved, relative to the process's cwd). Then: `RAM:` → the ramfs, a provider prefix
  (`FTP:`, `FTPS:` → `SD:/bin/ftpfs`) → the provider, else FatFs (FAT32, exFAT; `SD:`…`SD3:`, `USB:`…).
- **Ways around it** (to close): `kapi_exec` / `exec_as` (raw path to `ExecPath`), `image_preload` /
  `unload` and `lib_open` (`ImageCanonPath`), the provider test made on the unresolved string, and the
  kernel's own `f_open`s.
- No alias (`~`, `$HOME`) in the kernel. `kapi_app_dir` gives `SD:apps/<task>.app/`.
- Protection today: FAT's read-only attribute only (v75 layer). `fslock.cpp` is the volume lock, not a
  rights mechanism. There is **no kapi for raw sectors, mount or format** (good: nothing to close there).
- The kernel itself reads / writes in `SD:/etc`: `system.ini`, `theme.txt`, `wpa_supplicant.conf`,
  `environment`, `clock`, the crash and hang reports, the `net-trial.*` files.

### 1.4 Hard-coded paths (the size of the migration)

- **No common settings library.** AppKit's `.ini` reader (`app_ini_load`, read-only; it was `user/applib.h`) is used by 14 apps;
  about 25 others open a literal path with their own parser; shared headers do the same
  (`wallpaper.h`, `dockconf.h`, `fileassoc.h`, `launch.h`, `gamepad.h`, `ftpfs.h`, `uikit/theme.cpp`,
  `uikit/root.cpp`). uikit has no "where is my configuration" call.
- What is clearly **a user's**: `SD:/etc/` `theme.txt`, `wallpaper.ini`, `dock.ini`, `quicklaunch.txt`,
  `shelf.ini`, `places.ini`, `lock.ini`, `screenshot.ini`, `sound.ini`, `ftpfs.ini` (saved logins),
  `mail/*`, `media/*`, `pdf/*`, `photos`, `web-*` (Jet); the data folders `SD:/mail`, `SD:/courier`,
  `SD:/koton`, `SD:/lisa-chat.txt`; the documents `SD:/docs` (≈ 48 literals), `SD:/Documents`,
  `SD:/Downloads`, `SD:/pictures`, `SD:/music`, `SD:/videos`, `SD:/basic`, `SD:/projects`; the Trash
  `SD:/.Trash` (`user/trash.h`); **state written inside the app bundles** (`SD:/apps/<x>.app/`
  `config.ini`, `recovered.*`, `last.txt`, `agenda.txt`, `servers.txt`, `settings.ini`… — 15 apps).
- What is **the system's**: `system.ini`, `wpa_supplicant.conf` (Wi-Fi passwords in clear),
  `autostart`, `keymaps/`, `fileassoc.ini`, `runners.ini`, `preload.ini`, `gamepad.ini`, `pkg/`,
  `SD:/var/pkg`, fonts, `res/`, `wallpapers/`, `apps/`, `bin/`, `lib/`, `cmdline.txt`.
- File dialogs: `uikit::FileDialog` (`user/Kits/uikit/dialog.cpp`), ≈ 87 call sites in 28 apps; the start folder
  is `SD:/` when none is given (22 sites), `SD:/docs` (20+). The File Viewer starts at `SD:/`; its
  sidebar has no "Home".
- The clipboard is the `clipd` service (its memory + files in `RAM:/clip`): one for the whole system.

### 1.5 Network listeners

| Program | Port | Authentication today | What the peer gets |
|---|---|---|---|
| `telnetd` | 23 | **none** | a `cmd` shell on the whole card (8 sessions, a thread each, pipes to `cmd`) |
| `vncd` | 5900 | RFB 3.8, security type **None** | the whole screen, pointer and keys |
| `rdpd` | 3390 | **none** (`"ONYXRDP1"` + options byte; bits 3–7 free) | every window's pixels, pointer and keys, raise / close |
| `ftpd` | 21 (+ 50000–50099) | USER / PASS **in clear**; its users in memory, given as arguments (default `onyx` / `onyx`), passed to the session child **in its arguments** | the files under the user's root |

No other listener (the test tools apart). `pkgd` only connects out. On the PC: `pc/OnyxRemote`
(.NET Framework 4.8 WinForms; `Connection.cs` `Handshake ()`; `TelnetForm.cs` is a telnet console) — no
login screen. `tools/onyx-telnet.py` and the test scripts (`tools/tests/shlib/pi_apps.py`…) use telnet.

### 1.6 Cryptography

- mbedTLS 3.6.3 (`third_party/`), linked **statically into apps** (not in the kernel, not a shared
  library). Enabled: PBKDF2 (`PKCS5_C`), SHA-256 / 512, HMAC, HKDF, **the TLS server** (`SSL_SRV_C`,
  TLS 1.2 / 1.3), ECDH / ECDSA / Curve25519, certificate writing (`X509_CRT_WRITE_C`). Our glue
  (`user/tls/onyx_tls.hpp`) is client-only. No scrypt / argon2 / bcrypt.
- **`kapi_random` is not cryptographic** (a timer-seeded splitmix64; the hardware RNG200 freezes the
  bus — comment at `kernel/sys/kapi.cpp:1438`). Salts, challenges and TLS server keys need better:
  **a prerequisite** (step 0).

### 1.7 Packages

- `pkg` / `pkgman` / `pkgd` install relative to `SD:/`, **with no check of who asks**. `pkgd` (started
  at boot) installs the `auto` packages' updates by itself.
- A package's "user files" (`config =` in `tools/pkg/packages.ini`, e.g. `[onyx] config = etc/*`): on
  update, a file changed on the card is kept and the new one written as `<file>.new`.
- The Control Panel's applets are `.lnk` files in `SD:/apps/control.app/applets/` (an applet = an app
  started with `--applet`): a "Users" applet is one more.

## 2. Decisions for the user

Each has a recommendation, argued in the section named. **None is taken.**

| # | Question | Recommended | § |
|---|---|---|---|
| D1 | The rights model | **Owner / group / mode by subtree** (a rule covers a folder and all under it until a deeper rule), **plus optional per-user entries** on a rule; the index holds only the exceptions | 5 |
| D2 | What an administrator's session may do | **As a standard user; an administrative action asks for the password** (the administrator's own; for a standard user, an administrator's name and password) — rather than "an administrator's apps may do anything" | 7 |
| D3 | Where passwords are checked | **In the kernel** (`user_auth`), the hashes readable by no user process — rather than in a daemon | 3.3 |
| D4 | Per-user settings for the existing apps | **The kernel redirects** a user's writes to `SD:/etc/…` and `SD:/apps/<x>.app/…` into the user's home (reads look there first, then the system file) — no app changed; the apps move to an explicit helper later, one by one | 6.2 |
| D5 | Documents | `~/Documents`, `~/Downloads`, `~/Pictures`, `~/Music`, `~/Videos`, `~/Projects`; `SD:/docs`, `SD:/music`, `SD:/basic`… stay the **shared, read-only samples** of the packages; one shared writable folder `SD:/home/Shared` | 6 |
| D6 | Sessions | **One graphical session at a time**; logging out ends the session's processes; "Switch user" = lock + log in is postponed (it needs per-session window sets) | 4.3 |
| D7 | Who may attach the remote desktop (`rdpd`, `vncd`) | The **session's own user**; if nobody is logged in, logging in remotely opens that user's session; another user's session: **refused** (an administrator may end it, not watch it) | 8.3 |
| D8 | The password on the network | **TLS** (mbedTLS's server, a certificate made on the Pi at first boot, pinned by the client at first use) for `rdpd`, `telnetd`, `ftpd` (FTPS) and `vncd` (VeNCrypt); the accounts stored in **SCRAM-SHA-256** form so that a challenge-response stays possible where TLS is not | 8 |
| D9 | VNC | Standard VNC passwords (DES, 8 characters, needs the clear password) **cannot** use hashed accounts: either VeNCrypt (TigerVNC and a few others), or **`vncd` off by default** with Onyx Remote as the remote desktop | 8.4 |
| D10 | The migrated card | The first account is an administrator; **automatic login on** for it after a migration (the Pi behaves as before until the user turns it off); a fresh card asks in Setup | 4.4, 10 |
| D11 | Shutting down | The user at the console may restart / shut down without a password (a warning if others are connected); a remote standard user may not | 7 |
| D12 | Empty passwords | Allowed for a local login (a child's account), **refused for every remote access** | 3.2 |
| D13 | Per-user app installation | **No**: packages are the system's, installed by an administrator; `pkgd` keeps updating by itself | 9 |
| D14 | The test account | The remote test scripts (and Claude's tests on the Pi, memory `pi-remote-access`) need an account: a dedicated administrator account whose password lives in an untracked file on the PC | 8.6 |

## 3. Identity and accounts

### 3.1 A process's identity

`CAddressSpace` gains four fields — and nothing else describes "who":

| Field | Meaning |
|---|---|
| `uid` | the user; **0 = `system`** (the kernel's tasks, `init`, the daemons) — not an account one logs into |
| `groups` | a 32-bit set (group ids 0–31; bit `admin`, bit `users`, …; a user's own group is implied by the uid) |
| `session` | 0 = no session (system); else the session the process belongs to (§4.3) |
| `flags` | `ELEVATED` (started through the elevation dialog, §7), `CONSOLE` (may open windows) |

- **Inheritance**: every one of the five start paths copies the **caller's** identity to the child
  (`launch` and `exec` too, although they reset the environment and the parent). A kernel task's
  children are `system`.
- **Changing it**: only a `system` process may start a child under another identity
  (`spawn_as`, §12) — that is how the login screen, `telnetd`, `ftpd` open a user's session. An
  identity never changes in a living process, and never goes back up: no `setuid` bit, no `setuid ()`.
- **Legacy mode**: with no `SD:/etc/passwd`, everything is `system` and Onyx behaves exactly as today.
  This is what makes each step shippable (an updated card keeps working until accounts are created),
  and it is safe: once rights are enforced, only an administrator can remove that file.

### 3.2 The account base

Three text files, never in a package, never committed (as `wpa_supplicant.conf`: `.gitignore`, the
pre-commit hook, `tools/pkg` excludes them):

```
SD:/etc/passwd     name:uid:groups:full name:home:session      readable by all
    stephane:1000:admin,users:Stéphane:SD:/home/stephane:desktop
SD:/etc/group      name:gid                                     readable by all
    admin:1   users:2   remote:3   print:4
SD:/etc/shadow     name:scheme$iterations$salt$stored$server    system only
    stephane:scram-sha-256$60000$<16 bytes b64>$<32 b64>$<32 b64>
```

- Names: ASCII lower case, digits, `-`, `_`, 1–16 characters, a letter first (a folder name on FAT,
  a login typed on any keyboard). Uids from 1000; 1–999 kept for service accounts.
- **Passwords**: PBKDF2-HMAC-SHA-256 with a 16-byte random salt, the iteration count stored per
  account and set so that a check costs ≈ 250 ms on the Pi 4 (to measure in step 0: no crypto
  extensions on the BCM2711; expect 50 000–100 000). What is stored is the **SCRAM-SHA-256** pair
  (`StoredKey`, `ServerKey`) derived from that — never the password, and not even a hash that would let
  someone who reads the file log in over SCRAM. A local check recomputes it from the typed password.
- Groups that matter: `admin` (may elevate), `users`, `remote` (may log in from the network — a user
  is local-only without it), `print` (reserved for the printing work).
- An empty password: the `shadow` field is `none` — local login only (D12).
- **Rate limit** in the kernel: after 3 failures for a name, each further try waits (1 s, 2 s, 4 s… to
  30 s), per name and per remote address; logged in `kmsg`.

### 3.3 Where the check is made (D3)

**Recommended: in the kernel.** `user_auth (name, password)` answers yes / no (and the uid); the kernel
reads `shadow`, which the rights (§5) make unreadable to every user process — `system` daemons
included once they need not read it. Why not a daemon: a named IPC service can be taken by whoever
registers first; the lock screen, the elevation dialog, `su`, the four network services and the login
screen would each need the daemon alive; and the kernel must know the accounts anyway (the home for
`~`, the uid for the rights). Cost: SHA-256 + HMAC + PBKDF2 in the kernel (≈ 300 lines, ours, MIT — or
mbedTLS's `sha256.c`, Apache-2.0, compatible with the kernel's GPL-3.0). The loop **yields** every few
thousand iterations: the kernel is cooperative, a 250 ms hash must not freeze the desktop.

For SCRAM over the network (§8), a `system` service asks the kernel for the two SCRAM steps
(`user_scram`, §12) rather than reading `shadow` itself.

### 3.4 The first account

Setup (the first-boot wizard) gains a page: **your name, a password (twice), "log in automatically"**.
That account is in `admin`. Setup runs as `system`, before any session. Automatic login is one line of
`SD:/etc/login.ini` (`autologin = <name>`), changed in the Users applet.

## 4. Boot, login, sessions

### 4.1 `init` becomes the supervisor

`init` no longer exits. It runs the **system** list, then keeps the login screen alive:

```
SD:/etc/autostart        the system's: wait pkg commit, pkgd, printd, keyb <default>, telnetd, rdpd, …,
                         preload /boot — the same format as today, minus the desktop's lines
SD:/etc/session          the default session: run voronoy, run menubar, run notifyd, run clipd,
                         run dock, run agenda — a user's own list in ~/.config/session replaces it
```

Then, forever: start `login` (the greeter) and wait for it; if it dies, start it again.

### 4.2 The greeter: `login`

A new full-screen app made from `lock` (same look: the clock, a field), running as `system`:

- the list of accounts (name, picture `~/.face` if readable — else initials), the password field, the
  keyboard layout shown (the system's default layout applies until the session sets the user's),
  Restart / Shut Down;
- `user_auth`; on success `session_open (uid)` → the session number, then `spawn_as` of
  `/bin/session` under the user's identity, with `HOME`, `USER`, `LOGNAME`, cwd = the home;
- `/bin/session` (a small tool) creates the home's skeleton if missing (§6.1), runs the session list,
  and waits for a "log out" message (its IPC service `session`: the menu bar's Onyx ▸ Log Out…, the
  dock's power drawer and the `shutdown` dialog — which gains **Log Out** — send it);
- at log out: `session_close` — the kernel asks every process of the session to exit (`RequestExit`:
  the apps offer to save), kills what is left after a delay (10 s), closes their windows; the greeter
  returns.
- **The lock screen is the greeter** in "locked" mode (the session stays, its windows hidden behind the
  full-screen greeter; the password is the user's; `lock.ini`'s clear PIN goes away). A `system`
  process: the session's processes cannot kill it nor inject keys into it.
- The system theme (`SD:/etc/theme.txt`, read by the kernel at boot) dresses the greeter; the session
  start makes the kernel re-read the user's (`session_open` does it — no kapi exists for that today).

### 4.3 One graphical session (D6)

The window manager is in the kernel and knows one screen and one set of windows. **One graphical
session at a time** is the natural first stage. A process may create a window only if it belongs to
the console session or is `system` (flag `CONSOLE`): a telnet user cannot put a window on someone
else's desktop. Text sessions (telnet, FTP) of other users run beside it.

Fast user switching (two desktops alive, one shown) needs the window manager to keep a set of windows
per session and hide the others' — close to the workspaces it already has, and to what the user-space
window server (`docs/GUI-USERSPACE-STUDY.md`) will reshape. **Postponed**; nothing here closes the
door (windows already know their owner pid → their session).

### 4.4 Automatic login

`autologin = <name>`: the greeter opens that session without asking, once per boot (after a log out
it shows the list). The default after a migration (D10): the Pi starts on the desktop as before.

## 5. Rights on FAT: the index (D1)

### 5.1 The model

FAT stores no owner. Rather than one entry per file, **a rule covers a subtree**: a folder (or a file)
and everything under it, until a deeper rule says otherwise. A file's rights are those of the nearest
rule above it — so a file created in `SD:/home/alice` is Alice's with nothing to write anywhere, and
the index holds **only the exceptions** (a few dozen lines on a normal card).

```
# SD:/etc/perms       path                 owner     group   mode   [extra entries]
SD:/                  system    admin   rwxr-xr-x
SD:/home              system    admin   rwxr-xr-x
SD:/home/alice        alice     -       rwx------
SD:/home/alice/Public alice     users   rwxr-x---
SD:/home/Shared       system    users   rwxrwx---
SD:/home/bob/Projects/site  bob  -      rwx------   u:alice:rw-  g:print:r--
SD:/etc/shadow        system    -       ---------
SD:/etc/wpa_supplicant.conf  system  -  ---------
SD:/var/spool         system    print   rwx------
```

- `r` = read a file, list a folder, `stat`; `w` = create, modify, delete, rename inside; `x` = start
  a program / load a library. Three classes (owner, group, others) + the optional **per-user and
  per-group entries** — the "particular rights given to users" the user asked for.
- `system` passes every check. An **elevated** administrator process (§7) passes every check except
  reading `shadow`.
- **Built-in defaults**, compiled into the kernel, apply when the index has no line for them — and
  cannot be weakened by the file: `/etc/shadow`, `/etc/passwd`, `/etc/group`, `/etc/perms`,
  `/etc/login.ini`, `/etc/autostart`, `/etc/session` writable by `system` only (changed through the
  kapi, §12); `SD:/` and all the system read-only for users; each `home` of `/etc/passwd` private to
  its user (mode `rwx------`) unless a line says otherwise. A missing or damaged index therefore fails
  **closed**; a line that does not parse is ignored and logged.
- Other volumes: `SD1:`…`SD3:`, `USB:`… default to `system users rwxrwx---` (removable and data
  volumes are shared; rules may be added). `RAM:`: `RAM:/tmp/<uid>` private to each user (`TMPDIR`
  set per session), `RAM:/clip` to the session.
- Providers (`FTP:`…): the rights are the remote server's; `vfs_register` becomes `system`-only so
  that a user cannot answer for a prefix other users read.

Why not pure per-user lists (ACLs only): every rule would name every user; "private to its owner" and
"read-only for everyone but administrators" — the two rules that cover 95 % of the card — are exactly
what owner / group / mode says in one line. The extra entries give the particular cases.

### 5.2 In the kernel

- At boot (and when the kapi changes it) the index is parsed into a sorted table in memory; a lookup
  is the **longest matching prefix** on the resolved path (already normalised and upper-cased by
  `ResolvePath`): a few string comparisons — microseconds, against milliseconds for the card. No card
  access is added to an `open`.
- **Where**: one function `VfsAccess (resolved path, wanted, caller)` called right after `ResolvePath`
  in every entry of §1.3 — `open` / `file_open` (by the open mode), `save_file`, `file_in` / `file_out`,
  `opendir` / `dir_read`, `path_stat`, `mkdir`, `remove` / `path_unlink`, `rename` (write on both
  sides), `path_utime`, `truncate`, `chdir`, `spawn*` / `exec*` / `launch` / `image_preload` /
  `lib_open` (`x`). Reads and writes on an open handle are not re-checked. The **ways around
  `ResolvePath` listed in §1.3 are closed first** (they all get the resolved path).
- **Names that are the same file on FAT** must match the same rule — the real risk of the scheme:
  - upper / lower case: already folded;
  - **8.3 short names** (`ALICE~1` for a long name, on FAT32; exFAT has none): a path component
    containing `~` is looked up and replaced by its long name before the check (only such paths pay);
  - trailing dots and spaces, which FatFs drops (`alice.` = `alice`): removed by `ResolvePath`;
  - `SD0:` = `SD:`: already folded.
  A host test (`tools/tests/run_fs_test.sh` has FatFs on the PC) throws aliases at the check.
- **Rename / delete**: when a renamed or deleted path has a rule (or rules under it), the kernel
  rewrites the index (the rules follow a rename inside a volume; they go with a delete). A file moved
  to another subtree takes the rights of where it lands, as a copy does.
- The index is written by the kernel only: `perm_set` (the owner of the path, or an elevated
  administrator), atomically (`perms.new`, then rename).

### 5.3 The limit, in black and white

**These rights protect only while Onyx runs.** FAT / exFAT hold no owner and nothing is encrypted: the
card read in another computer shows every file of every user, the index and the (hashed) passwords;
whoever holds the card can also edit `perms`, `passwd`, `cmdline.txt` or the kernel. The model
protects users from each other and the system from mistakes and from the network — not from someone
with the card in hand. **Encrypting a home** (a key derived from the user's password; a container
volume or per-file encryption in the VFS) is the answer to that and is **not part of this plan**; it
fits with HANDOFF's "key vault" idea (Wi-Fi, mail, API keys).

Other limits to write in docs/02 when built: the GPU's user shaders (`gpu_program`) can still reach
physical memory (already noted in the EL0 document) — to restrict or validate; no disk quota; a
cooperative kernel cannot stop a user's program from hogging the CPU; `kmsg` and the crash reports in
`SD:/etc` may show other users' file names.

## 6. `/home` and the settings

### 6.1 The home

`SD:/home/<name>/` — on the boot volume by default; the `home` field of `passwd` may point to another
volume (`SD1:/home/alice`: a big exFAT partition) with nothing else to change.

```
Documents/  Downloads/  Pictures/  Music/  Videos/  Projects/      (made at the first login)
.config/                the user's settings (and the redirected files, §6.2)
.Trash/                 the user's Trash (user/trash.h: one per home; one per volume for other volumes)
```

- **`~` is understood by the kernel**: `ResolvePath` turns a leading `~/` into the caller's home. One
  place, and every app, dialog, script and command line gets it. `HOME` is set by the session.
- `user_dir (kind)` gives the home, the settings folder, Documents… (the names are data, not literals
  in 28 apps).
- The file dialogs (`uikit::FileDialog`, in `uikit.so`: **no app rebuilt**) start in the home when the app
  gives no folder or `SD:/`, gain a short places list (Home, Documents, Downloads, the volumes), and
  hide what the user may not read. The File Viewer starts in the home; its sidebar gains Home and the
  standard folders.
- The apps' literal start folders (`"SD:/docs"`… ≈ 30 call sites) become `~/Documents`… — a
  mechanical edit, app by app, at each app's next rebuild; until then the dialog opens on the shared
  samples and the user navigates.
- libc: `getuid`, `getpwuid`, `getpwnam`, `HOME` answer truly (Jet, the POSIX ports).

### 6.2 Per-user settings without touching the apps (D4)

The finding: no library to hook — 40-odd places open `SD:/etc/<x>` or `SD:/apps/<x>.app/<file>` by
name, and old binaries stay on users' cards. **Recommended: the kernel redirects.**

- A process of a user (not `system`, not elevated) that **opens for writing** (or creates, deletes,
  renames) a path under `SD:/etc/` or `SD:/apps/*.app/` that it may not write is given, instead of a
  refusal, the same path under its home: `~/.config/etc/<x>`, `~/.config/apps/<x>.app/<file>`.
- A **read** of a path under those two trees looks first for the user's copy, then for the system's
  file: the system's `dock.ini`, `theme.txt`… are **the defaults**, a user's change is the user's.
- Never redirected (a short built-in list): the account files, `perms`, `autostart`, `session`,
  `system.ini`, `wpa_supplicant.conf`, `keymaps/`, `pkg/`, `printers.ini`, and an app's `main`,
  `app.txt`, icons — writing those is an administrative action (§7).
- Cost: one more `f_stat` on opens under those two trees only. Listing a folder is not merged (the few
  apps that list `SD:/etc/<their folder>` — Mail, Media — work because their whole folder is created
  in the user's copy on the first write).

The other way — a `cfg_path ()` helper in `applib.h` / uikit and each literal replaced — is cleaner in
the long run and stays the direction: new code uses `user_dir`, and an app that is touched moves its
literals. The redirection is what makes the whole desktop per-user on day one, old binaries included.

What becomes of each kind:

| Today | With accounts |
|---|---|
| `SD:/etc/theme.txt`, `wallpaper.ini`, `dock.ini`, `places.ini`, `screenshot.ini`, `sound.ini`, `ftpfs.ini`, `mail/`, `media/`, `pdf/`, `web-*`… | the user's copy (redirected), the system's as the default |
| `SD:/apps/<x>.app/config.ini`, `recovered.*`, `last.txt`… | `~/.config/apps/<x>.app/…` (redirected) |
| `SD:/mail`, `SD:/courier`, `SD:/koton`, `SD:/lisa-chat.txt`, `SD:/Documents`, `SD:/Downloads`, `SD:/pictures` | **moved into the home** — these apps are edited (`~/Mail`, `~/Documents`…); 6 apps |
| `SD:/docs`, `SD:/music`, `SD:/basic`, `SD:/projects`, `SD:/roms`, `SD:/doom`, `SD:/manuals` | shared, read-only for users: the packages' samples and the common library |
| `SD:/.Trash` | `~/.Trash` (`trash.h`) |
| the clipboard (`clipd`, `RAM:/clip`) | a session's: `clipd` starts with the session and ends with it |
| the keyboard layout (`keyb FR` in autostart) | the system's default for the greeter; `keyb` in the user's session list |

## 7. Administrative actions: elevation (D2)

- **Recommended**: everybody's session, an administrator's included, runs with a standard user's
  rights. A program that an administrator runs does not get to rewrite the system silently.
- An administrative action is done by **a process started elevated**: `elevate <program> [args]`
  asks the `system` service of the same name (a `system` daemon, its IPC name **reserved** — only
  `system` may register the names of a built-in list) to start the program. The service shows **its
  own** dialog — "*Package Manager* wants to install *paint*" + a password field: the user's own if in
  `admin`, else an administrator's name and password —, checks with `user_auth`, and starts the program
  with the flag `ELEVATED` under the administrator's identity. The dialog is a `system` window: the
  kernel refuses `win_read` / `inject_*` on it from non-system processes, and draws its frame in a way
  an app cannot imitate (the screen dimmed by the compositor).
- In a text session: `su <name>` (asks that user's password, `spawn_as` through the kernel's check)
  and `sudo <command>` (the `elevate` service with a prompt on the terminal).
- What needs it:

  | Action | Today | With accounts |
  |---|---|---|
  | install / remove / update a package (`pkg`, Package Manager) | anyone | elevated (browsing the list is free); `pkgd` is `system` |
  | Wi-Fi networks and passwords (`wpaconf`, `wifimenu`), hostname, time zone, NTP, display mode (`cmdline.txt`), preload list, default keyboard | anyone | elevated; *choosing among known networks* stays free |
  | printers (`SD:/etc/printers.ini`) | — | elevated; printing and one's own jobs are free |
  | accounts (Users applet) | — | elevated; one's own password and picture are free |
  | rights on someone else's files | — | elevated |
  | kill a process | anyone, any | one's own; elevated for the others |
  | restart / shut down | anyone | the console user: free (D11); remote: elevated |
  | `inject_*`, `screen_grab`, `win_read`, `win_close`, `win_raise` | anyone | on the caller's own session's windows (Screenshot keeps working); `system` for all (`rdpd`, `vncd`) |
  | `tcp_listen` / `sock_listen` on ports below 1024, `vfs_register`, `set_keymap` outside a console session | anyone | `system` / elevated |

- The gate is **in the kernel**, on the calls themselves (a table: slot → rule), not in the apps.

## 8. Remote access

### 8.1 Principles

- Every listener authenticates **an Onyx account** in the group `remote`, through the kernel
  (`user_auth` inside TLS, or `user_scram`), with the rate limit of §3.2. What the peer then runs has
  **that user's identity**.
- The listeners are `system` daemons started by `init`; they keep the right to open a session.
- **The password never crosses the network in clear** (D8).

### 8.2 The channel: TLS, recommended

mbedTLS's server side is already compiled. One small shared piece (`user/tls/onyx_tls_server.hpp`,
then an `mbedtls.so` — it is on the shared-library list anyway):

- **The Pi's identity**: an ECDSA P-256 key and a self-signed certificate made at first boot
  (`SD:/etc/tls/host.key` — `system` only —, `host.crt`), named after the hostname. No authority: the
  client **pins it at first use** (shows the fingerprint once, like SSH's `known_hosts`, warns if it
  changes). The greeter and `uname` show the fingerprint.
- Needs the random-number work of step 0.
- **SCRAM-SHA-256** (the form the accounts are stored in) is the fallback where TLS is not available:
  the password is not sent and the client also verifies the server — but what follows is in clear
  (keystrokes, an elevation password typed later). So: TLS first; SCRAM alone is not offered by
  default.
- An **SSH server** would replace telnet + FTP at once (and the SFTP idea, IDEAS.md P6e, is its client
  side): a larger piece (the SSH transport is not in mbedTLS, only its primitives). **Later**; TLS on
  our own two ends gives the protection now.

### 8.3 `rdpd` and Onyx Remote (D7)

- Protocol: option bit 3 of the client's hello = "TLS + login" (bits 3–7 are free); a new server
  refuses a client without it (message "update Onyx Remote"), unless `SD:/etc/remote.ini` allows the
  old protocol (off by default once shipped). After the TLS handshake, a `LOGIN` message (name,
  password) and its answer; then the stream as today, inside TLS.
- `pc/OnyxRemote` (.NET Framework 4.8): `SslStream` (TLS 1.2) around the socket in `Connection.cs`, a
  **login dialog** (name, password, "remember the name"), the fingerprint dialog at first connection,
  a `known_hosts` file in `%APPDATA%`. `TelnetForm.cs` gets the same channel and login. Cost on the
  Pi: AES-GCM in software on the picture stream (no crypto extensions) — to measure against today's
  rates; ChaCha20-Poly1305 is the faster suite on this CPU if .NET 4.8 / Windows offers it (Windows
  11: yes for TLS 1.3; otherwise AES-GCM).
- **What a remote user sees** (one graphical session, §4.3): nobody logged in → the remote login opens
  that user's session on the console (the screen shows it too); the same user is logged in → attached
  (and the lock, if locked, is lifted for that client only when the kernel says the same user
  authenticated); **another user is logged in → refused** ("in use by …"); an administrator may then
  choose to end that session, not to watch it.

### 8.4 `vncd` (D9)

RFB's standard password scheme (type 2) is a DES challenge on an 8-character password and needs the
password itself on the server: incompatible with hashed accounts. Options:

1. **VeNCrypt** (type 19, sub-type X509Plain / TLSPlain): TLS, then name + password — accounts work;
   viewers: TigerVNC, TightVNC-derived, Remmina, bVNC; not RealVNC's viewer, not `vncdotool` (used by
   our remote tests: they move to Onyx Remote's protocol or to a VeNCrypt-capable tool).
2. **`vncd` off by default**, Onyx Remote being the remote desktop; an administrator may turn it on.

Recommended: both — implement VeNCrypt, ship `vncd` off. Same attach rule as §8.3.

### 8.5 `telnetd`, `ftpd`

- `telnetd`: TLS on connect (port 992, "telnets"; our clients are ours: `tools/onyx-telnet.py`
  — Python's `ssl` —, Onyx Remote's console), then `login:` / `Password:` (asked by `telnetd` itself,
  not echoed), `user_auth`, a text session (`session_open` with no console), and `cmd` started with
  `spawn_as` in the user's home. Port 23 in clear: **off** by default; an administrator may keep it on
  a trusted network (`remote.ini`), login still required. A first, clear-text `login:` on port 23 comes
  early (step 2) so that the identity work can be tested; the TLS port comes in step 6.
- `ftpd`: its own user table and the `user` / `password` arguments go away (the session child no longer
  gets passwords in its arguments): `USER` / `PASS` → `user_auth`, the root is the user's home (an
  administrator: `SD:/` after `SITE` elevation, or simply through the rights), the session child runs
  as the user so that **the kernel's rights apply to FTP** with no code in `ftpd`. `AUTH TLS` (explicit
  FTPS) required before `USER` unless `remote.ini` allows clear text; FileZilla / WinSCP do it; our
  `ftpfs` client already speaks FTPS.
- Setup's "remote services" page and a **Remote Access applet** list the four services (on / off,
  clear text allowed or not) instead of editing autostart lines.

### 8.6 Keeping a way back in (memories `pi-remote-access`, `pi-net-trial`)

The Pi is tested remotely; a listener that no longer lets us in is a card to pull. Rules for step 6:

- a new protocol is **added on another port first** (`telnetd` 992 beside 23, `rdpd` with both hellos)
  and the old one removed only after the new one has been used from the PC;
- the change of default goes through a **one-boot trial**, like `net-trial.txt`: `SD:/etc/remote-trial`
  (the stricter settings for this boot only; a restart puts the previous ones back);
- `cmdline.txt` `multiuser=0` = legacy mode for one who holds the card (consistent with §5.3);
- the test scripts log in with the test account (D14), read from an untracked file.

## 9. Packages, printing

- **Who installs**: `pkg` writes `SD:/apps`, `/bin`, `/lib`, `/var/pkg` — the system's: an elevated
  administrator (§7). `pkgd` (`system`, from the system autostart) keeps installing the `auto` updates
  and notifying the session (`notifyd` is the session's: no session, no bubble — the notice waits).
  `wait pkg commit` at boot is `system`. No per-user installation (D13).
- **A package's user files** (`config =`): they are now **the system's defaults**. A user's change
  lives in the home (§6.2) and no longer meets the update; the `<file>.new` mechanism remains for the
  defaults an administrator changed. **No package ever writes into `/home`.** Samples stay in the
  shared folders. The home's skeleton is made by `/bin/session`, not shipped.
- `tools/pkg`: `etc/passwd`, `group`, `shadow`, `perms`, `login.ini`, `tls/`, `home/` are **never
  packaged** (`config = etc/*` of `[onyx]` must exclude them — as it does `wpa_supplicant.conf`), nor
  staged in `sdcard/` or `sdcard_lite/`: a fresh card has no account until Setup runs. New files
  shipped: `etc/session`, the trimmed `etc/autostart`, `bin/session`, `bin/su`…, the `login` app.
- **Printing** (another session's work: `printd`, the Printers applet, `SD:/etc/printers.ini`,
  `SD:/var/spool` — not touched here). What this plan asks of it, when both exist: `printd` is a
  `system` daemon of the system autostart; printers are the system's (adding / removing: elevated);
  **a job belongs to the user who sent it** — `printd` takes the sender's pid from the kernel
  (`mailbox_recv`) and its uid with `proc_ident`, records it with the job, lets a user list and cancel
  their own jobs and an administrator all; `SD:/var/spool` is `system`-only (the apps hand the document
  through the service, they do not write there). Until then, `printd` running in legacy mode needs no
  change.

## 10. Migrating a card

A card updated by `pkgd` gets the new kernel and `init` but has no `/etc/passwd`: **legacy mode** —
nothing changes for the user. The migration is a deliberate act: Control Panel ▸ Users ▸ "Set up user
accounts" (or `useradd` on telnet), which runs once, as `system`:

1. creates the first account (administrator), `passwd` / `group` / `shadow`, `login.ini` with
   **automatic login on** (D10);
2. creates its home and **moves** into it what was the single user's: `SD:/Documents`, `SD:/Downloads`,
   `SD:/pictures`, `SD:/mail`, `SD:/courier`, `SD:/koton` (user data), `SD:/lisa-chat.txt`,
   `SD:/.Trash`, the files of `SD:/docs`, `SD:/music`, `SD:/projects`, `SD:/basic` **that are not a
   package's** (the package database `SD:/var/pkg/db` knows every shipped file and its hash: what is
   not listed, or was modified, is the user's → `~/Documents`…); the settings of §6.2's first row and
   the bundles' state files → `~/.config/…` (the system's copy of a package's default is put back from
   its recorded hash, or left as the default);
3. splits `SD:/etc/autostart` into the system list and `SD:/etc/session` (the user's own added lines
   go to `~/.config/session`);
4. writes a log (`SD:/var/migrate.log`) of every move — and moves are renames inside a volume: fast,
   and reversible by the same log (`migrate --undo`, as long as no second account exists).

Everything is a rename on the same volume: no copy, no space needed. A dry run (`migrate --list`)
prints the plan first. `sdcard_lite` and a fresh card: no account, Setup's new page (§3.4).

## 11. The steps

Each ends on the Pi, with the docs (02: the ABI table and history; 03; 04) and the packages published
in the same session (CLAUDE.md). Kapi version numbers are indicative (v84 onward, slots from 262).

### Step 0 — prerequisites (no visible change)

- **Random numbers**: `kapi_random` backed by a real generator (a ChaCha20 or HMAC-DRBG in the kernel,
  seeded and reseeded from interrupt timings, the SD and Wi-Fi traffic, the cycle counter; the RNG200
  looked at again with its bus freeze). No ABI change.
- SHA-256 / HMAC / PBKDF2 in the kernel, with yields; **measure** the iteration count for 250 ms.
- *Files*: `kernel/sys/kapi.cpp`, a new `kernel/sys/crypto.cpp`; host test with the RFC vectors.
- *Test on the Pi*: a `/bin` test prints the PBKDF2 time and checks the vectors; `kmsg` shows the
  entropy sources; the desktop stays fluid during a hash.

### Step 1 — identity and accounts

- *Kernel*: the four fields, inheritance in the five start paths, the parsers of `passwd` / `group` /
  `shadow`, `user_auth` with its rate limit, `spawn_as`, `proc_ident`, `user_info`. **No enforcement.**
- *kapi* (v84): `proc_ident`, `user_info`, `user_auth`, `user_passwd`, `user_edit`, `spawn_as`.
- *User*: `/bin/id`, `whoami`, `su`, `passwd`, `useradd` / `userdel`; `ps` and the Task Manager show
  the user; libc's `getuid` / `getpw*`.
- *Risk*: a start path forgotten → a child with the wrong identity (a test walks all five).
- *Test*: on telnet (still open, `system`): `useradd`, `su alice`, `id`, `ps`; a child started by each
  of the five calls keeps `alice`; three wrong passwords → the delay; no `/etc/passwd` → all as today.

### Step 2 — login, session, log out

- `init` the supervisor; `autostart` split, `etc/session`; the `login` app (greeter + lock); 
  `/bin/session`; `session_open` / `session_close`; Log Out in the menu bar, the dock and `shutdown`;
  Setup's account page; `login.ini` (automatic login); `telnetd`'s `login:` prompt (clear text, the
  port and the rest unchanged).
- *kapi* (v85): `session_open`, `session_close`, `session_info`; the `CONSOLE` rule on window creation.
- *Files*: `user/BinUtils/init.c`, `user/Apps/login` (from `lock`), `user/BinUtils/session.c`,
  `user/Apps/{menubar,dock,shutdown,setup}`, `user/BinUtils/telnetd.c`, `sdcard/etc/{autostart,session}`.
- *Risk*: a console with no desktop (the greeter crashes, a bad session list) — `init` restarts the
  greeter, telnet stays (it is the system's), `multiuser=0` on the card.
- *Test*: boot → the greeter; a wrong password; log in → the desktop; log out with a document open
  (asked to save), a hung app (killed after the delay) — nothing of the session is left in `ps`; log
  in as another user; lock / unlock; automatic login; a telnet user cannot open a window.
  Screenshots: `login`, the new Setup page.

### Step 3 — the home and per-user settings

- `~` in `ResolvePath`, `user_dir`, `HOME` / `TMPDIR` per session, the skeleton; the redirection of
  §6.2; `uikit::FileDialog` and the File Viewer (Home, places); `trash.h`; `clipd` in the session; the
  six apps whose data folder moves (Mail, Courier, Koton, Lisa, PDF, Photos, Jet's Downloads); the
  migration tool (§10).
- *kapi* (v86): `user_dir`.
- *Risk*: the redirection's corner cases (an app that renames a temporary file over its settings; a
  folder listed) — a host test over the real apps' open / write patterns (the desktop simulator runs
  the real apps on the PC); a migration that loses files — renames only, the log, the dry run.
- *Test*: two users each change the wallpaper, the dock, a Mail account: each finds their own at the
  next login, the system's files are unchanged; `~/Documents` in every dialog; the Trash per user; the
  clipboard empty at the next login; the migration on a copy of the user's card, then `--undo`.

### Step 4 — elevation and the Users applet

- The `elevate` service and its dialog, reserved IPC names, `sudo`; the **Users** applet
  (`user/Apps/userconf`, `45-userconf.lnk`: accounts, password, picture, administrator, remote access,
  automatic login); Package Manager, `wpaconf`, `wifimenu`, `displayconf`, `preloadconf`, `keyconf`
  ask for elevation where §7 says.
- *kapi* (v87): the protected-window flag; `ipc_register`'s reserved names (no new slot).
- *Test*: a standard user installs a package → the dialog → an administrator's password → done; a
  wrong password; the dialog cannot be read (`screenshot`) nor typed into (`inject_key`) by a user's
  process. Screenshots: the dialog, the applet.

### Step 5 — the rights enforced

- The index, `VfsAccess` at every entry, the alias handling (§5.2), the ways around `ResolvePath`
  closed, rename / delete keeping the index; the privileged-call table of §7; `perm_get` / `perm_set` /
  `path_access`; `/bin/perms`, the File Viewer's Properties ▸ Sharing; the dialogs and listings hide
  what cannot be read.
- *kapi* (v88): `path_access`, `perm_get`, `perm_set`.
- **Turned on by a one-boot trial first** (`SD:/etc/perms-trial`: enforce for this boot, log every
  refusal in `kmsg` — a "would refuse" mode before that, to find the apps that write where they should
  not), then by default.
- *Risk*: the largest step — an app broken by a refusal (the audit mode finds them); an alias that
  slips through (the host test); a slower `open` (measure with `fsbench`).
- *Test*: Alice cannot list `~bob`, read `shadow` or `wpa_supplicant.conf`, write in `SD:/apps`, kill
  Bob's telnet shell, grab the screen from telnet; `HOMEBO~1`-style names and `bob.` refused alike; a
  per-user entry opens one folder to one user; rename of a folder with a rule keeps it; `fsbench`
  before / after; every app started once (`tools/tests/shlib/pi_apps.py`) with no refusal logged.

### Step 6 — remote access

- The host key and certificate, the TLS server piece; `rdpd` + Onyx Remote (login, TLS, fingerprint);
  `telnetd` on 992 + `tools/onyx-telnet.py` + the test scripts; `ftpd` on accounts + `AUTH TLS`;
  `vncd` VeNCrypt, off by default; `remote.ini`, the Remote Access applet; the attach rules (§8.3).
- *kapi*: `user_scram` (v89) if SCRAM is kept for a client without TLS.
- *Files*: `user/BinUtils/{rdpd,telnetd,ftpd,vncd}.c`, `user/tls/`, `pc/OnyxRemote/{Connection,MainForm,
  TelnetForm}.cs` (+ `pc/dist`), `tools/onyx-telnet.py`, `tools/tests/**` that telnet.
- *Risk*: **losing the remote hand** — the rules of §8.6 (new beside old, one-boot trial); TLS's cost
  on the remote desktop's rate (measure; HANDOFF's network figures are the reference).
- *Test*: a network capture shows no password and no screen in clear; a wrong password ×3 → the delay;
  a user without `remote` refused; Onyx Remote: nobody logged in / same user / another user; a changed
  host key → the client's warning; FileZilla in FTPS sees only the home; the rates before / after.

### Step 7 — shipping

- The packages (`packages.ini` exclusions, the new files), `sdcard_lite`, Setup on a fresh card, the
  "Set up user accounts" path on an updated card; docs 01–04 (a "Users and sessions" chapter in 04, the
  security model and its limit in 02), LICENSING unchanged (our code: MIT; mbedTLS Apache-2.0 already
  listed), the screenshots, the exports.
- *Test*: a published update on a legacy card → nothing changes; the migration from the Control Panel;
  a fresh `sdcard_lite` → Setup → the first login.

## 12. Additions to the kapi (append-only; names and slots to fix at each step)

| Call | For | Who may |
|---|---|---|
| `int proc_ident (int pid, struct kapi_ident *)` — uid, groups, session, flags (pid 0 = the caller) | services (who is asking: the pid of `mailbox_recv`), `ps`, the Task Manager | all |
| `int user_info (const char *name_or_null, unsigned uid, struct kapi_user *)` — name, full name, home, groups; enumeration by index | the greeter, the applet, libc | all |
| `int user_auth (const char *name, const char *password, unsigned flags)` → uid / error | the greeter, the lock, `elevate`, `su`, the listeners | all (rate-limited) |
| `int user_scram (...)` — the two server steps of SCRAM-SHA-256 | a listener without TLS | `system` |
| `int user_passwd (const char *name, const char *old, const char *new)` | changing one's password; an elevated administrator: anyone's (`old` = 0) | owner / elevated |
| `int user_edit (int op, const struct kapi_user *)` — add, remove, change groups / home | the applet, `useradd` | elevated |
| `long long spawn_as (const struct kapi_spawn_attr *, void **handles, unsigned n, const struct kapi_cred *)` — uid, session, flags | the greeter, the listeners, `elevate`; with a password in the cred: `su` | `system`, or a valid password |
| `int session_open (unsigned uid, unsigned flags)`, `session_close (int session, unsigned grace_ms)`, `session_info (int session, struct kapi_session *)` | the greeter, `telnetd`, `/bin/session`, `rdpd` (who is at the console) | `system` (info: all) |
| `int user_dir (int kind, char *buf, unsigned cap)` — home, settings, documents, downloads… | apps, uikit | all |
| `int path_access (const char *path, int want)` | dialogs, the File Viewer | all |
| `int perm_get (const char *path, struct kapi_perm *)`, `perm_set (const char *path, const struct kapi_perm *)` | Properties, `/bin/perms` | all / owner or elevated |

No existing call changes its signature. Existing calls gain refusals (an error they could already
return: `-1`, `EACCES` in the v75 layer). `struct kapi_stat`'s `mode` reports the caller's effective
rights; `uid` / `gid` fields, if absent, come as a new `path_stat2`.

## 13. What is not in this plan

Encrypted homes and a key vault (§5.3); an SSH server / SFTP (§8.2); fast user switching (§4.3); disk
quotas; per-user app installation; auditing (a log of accesses); network users (LDAP…); per-user
volumes mounted at login.
