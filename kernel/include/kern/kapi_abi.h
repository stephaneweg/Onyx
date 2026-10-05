//
// kapi_abi.h -- the kapi ABI shared by the kernel and userland apps.
//
// Instead of linking apps directly against kernel symbol addresses (which move on
// every kernel rebuild), the kernel publishes a function-pointer TABLE at a FIXED
// virtual address, mapped read-only into every app's address space. Apps call the
// kernel through this table, so an app binary keeps working against any kernel that
// exposes the same ABI -- no rebuild needed when the kernel changes. Apps run at EL0:
// the entries they see point at EL0 stubs that make system calls (slot n: "svc #0" with
// n in x8) or at user-side code (memcpy & co., the event pump) -- kern/el0.h.
//
// THE CONTRACT IS: this struct layout + KAPI_TABLE_VA. It is APPEND-ONLY -- never
// reorder or remove fields; add new ones at the end and bump KAPI_ABI_VERSION. Old
// apps then keep working (they only touch the prefix they know).
//
#ifndef _kern_kapi_abi_h
#define _kern_kapi_abi_h

// Fixed user VA where the kernel maps the table (one 64 KB page). Stable forever.
// (Window canvas is at 12 GB, user stack at 16 GB; this sits in the gap at 14 GB.)
#define KAPI_TABLE_VA		(14ULL * 0x40000000ULL)
// AppKit (2026-10-05, SD:/lib/appkit.so, user/Kits/appkit): the ONE interface between the programs and the
// kernel. The programs call its functions (kapi_*: user/kapi.h) BY NAME, through import stubs that jump
// through AppKit's table -- which the kernel copies at APPKIT_TABLE_VA (entry n at + 8 n), in the same
// read-only page as its own, when the first program starts; AppKit's code is mapped into every
// program. A program does nothing to have it.
// THE CONTRACT BELOW (this struct's layout at KAPI_TABLE_VA) THEN BINDS APPKIT AND THE KERNEL ONLY: they
// are built and shipped together (the package onyx), so the table may be restructured -- entries moved,
// removed, merged -- by adapting AppKit; no program is rebuilt. What is append-only from now on is
// AppKit's own list of names (user/Kits/appkit/appkit.abi). A new AppKit is the next start's.
#define APPKIT_TABLE_VA		(KAPI_TABLE_VA + 0x8000ULL)	// (4096 entries at most)
#define APPKIT_TABLE_MAX	4096
// v29: COMPAT BREAK -- the kernel-drawn widget API was removed from the table and the
// table consolidated (no gaps), so old app binaries must be rebuilt. (Retro-compat was
// explicitly waived; every app is rebuilt from this tree.)
// v30: + random() -- hardware RNG (Pi RNG) for cryptographic seeding (TLS entropy).
// v33: + ram_detail() -- firmware-detected board RAM + app page-pool total/free (memmon).
// v35: + surface_create/map/size/present/destroy -- shared surfaces (activity shell);
//      + register_shell/shell_request/mailbox_send/mailbox_recv -- activity-shell IPC.
// v36: + memset/memcpy/memmove -- Circle's kernel implementations, so the calls the
//      compiler emits on its own (array/struct init and copies) link in every app.
// v37: + tcp_listen/tcp_accept -- TCP server side (telnetd remote shell).
// v38: + screen_grab/inject_pointer/inject_key -- remote screen (vncd).
// v39: + set_menu/get_menu/menu_command -- system menu bar (menubar app).
// v40: + ipc_register/ipc_lookup (named services, 512-byte messages), clipboard_set/get,
//      set_window_alpha (fades), shutdown (restart / halt).
// v41: + fullscreen_begin/present_fb/fullscreen_end -- full-screen apps.
// v42: + drag_begin/drag_data (drag & drop), get_modifiers/inject_modifiers.
// v43: + net_ping/net_resolve/net_info -- network tools (ping, nslookup, netstat).
// v44: + vfs_register/vfs_next/vfs_req_data/vfs_reply -- user-space file systems (FTP:).
// v45: + wlan_scan -- the Wi-Fi access points around.
// v46: + sound_acquire/release/start/stop/write/status -- audio (synth voices + PCM).
// v47: + sound_instrument -- 2-operator FM instruments (OPL2 style) on the voices.
// v48: + key_held / inject_key_held -- is a key held down (games: move while held).
// v49: + exec_as -- run a program under another name (a runner: SD:/bin/basic for an app's
//      main.bax is named after the app). The kernel runs ELFs only: the formats a runner
//      executes (.bas, .bax...) are chosen in user space (SD:/etc/runners.ini, launch.h).
// v50: + pad_state -- USB gamepads (Circle's drivers: Xbox 360 / One, PS3 / PS4, Switch Pro
//      and standard HID pads): the raw buttons / axes / hats of pad 0..3; the button
//      mapping is done in user space (user/Include/gamepad.h, SD:/etc/gamepad.ini).
// v51: + core_acquire/core_run/core_state/core_release -- app cores: an app acquires core 2
//      or 3 and runs one function of its own there (no kapi calls, no malloc on it).
// v52: + gpu_info/gpu_draw -- the V3D GPU (VideoCore VI): depth-tested Gouraud triangles
//      rendered by the GPU into the caller's pixels (sys/v3d.cpp).
// v53: + gpu_texture/gpu_render -- the GPU's full pipeline: textures (RGBA8, nearest /
//      linear, repeat / clamp / mirror), batches with their own 4 x 4 matrix (transformed
//      by the GPU), texture, blending (alpha / add / multiply), depth test and culling.
// v54: gpu_render: the vertex's last 4 bytes (reserved until then, 0 in the programs of v53) are
//      a second colour r2 g2 b2 a2, added to the result (colour + colour2, or texel * colour +
//      colour2, clamped to 1); KAPI_GPU_B_ALPHATEST(t): the pixels whose alpha < t / 255 are not drawn.
// v55: + fullscreen_direct -- a full-screen app draws straight into the displayed framebuffer
//      (no copy by present_fb; the GPU renders there too).
// v56: + win_list/win_read/win_raise/win_close -- the windows as objects, for the window-
//      level remote desktop (rdpd): their place, size, state and pixels.
// v57: + seek -- set the read position of an opened file (random access: the GameCube discs).
// v58: + code_alloc -- executable memory for generated code (the GameCube emulator's JIT).
// v59: + fsize64 -- a file's size over 4 GB (exFAT partitions); paths may name the SD card's
//      partitions: SD: (= SD0:) the first, SD1: .. SD3: the others.
// v60: + sound_volume -- the master volume (0..10) and mute, applied to everything played;
//      + wlan_reconnect -- join by wpa_supplicant.conf again without a reboot (the Wi-Fi menu).
// v61: + gpu_program / gpu_render2 -- the app's own QPU shaders (vertex, coordinate, fragment),
//      batches with their uniforms, up to 8 textures, blend factors, write mask, scissor.
// v62: + gpu_render3 -- gpu_render2's batches with their vertices where the app keeps them (each
//      its offset and stride), x / y framed by the kernel on the way (no common array to make).
// v63: + gpu_vbuf -- GPU-visible memory the app writes: gpu_render3 draws vertices there in place
//      (framed in place, clipped into its free end: nothing copied).
// v64: + win_minimise / win_geometry / resize_window2 -- the modernised CDE desktop's windows:
//      the frame's title buttons (the window menu, minimise, maximise, close: KAPI_FRAME_*,
//      GUI_EVENT_WINCTL), minimised windows (KAPI_WIN_MINIMISED), maximise (a window grows past
//      its first size), the work area; the frames' rounded corners (their see-through pixels:
//      the chrome's top byte); WIN_FLAG_ALPHA (per-pixel see-through windows).
// v65: + desk / win_desk -- workspaces (virtual desktops): a window opens on the current desk and
//      is shown only there (KAPI_WIN_OFFDESK, KAPI_WIN_DESK in win_list); list_windows and
//      raise_app see the current desk's windows only; Ctrl+Alt+Left / Right switch desks.
// v66: + screen_set -- the screen's resolution changed while running (the Control Panel's Display
//      applet); every window then gets GUI_EVENT_DISPLAY_RESIZE (19, (w << 16) | h) and is kept
//      on the screen; an app's window may be as big as the screen (was 1024 x 768 at most).
// v67: + thread_create / thread_exit / thread_join / thread_self -- threads: tasks of the app's
//      own process (its address space, window, heap, files), preempted like it, ended with it;
//      + mutex_* / event_* / barrier_* / sync_close -- their synchronisation objects;
//      + post / pump_wait -- a call queued to the app's event pump (a thread hands its result
//      to the UI thread), and a pump that sleeps until an event or a post arrives. The
//      scheduler has no task limit any more (a list; MAX_TASKS gone).
// v68: + sound_config / sound_map -- low-latency sound for the owner: the chunk size and the
//      chunks rendered ahead (1024 x 4 ~116 ms by default, 256 x 2 ~17 ms), and a PCM ring in
//      a mapped page (struct kapi_sound_ring) that an app core fills without a kapi call;
//      + wait_word / wake_word -- a futex: sleep while a word holds a value, across processes
//      on a shared surface, the kernel re-checking the sleeping words at every tick (a word
//      changed by an app core wakes its waiters within 10 ms); + thread_priority -- a "real
//      time" thread, picked first when ready; + midi_read / midi_devices -- USB MIDI input
//      (class-compliant keyboards, hot-plugged), timestamped events.
// v69: + screen_native -- the monitor's own resolution (its EDID's preferred timing: the first
//      detailed timing descriptor), for Setup and Display; + set_timezone -- the local time's
//      offset from UTC changed while running (the clock, kapi_get_datetime), as system.ini's
//      timezone= does at boot. Also WIN_FLAG_FIXED (kern/gui/window.h): a window the user
//      cannot move, without minimise / maximise / close buttons, kept centred when the screen's
//      resolution changes (the first-run wizard's).
// v70: + gpu_texture_rect -- a rectangle of a texture's pixels replaced (no whole re-upload: the
//      GPU compositing service, user/Libs/gpucomp); KAPI_GPU_F_ALPHA -- a GPU frame's target keeps its
//      alpha (premultiplied ARGB: loaded, blended, stored, cleared to clear's top byte). The
//      texture handles are shared by the programs using the GPU at once: 1024 in all (was 256),
//      512 at most a program; the gpu_vbuf blocks 32 in all, 8 a program (was 8 in all).
// v71: + vol_info -- a volume's size, free space, type (struct kapi_vol_info). Also the RAM:
//      volume (kern/ramfs.h): a file system in memory, until the Pi restarts, reached by the same
//      file calls as the card (open / read / fsize / seek / close, save_file, file_in / file_out,
//      opendir / readdir / closedir, mkdir / remove / rename, chdir).
// v72: gpu_render's blending: the compositing presets KAPI_GPU_BLEND_MULCOL .. DSTOUT (5..12:
//      multiply, screen, plus, subtract, lighten, mask, cut out -- premultiplied, the alpha
//      apart; user/Libs/gpucomp's layer blend modes). An older kernel takes 5..15 as ALPHA.
// v73: + pop_event / event_mods / pop_post / pump_sleep -- the event pump's kernel half, for the
//      user-side pump of a PROTECTED (EL0) process (kern/el0.h): its table's pump_events /
//      wait_for_exit / pump_wait are EL0 code that pops the window's events and the posted calls
//      (struct kapi_event, struct kapi_posted) and calls the handlers itself. An EL1 app may call
//      them too (its pump_events still runs in the kernel).
// v74: every process runs at EL0 (the EL1 "legacy" mode is gone: kern/el0.h). + proc_stats --
//      a process's system calls (struct kapi_syscall_stats: the total, the rate per second, the
//      8 table slots most called) and its ID register reads; those reads (MRS of MIDR_EL1,
//      MPIDR_EL1, REVIDR_EL1, ID_AA64*_EL1 at EL0) are now emulated by the kernel, sanitised,
//      instead of killing the app. The kernel table's pump_events / wait_for_exit / pump_wait /
//      memset / memcpy / memmove are 0: they were never system calls (user-side code).
// v75: the POSIX layer's kernel half (docs/POSIX-PLAN.md), three blocks after proc_stats:
//      + vm_map / vm_unmap / vm_protect / vm_advise / vm_query / vm_stats / thread_create_ex /
//      thread_info (slots 199..206: demand paging, mmap, TLS, stacks); + file_* / path_* /
//      dir_read / stream_write_nb / spawn_ex / proc_wait / get_argv / get_env / getpid /
//      clock_info / sleep_us (207..228: file descriptors, stat, pipes, environment, spawn/wait,
//      clock); + sock_* / poll (229..241: BSD sockets and poll). Every v75 call returns >= 0 on
//      success, -KAPI_Exxx (newlib's errno values) on failure. The skeleton: every entry exists
//      and returns -KAPI_ENOSYS until its work package lands.
// v76: IPC between processes (docs/POSIX-PLAN.md §14, WP-IPC: what WebKit2's Unix IPC needs),
//      a block after poll: + sock_pair (local sockets: SOCK_STREAM / SOCK_SEQPACKET / SOCK_DGRAM
//      pairs, numbered from KAPI_SOCK_LOCAL_BASE, served by every sock_* call and poll) /
//      sock_sendmsg / sock_recvmsg (scatter-gather, up to KAPI_IPC_HANDLES_MAX handles per message
//      moved into the receiver's table: SCM_RIGHTS) / shm_create / shm_open / shm_unlink / shm_ctl
//      (size, seals) / shm_map (shared memory objects mapped MAP_SHARED in several spaces) /
//      handle_close / spawn_ex2 (handles given to the child at chosen descriptors) / get_handles
//      (slots 242..252). + KAPI_SO_RCVBUF / SNDBUF / PEERPID / DOMAIN, KAPI_VMK_SHM.
// v77: program images (kern/image.h, docs/02 section 7): a program is loaded once -- streamed
//      from its file -- and its read-only segments are shared by its processes; the image's key
//      is the program's canonical path. + image_preload (a program loaded ahead and kept: a run
//      of its path reads nothing from the card) / image_unload (its pin and its name taken away)
//      / image_list (the live images, or the one of a path) (slots 253..255), struct
//      kapi_image_info, KAPI_IMG_*. No existing call changes.
// v78: executable memory for a JIT (roadmap step 3, docs/08): vm_map and vm_protect accept
//      KAPI_PROT_EXEC for anonymous regions (lazy pages, EL0 execute; vm_protect can take it
//      away again: W^X); shm_map still refuses it. No new call: a program asks version >= 78.
// v79: + kernel_info (slot 256): what the running kernel is, as "key value" lines (name, abi,
//      built, rev, machine, model, ram) -- /bin/uname.
// v80: + cpu_stats (slot 257): each core's role (the system's, the sound's, an app core and its
//      owner, the network's) and the microseconds it was busy; + net_stats (slot 258): the bytes a
//      process's sockets sent and received (pid 0: all of them). The Task Manager's two tabs.
// v81: + set_cursor (slot 259): the pointer's shape over the caller's window (KAPI_CURSOR_*: the
//      arrow, a hand, the text bar, the arrows that move and resize, the cell's cross, a crosshair,
//      the hourglass, "no"). The kernel shows the four arrows while a window is dragged.
// v82: + win_resizable (slot 260): the caller's window can be resized by its frame -- the pointer on
//      an edge or a corner shows the two arrows, a drag shows the new outline, and at the release the
//      window gets GUI_EVENT_WINRESIZE (20) with its new place and client size, which it applies
//      (resize_window2, move_window). uikit: Root::setResizable.
// v83: shared libraries (docs/SHARED-LIBS-PLAN.md, docs/02 section 7): + lib_open (slot 261): a
//      position-independent library (SD:/lib/<name>.so, user/Runtime/lib.ld's shape) loaded once for the
//      whole system, placed by the kernel in the library arena (16 GB..32 GB), its data relocated
//      once, mapped into the caller -> its export table (version, size, init, then its entries:
//      append-only, as this table). + KAPI_IMG_LIB in kapi_image_info.flags. No existing call changes.
// v84: the sound's output (sys/sound.cpp, COnyxSoundDevice): + sound_output (slot 262): which output
//      plays -- the jack (PWM), a USB audio device, HDMI, or auto (USB if there is one, else the jack,
//      else HDMI on a board without a jack) --, what runs and which ones are there (KAPI_SND_OUT_*).
//      The producer, the streams and every other sound call are unchanged: the output adapts (the
//      rate 44.1 -> 48 kHz, the sample format, the volume by the device's own control when it has one).
// v85: the sound is a MIXER: every program that plays has a channel of its own (sound_acquire gives
//      one, up to 8: two programs are heard together) with its volume and its mute, remembered by
//      the program's name (SD:/etc/mixer.ini at the start). + sound_clients (slot 263): the channels
//      (struct kapi_sound_client: pid, name, volume, mute, its level now); + sound_client_volume
//      (slot 264). sound_status's free frames are the caller's own channel's, its owner the caller
//      (0: no channel). The mapped ring stays one (the first program that maps it).
// v86: AppKit (above, KAPI_TABLE_VA): the kernel loads SD:/lib/appkit.so at the first program's start,
//      copies its table at APPKIT_TABLE_VA and maps its code into every program. No entry added: the
//      number says "this kernel gives the programs AppKit" -- a program built for AppKit needs it (its
//      package's "kapi >= 86": the package manager installs it once this kernel runs).
// v87: AppKit carries its small services too (user/Kits/appkit/appkit_lib.inc: the strings, the console,
//      the .ini reader, the keyboard layout -- they were user/applib.h, inline in every program). No
//      entry added to this table: the number says "this system's AppKit has them" -- a program built
//      from now on calls them in AppKit, and its package's "kapi >= 87" waits for this system.
#define KAPI_ABI_VERSION	87

#define KAPI_WAIT_FOREVER	0xFFFFFFFFu	// (v67) a wait's timeout: none

#ifdef __cplusplus
extern "C" {
#endif

// Widget / key event callback: void (sender, GUI_EVENT_*, value). The value is 64 bits (a pointer
// event packs its wheel, buttons and x, y): `long` on Onyx, `long long` where long has 32 bits (the
// Windows build of the apps, pc/Koton) -- the same type on Onyx, the same ABI.
#if defined(_WIN32)
typedef long long gui_value;
#else
typedef long gui_value;
#endif
typedef void (*gui_handler) (unsigned long sender, int event, gui_value value);

// Sound (ABI v46): waveforms of kapi_sound_start, and the output format.
#define SOUND_SQUARE	0
#define SOUND_SINE	1
#define SOUND_TRIANGLE	2
#define SOUND_SAW	3
#define SOUND_NOISE	4
#define SOUND_FM	5		// the voice's FM instrument (kapi_sound_instrument, v47)

// A 2-operator FM instrument (ABI v47), OPL2 style: op[0] = modulator, op[1] = carrier.
// mult 0..15 (x0.5, 1, 2 .. 15), level 0..63 (attenuation, 0.75 dB steps; 0 = loudest),
// attack / decay / release 0..15 (0 = never, 15 = fastest), sustain 0..15 (the level
// the decay stops at, 3 dB steps), wave 0 sine, 1 half sine, 2 absolute sine, 3 quarter
// pulses. feedback 0..7 (the modulator modulates itself), connection 0 = FM (the
// modulator bends the carrier), 1 = additive (both are heard).
#define FM_SUSTAINED	1		// hold at the sustain level while the key is down
#define FM_TREMOLO	2		// amplitude vibrato (1 dB, 3.7 Hz)
#define FM_VIBRATO	4		// pitch vibrato (7 cents, 6.1 Hz)
#define FM_KSR		8		// key scaling of the rates (accepted, not used yet)
struct kapi_fm_op { unsigned char mult, level, ksl, attack, decay, sustain, release, wave, flags; };
struct kapi_fm_instrument { struct kapi_fm_op op[2]; unsigned char feedback, connection; };
#define SOUND_VOICES	16		// voices 0..15
#define SOUND_RATE	44100		// PCM frames per second (s16 left, s16 right)

// The mapped PCM ring (ABI v68, kapi_sound_map): one 64 KB page shared by the sound owner
// and the kernel's producer (core 1), mixed with the voices and the sound_write stream. The
// app writes frames at data[(wr + i) % frames], then (a store barrier: "dmb ish") moves wr;
// the kernel moves rd once it has taken them. Both are free-running frame counters (they
// wrap at 2^32): wr - rd = the frames waiting (at most `frames`). Usable from an app core
// (kapi_core_run code: plain memory, no kapi call); kapi_sound_ring_write (user/kapi.h) does it.
#define KAPI_SOUND_RING_MAGIC	0x474E5253u	// "SRNG"
#define KAPI_SOUND_RING_FRAMES	8192		// its capacity (a power of two): ~186 ms
struct kapi_sound_ring
{
	unsigned	  magic;		// KAPI_SOUND_RING_MAGIC
	unsigned	  frames;		// KAPI_SOUND_RING_FRAMES
	volatile unsigned wr;			// the app: frames written so far
	volatile unsigned rd;			// the kernel: frames taken so far
	volatile unsigned dry;			// the kernel: times it ran dry while playing (underruns)
	unsigned	  rate;			// SOUND_RATE
	volatile unsigned chunk;		// the output's chunk now, frames (kapi_sound_config)
	volatile unsigned ahead;		// ... and the chunks rendered ahead
	unsigned	  reserved[8];
	short		  data[KAPI_SOUND_RING_FRAMES * 2];	// s16 left, right (at offset 64)
};

// One Wi-Fi access point seen by kapi_wlan_scan (ABI v45).
#define WLAN_SEC_OPEN	0
#define WLAN_SEC_WEP	1
#define WLAN_SEC_WPA	2
#define WLAN_SEC_WPA2	3		// RSN (WPA2 / WPA3)
struct kapi_wlan_ap
{
	char	      ssid[33];		// 0-terminated, "" = hidden network
	unsigned char bssid[6];
	unsigned char security;		// WLAN_SEC_*
	unsigned char channel;		// 1..14 (2.4 GHz), 36.. (5 GHz)
	unsigned char connected;	// 1 = the network we are associated with
	int	      freq;		// MHz
	int	      level;		// signal, dBm (-40 strong .. -90 weak)
};

// A request to a user-space file-system provider (kapi_vfs_next, ABI v44).
struct kapi_vfs_req
{
	unsigned id;		// answer it with vfs_reply (id)
	int      op;		// VFS_OP_* (kern/vfs.h, user/vfs.h)
	char     path[300];	// the full path, prefix included ("FTP:host/dir/file")
	char     path2[300];	// RENAME: the new path
	long     a0, a1, a2;	// READ: fid, offset, length; CLOSE: fid
	unsigned in_len;	// SAVE: payload size (fetch it with vfs_req_data)
};

// A volume's room (ABI v71, kapi_vol_info): "SD:", "SD1:"... (FatFs: total and free from its
// FAT, f_getfree), "RAM:" (the RAM volume: its size, what its files take, files / folders).
#define KAPI_VOL_RAM		(1u << 0)	// flags: in memory, lost at a restart
struct kapi_vol_info
{
	unsigned long long total;	// bytes
	unsigned long long free;	// bytes that can still be written
	unsigned long long used;	// bytes taken
	unsigned files, dirs;		// RAM: only (0 elsewhere)
	unsigned flags;			// KAPI_VOL_*
	char     type[12];		// "RAM", "FAT12", "FAT16", "FAT32", "exFAT"
};

// A directory entry from kapi_readdir.
struct kapi_dirent
{
	char     name[128];
	unsigned size;		// bytes (0 for directories)
	int      is_dir;	// 1 if a directory
};

// The calling app's window surfaces, for a user-side chrome (decoration) drawer
// (ABI v28, kapi_get_chrome). `content` is the client canvas; `active`/`inactive` are
// the two pre-composited chrome copies the app draws its title bar / borders / close
// box into (the compositor blits the one matching focus, magenta = transparent). Both
// chrome pointers are 0 for a borderless window. Insets give the client offset inside
// the chrome (client origin = active + (inset_t * chrome_w + inset_l)).
struct kapi_chrome
{
	unsigned *content;		// client canvas (== USER_WINDOW_CANVAS)
	int       content_w, content_h;
	unsigned *active;		// active-focus chrome copy   (0 if borderless)
	unsigned *inactive;		// inactive (unfocused) copy  (0 if borderless)
	int       chrome_w, chrome_h;	// outer size of each chrome copy
	int       inset_l, inset_r, inset_t, inset_b;	// chrome insets (client offset)
	char      title[48];		// window title (kernel-owned copy)
};

// The window frame (v64): the kernel's metrics (kern/gui/window.h WIN_TITLEBAR_H, WIN_BORDER)
// and the title buttons' places -- the kernel hit-tests them, the app draws them into its chrome
// copies (uikit: user/Kits/uikit/skin.cpp). The window menu at the left; from the right: close, maximise,
// minimise -- each KAPI_FRAME_BTN_W x _H, _Y below the frame's top, the outer ones _EDGE from
// its side, _STEP from one to the next. The corners are rounded (radius KAPI_FRAME_RADIUS): in
// the chrome copies a pixel's top byte is its transparency (0 opaque .. 255 see-through), heeded
// in the four corner squares only (the rest of the frame is opaque).
#define KAPI_FRAME_TITLE_H	28
#define KAPI_FRAME_BORDER	4
#define KAPI_FRAME_RADIUS	8
#define KAPI_FRAME_BTN_W	22
#define KAPI_FRAME_BTN_H	19
#define KAPI_FRAME_BTN_Y	5
#define KAPI_FRAME_BTN_EDGE	6
#define KAPI_FRAME_BTN_STEP	25
#define KAPI_FRAME_MENU		0	// the title buttons (GUI_EVENT_WINCTL's value: MENU, MAXIMISE;
#define KAPI_FRAME_CLOSE	1	// the kernel closes and minimises by itself)
#define KAPI_FRAME_MAXIMISE	2
#define KAPI_FRAME_MINIMISE	3
// The caller's window (v64 win_geometry): its frame's place and size on the screen, its client
// area's size, the work area (the screen less the menu bar and the dock: where a maximised window
// goes), KAPI_WIN_* state.
struct kapi_win_geom
{
	int x, y, w, h;			// the whole window (frame included)
	int cw, ch;			// its client area
	int ax, ay, aw, ah;		// the work area
	unsigned state;
};

// A USB gamepad's raw state (ABI v50, kapi_pad_state). For a pad Circle knows (props bit 0:
// Xbox 360 / One, PS3 / PS4, Switch Pro) `buttons` uses Circle's TGamePadButton bits
// (circle/usb/usbgamepad.h) and axes 0..3 are the left / right sticks; for any other HID
// pad they are the report's own buttons (bit 0 = button 1), axes and hats (0..7 = N, NE,
// E ... NW, else centred). user/Include/gamepad.h turns this into PAD_UP / PAD_A ... masks.
#define KAPI_CORE_IDLE		0	// core_state() values
#define KAPI_CORE_RUNNING	1
#define KAPI_CORE_NOTYOURS	(-1)
#define KAPI_CORE_FAULT		(-2)

// USB MIDI input (ABI v68, kapi_midi_read): one event per USB MIDI packet, in arrival order,
// from every class-compliant device plugged in (Circle's CUSBMIDIDevice: umidi1..). time_us is
// the kernel's microsecond clock (CTimer::GetClockTicks, wrapping at 2^32; user/kapi.h's
// kapi_clock_us reads the same clock, on an app core too) when the packet arrived.
// length 1..3 bytes are valid in status, data1, data2 (a SysEx comes in pieces of up to 3
// bytes, each its own event: F0 .. F7 with the bytes between).
#define KAPI_MIDI_DEVICES	4		// devices read at once (umidi1..4)
struct kapi_midi_event
{
	unsigned      time_us;		// arrival (1 MHz clock)
	unsigned char cable;		// the device's virtual cable (0..15)
	unsigned char status;		// the MIDI bytes (running status already expanded by USB MIDI)
	unsigned char data1, data2;
	unsigned char device;		// its umidiN number (1..)
	unsigned char length;		// valid bytes among status, data1, data2
	unsigned char reserved[2];
};

#define KAPI_PAD_MAX	4
#define KAPI_PAD_AXES	16
#define KAPI_PAD_HATS	6
struct kapi_pad
{
	unsigned short vid, pid;	// USB vendor / product ids
	unsigned props;			// TGamePadProperty bits (bit 0: known mapping)
	int      focus;			// 1: the caller's window has the keyboard (react only then)
	unsigned seq;			// reports received (changes with every report)
	int      nbuttons;
	unsigned buttons;
	int      naxes;
	struct { int value, minimum, maximum; } axes[KAPI_PAD_AXES];
	int      nhats;
	int      hats[KAPI_PAD_HATS];
};

// A GPU vertex (kapi v52): position in normalized device coordinates + colour (RGBA8).
struct kapi_gpu_vertex { float x, y, z; unsigned char r, g, b, a; };
#define KAPI_GPU_MAX_VERTS	(3 * 65536)

// kapi v53 (gpu_texture / gpu_render). A vertex: its position, transformed by the batch's
// matrix into clip space (the GPU divides by w and clips), its texture coordinates (0..1
// across the texture) and its colour (multiplied by the texel when the batch is textured);
// v54: a second colour, added (0 0 0 0: none -- what v53 had there).
struct kapi_gpu_vertex3
{
	float x, y, z, w;
	float s, t;
	unsigned char r, g, b, a;
	unsigned char r2, g2, b2, a2;	// (v54; 0 before)
};
// A batch: vertices [first, first + count) of the gpu_render call (a triangle list), drawn
// with its own state. matrix: row by row, clip = matrix * (x y z w) (KAPI_GPU_B_NOMATRIX:
// the identity -- the vertices are already in clip space).
struct kapi_gpu_batch
{
	unsigned first, count;
	int texture;			// a gpu_texture handle, or -1 (colour only)
	unsigned flags;			// KAPI_GPU_B_*
	float matrix[16];
};
#define KAPI_GPU_B_ZFUNC(f)	((f) & 7)	// the depth test (0 = LESS, the default):
#define KAPI_GPU_Z_LESS		0
#define KAPI_GPU_Z_EQUAL	2
#define KAPI_GPU_Z_LEQUAL	3
#define KAPI_GPU_Z_GREATER	4
#define KAPI_GPU_Z_NOTEQUAL	5
#define KAPI_GPU_Z_GEQUAL	6
#define KAPI_GPU_Z_ALWAYS	7		// (no depth test)
#define KAPI_GPU_B_NOZWRITE	(1u << 3)	// the depth buffer is not updated
#define KAPI_GPU_B_CULL_BACK	(1u << 4)	// front = counter-clockwise in NDC (y up)
#define KAPI_GPU_B_CULL_FRONT	(1u << 5)
#define KAPI_GPU_B_BLEND(m)	(((m) & 15) << 8)
#define KAPI_GPU_BLEND_NONE	0		// opaque
#define KAPI_GPU_BLEND_ALPHA	1		// src * a + dst * (1 - a)
#define KAPI_GPU_BLEND_ADD	2		// src * a + dst
#define KAPI_GPU_BLEND_MUL	3		// src * dst
#define KAPI_GPU_BLEND_PREMUL	4		// src + dst * (1 - a)
// (v72) the compositing presets, premultiplied colours (s, d; sa, da their alphas) -- what blend
// modes are made of (user/Libs/gpucomp: a layer's multiply = MULCOL then UNDER, subtract = RSUB then UNDER)
#define KAPI_GPU_BLEND_MULCOL	5		// colour s d + d (1 - sa); alpha kept
#define KAPI_GPU_BLEND_UNDER	6		// colour s (1 - da) + d; alpha sa + da (1 - sa)
#define KAPI_GPU_BLEND_SCREEN	7		// colour s + d (1 - s); alpha over
#define KAPI_GPU_BLEND_PLUS	8		// colour s + d (clamped); alpha over
#define KAPI_GPU_BLEND_RSUB	9		// colour d - s (clamped at 0); alpha kept
#define KAPI_GPU_BLEND_LIGHTEN	10		// colour max (s, d); alpha over
#define KAPI_GPU_BLEND_DSTIN	11		// colour and alpha d sa (a mask: d kept where s is)
#define KAPI_GPU_BLEND_DSTOUT	12		// colour and alpha d (1 - sa) (d cut out where s is)
#define KAPI_GPU_BLEND_LAST	12
#define KAPI_GPU_B_LINEAR	(1u << 12)	// bilinear texture filtering (else nearest)
#define KAPI_GPU_B_WRAP_S(m)	(((m) & 3) << 13)
#define KAPI_GPU_B_WRAP_T(m)	(((m) & 3) << 15)
#define KAPI_GPU_WRAP_REPEAT	0
#define KAPI_GPU_WRAP_CLAMP	1
#define KAPI_GPU_WRAP_MIRROR	2
#define KAPI_GPU_B_NOMATRIX	(1u << 17)
#define KAPI_GPU_B_ALPHATEST(t)	(1u << 18 | ((t) & 255u) << 19)	// (v54) alpha < t / 255: not drawn

// A window, for the remote desktop (v56 win_list): its client area on the screen (x, y, w,
// h: without the frame; the full-screen window: the whole screen), WIN_FLAG_* flags, its
// opacity, a counter that changes whenever it is redrawn / moved / resized, its state.
#define KAPI_WIN_KEYS		1	// it has the keyboard
#define KAPI_WIN_FULLSCREEN	2	// the full-screen window (its pixels: the screen's)
#define KAPI_WIN_MINIMISED	4	// (v64) minimised: not shown until raised (win_raise / raise_app)
#define KAPI_WIN_OFFDESK	8	// (v65) on another workspace than the current one: not shown
#define KAPI_WIN_DESK(state)	((int) (((state) >> 8) & 0xFF) - 1)	// (v65) its workspace (-1: all)
#define KAPI_DESK_MAX		8	// (v65) workspaces at most (kapi desk)
#define KAPI_WIN_DESKTOP	0xFFFFFFFFu	// the id of the desktop (listed first: the wallpaper
						// + the backmost windows, screen-sized; read whole only)
struct kapi_win_info
{
	unsigned id;			// never reused
	unsigned pid;			// the owner (0: the kernel)
	int x, y, w, h;
	unsigned flags;			// WIN_FLAG_BORDERLESS 1, BACKMOST 2, TOPMOST 4, TRANSPARENT 8, SYSTEM 16
	int alpha;			// 0..255
	unsigned gen;
	unsigned state;			// KAPI_WIN_* | (v65) its desk + 1 << 8 (KAPI_WIN_DESK: 0 = all)
	char title[48];
	int ow, oh;			// the whole window with its frame (0 0: no frame)
	int il, it;			// the client area's place in it (frame left, top)
	unsigned chromeGen;		// changes when the app redraws its frame
};
// The target of gpu_render: w x h pixels (0x00RRGGBB, stride = pixels per row), cleared to
// clear (0xRRGGBB) -- or, KAPI_GPU_F_KEEP, drawn over what they hold.
struct kapi_gpu_frame
{
	unsigned *pixels;
	int w, h, stride;
	unsigned clear;
	unsigned flags;
};
#define KAPI_GPU_F_KEEP		(1u << 0)
#define KAPI_GPU_F_ALPHA	(1u << 1)	// (v70) the target is 0xAARRGGBB (premultiplied): its alpha
						// loaded (KEEP), blended and stored; cleared to clear's top
						// byte. Without it: alpha not kept (0x00RRGGBB, as before)
#define KAPI_GPU_MAX_BATCHES	4096

// kapi v61 (gpu_program / gpu_render2): draws with the app's own QPU shaders (user/Libs/v3d/qpu.h
// builds them; the GameCube's TEV is generated so). A program: the vertex shader (render), the
// coordinate shader (binning) and the fragment shader, as V3D 4.2 instructions. A vertex is
// `inputs` floats, read in order by the vertex shader (the coordinate shader reads the first
// `csInputs`); the first 4 are the clip-space position (x y z w), which the kernel clips
// (the near plane, a guard band) before the GPU, interpolating every float. The vertex shader
// writes Xs Ys Zs 1/Wc then the `varyings` the fragment shader reads (ldvary, in order).
struct kapi_gpu_program
{
	const unsigned long long *vs, *cs, *fs;
	unsigned nvs, ncs, nfs;		// instructions (4096 at most each)
	unsigned inputs;		// floats a vertex (4..64)
	unsigned csInputs;		// floats the coordinate shader reads (4..inputs)
	unsigned csOutputs;		// values the coordinate shader writes (6: Xc Yc Zc Wc Xs Ys)
	unsigned varyings;		// 0..64
	unsigned flags;			// KAPI_GPU_P_*
};
#define KAPI_GPU_P_FS_4WAY	(1u << 0)	// the fragment shader runs 4 threads a QPU (16 registers)
						// (else 2: 32 registers)
#define KAPI_GPU_P_FS_FINAL	(1u << 1)	// the fragment shader starts in its last thread section
						// (no thread switch before its end)
#define KAPI_GPU_P_FS_ZWRITE	(1u << 2)	// the fragment shader writes the depth
#define KAPI_GPU_MAX_PROGRAMS	256
// A batch of gpu_render2: vertices [first, first + count) (triangles) with a program, its
// uniforms (ranges of the call's uniform array: vertex, coordinate, fragment shader), up to 8
// textures -- for each, the index in the fragment uniforms where the kernel puts its two
// configuration words (p0: the texture state, returning 16-bit floats RG / BA; p1: the sampler,
// from texFlags: KAPI_GPU_B_LINEAR / WRAP_S / WRAP_T) -- and its state.
struct kapi_gpu_batch2
{
	unsigned first, count;
	int program;
	unsigned flags;			// KAPI_GPU_B_ZFUNC, NOZWRITE, CULL_BACK / FRONT
	unsigned blend;			// 0 none, else KAPI_GPU_BLEND2 (...)
	unsigned wmask;			// the channels NOT written: bit 0 R, 1 G, 2 B, 3 A
	int scissor[4];			// x, y, w, h in the target (w <= 0: the whole target)
	unsigned vsUni, vsNUni, csUni, csNUni, fsUni, fsNUni;
	int tex[8];			// gpu_texture handles (-1: none)
	unsigned texFlags[8];
	int texUni[8];			// p0 at fsUni + texUni[i], p1 right after (-1: not used)
};
// A batch of gpu_render3: count vertices (triangles) of `stride` floats (>= its program's inputs,
// <= 64) from float `off` of the call's array, the rest as gpu_render2's (b.first unused)
struct kapi_gpu_batch3
{
	struct kapi_gpu_batch2 b;
	unsigned off, stride;
};
// blending (V3D's factors: 0 zero, 1 one, 2 src colour, 3 1 - src colour, 4 dst colour,
// 5 1 - dst colour, 6 src alpha, 7 1 - src alpha, 8 dst alpha, 9 1 - dst alpha, 10 const colour,
// 11 1 - const colour, 12 const alpha, 13 1 - const alpha, 14 src alpha saturate; the equations:
// 0 add, 1 subtract, 2 reverse subtract, 3 min, 4 max)
#define KAPI_GPU_BLEND2(cSrc, cDst, aSrc, aDst, cEq, aEq) \
	(1u | ((cSrc) & 15u) << 4 | ((cDst) & 15u) << 8 | ((aSrc) & 15u) << 12 | ((aDst) & 15u) << 16 \
	 | ((cEq) & 7u) << 20 | ((aEq) & 7u) << 24)
#define KAPI_GPU_MAX_UNIFORMS	(1 << 20)
#define KAPI_GPU_MAX_TEXTURES	1024		// handles in all (v70; 256 before), shared by the programs
#define KAPI_GPU_MAX_TEXTURES_AS 512		// (v70) of them at most a program (the others' share)
#define KAPI_GPU_MAX_TEXSIZE	2048

// (v73) An event of the window, as pop_event returns it (the handler is the app's).
struct kapi_event
{
	unsigned long long handler;	// gui_handler (0: none)
	unsigned long long sender;
	long long value;		// gui_value
	int event;			// GUI_EVENT_*
	unsigned mods;			// GUI_EVENT_KEY: the MOD_* held when the key was typed
};

// (v73) A call posted to the process (post), as pop_post returns it.
struct kapi_posted
{
	unsigned long long fn;		// void fn (void *ctx, long value)
	unsigned long long ctx;
	long long value;
};

// (v74) A process's system calls, as proc_stats returns them. A "slot" is a kapi entry's index
// in TKApiTable counted in 8-byte words (version = 0, create_window = 1, ...): the number the
// EL0 stub puts in x8.
#define KAPI_SYSCALL_STATS_TOP	8
struct kapi_syscall_stats
{
	unsigned long long syscalls;	// system calls since the process started
	unsigned long long emulated;	// ID register reads (MRS) emulated by the kernel
	unsigned rate;			// system calls per second: the last full window (>= 1 s),
					// or the current one when it is over 1 s (an idle process: 0)
	unsigned slots;			// the kernel's table slots (KAPI_TABLE_SLOTS: the version's)
	unsigned top_slot[KAPI_SYSCALL_STATS_TOP];	 // the slots most called, the most first (0: none)
	unsigned top_count[KAPI_SYSCALL_STATS_TOP]; // their counts (saturating at 0xFFFFFFFF)
	unsigned reserved[4];		// 0
};

// ---- v75: the POSIX layer's kernel half (docs/POSIX-PLAN.md §3) ------------------------------
// 64-bit values are `long long` / `unsigned long long`, never `long` (32 bits in the Windows
// build of the apps). Each structure's size and its key field offsets are the ABI: checked below.

#ifdef __cplusplus
#define KAPI_STATIC_ASSERT(c, m)	static_assert (c, m)
#else
#define KAPI_STATIC_ASSERT(c, m)	_Static_assert (c, m)
#endif

// (v75) The error values: a v75 call returns -KAPI_Exxx on failure. KAPI_Exxx = newlib's errno
// value (sys/errno.h), so a libc does errno = -r.
#define KAPI_EPERM		1
#define KAPI_ENOENT		2
#define KAPI_ESRCH		3
#define KAPI_EINTR		4
#define KAPI_EIO		5
#define KAPI_EBADF		9
#define KAPI_ECHILD		10
#define KAPI_EAGAIN		11
#define KAPI_ENOMEM		12
#define KAPI_EACCES		13
#define KAPI_EFAULT		14
#define KAPI_EBUSY		16
#define KAPI_EEXIST		17
#define KAPI_EXDEV		18
#define KAPI_ENODEV		19
#define KAPI_ENOTDIR		20
#define KAPI_EISDIR		21
#define KAPI_EINVAL		22
#define KAPI_ENFILE		23
#define KAPI_EMFILE		24
#define KAPI_EFBIG		27
#define KAPI_ENOSPC		28
#define KAPI_ESPIPE		29
#define KAPI_EROFS		30
#define KAPI_EPIPE		32
#define KAPI_ENOSYS		88
#define KAPI_ENOTEMPTY		90
#define KAPI_ENAMETOOLONG	91
#define KAPI_EOPNOTSUPP		95
#define KAPI_ECONNRESET		104
#define KAPI_ENOBUFS		105
#define KAPI_EAFNOSUPPORT	106
#define KAPI_ENOTSOCK		108
#define KAPI_ENOPROTOOPT	109
#define KAPI_ECONNREFUSED	111
#define KAPI_EADDRINUSE		112
#define KAPI_ECONNABORTED	113
#define KAPI_ENETUNREACH	114
#define KAPI_ENETDOWN		115
#define KAPI_ETIMEDOUT		116
#define KAPI_EHOSTUNREACH	118
#define KAPI_EINPROGRESS	119
#define KAPI_EALREADY		120
#define KAPI_EDESTADDRREQ	121
#define KAPI_EMSGSIZE		122
#define KAPI_EPROTONOSUPPORT	123
#define KAPI_EADDRNOTAVAIL	125
#define KAPI_EISCONN		127
#define KAPI_ENOTCONN		128
#define KAPI_ENOTSUP		134

// (v75, WP-MEM) Memory: vm_* (mmap, mprotect, madvise), threads with their TLS and stack.
#define KAPI_PROT_NONE		0
#define KAPI_PROT_READ		1
#define KAPI_PROT_WRITE		2
#define KAPI_PROT_EXEC		4		// (v78) anonymous regions only; before: -KAPI_ENOTSUP
#define KAPI_MAP_FIXED		0x10
#define KAPI_MAP_NORESERVE	0x4000
#define KAPI_MAP_POPULATE	0x8000
#define KAPI_MAP_FIXED_NOREPLACE 0x100000
#define KAPI_MADV_NORMAL	0
#define KAPI_MADV_RANDOM	1
#define KAPI_MADV_SEQUENTIAL	2
#define KAPI_MADV_WILLNEED	3
#define KAPI_MADV_DONTNEED	4
#define KAPI_MADV_FREE		8
#define KAPI_VMK_ANON		1		// vm_map
#define KAPI_VMK_HEAP		2
#define KAPI_VMK_STACK		3
#define KAPI_VMK_IMAGE		4
#define KAPI_VMK_FIXED		5		// canvas, surface, sound ring, GPU memory, code arena, kapi pages
#define KAPI_VMF_LAZY		1
#define KAPI_THREAD_DETACHED	1		// no join: the kernel frees its record when it ends

struct kapi_vm_region				// 32 bytes
{
	unsigned long long start, end;		// 0, 8: [start, end), 64 KB-aligned
	unsigned prot;				// 16: KAPI_PROT_*
	unsigned kind;				// 20: KAPI_VMK_*
	unsigned resident;			// 24: pages present
	unsigned flags;				// 28: KAPI_VMF_*
};

struct kapi_vm_stats				// 48 bytes
{
	unsigned long long resident;		// 0: bytes of owned frames, page tables included
	unsigned long long lazy;		// 8: bytes of VA in lazy regions
	unsigned long long writable;		// 16: bytes of writable VA (all lazy regions touched)
	unsigned long long faults;		// 24: pages filled on demand (EL0 + kernel + app cores)
	unsigned long long pt_bytes;		// 32: page tables
	unsigned long long limit;		// 40: per-process limit, 0 = none
};

struct kapi_thread_attr				// 64 bytes
{
	unsigned long long fn;			// 0: int (*) (void *)
	unsigned long long arg;			// 8
	unsigned long long stack_size;		// 16: 0 = 8 MB; 16 KB..16 MB (lazy)
	unsigned long long tls;			// 24: the thread's initial TPIDR_EL0
	const char *name;			// 32: may be 0; 31 characters kept
	unsigned flags;				// 40: KAPI_THREAD_DETACHED
	int prio;				// 44: 0, or 1 = "real time" (as thread_priority)
	unsigned long long reserved[2];		// 48: 0
};

struct kapi_thread_info				// 32 bytes
{
	unsigned long long stack_lo;		// 0: lowest usable byte of its stack VMA
	unsigned long long stack_hi;		// 8: its top (the initial SP)
	int tid;				// 16
	int state;				// 20: 0 running, 1 ended (joinable)
	unsigned long long guard;		// 24: unmapped bytes below stack_lo
};

// (v75, WP-FILE/PROC) Files (descriptors, stat, directories), processes (spawn / wait, argv,
// environment), the clock.
#define KAPI_O_RDONLY		0
#define KAPI_O_WRONLY		1
#define KAPI_O_RDWR		2
#define KAPI_O_ACCMODE		3
#define KAPI_O_CREAT		0x40
#define KAPI_O_EXCL		0x80
#define KAPI_O_TRUNC		0x200
#define KAPI_O_APPEND		0x400
#define KAPI_SEEK_SET		0
#define KAPI_SEEK_CUR		1
#define KAPI_SEEK_END		2
#define KAPI_S_IFMT		0170000
#define KAPI_S_IFDIR		0040000
#define KAPI_S_IFREG		0100000
#define KAPI_UNLINK_DIR		1		// rmdir semantics
#define KAPI_WAIT_NOHANG	1
#define KAPI_WAIT_KEEP		2		// do not close the process handle
#define KAPI_PROC_EXITED	0
#define KAPI_PROC_FAULT		1
#define KAPI_PROC_KILLED	2
#define KAPI_PROC_OOM		3
#define KAPI_CLOCK_REALTIME_VALID 1		// the date is real (NTP / RTC), not "since boot"

struct kapi_stat				// 64 bytes
{
	unsigned long long size;		// 0
	long long mtime;			// 8: UTC seconds since 1970
	unsigned long long ino;			// 16: FNV-1a 64 of the upper-cased absolute path
	unsigned mode;				// 24: KAPI_S_IF* | permission bits
	unsigned dev;				// 28: volume number
	unsigned blksize;			// 32: cluster size (RAM: 65536)
	unsigned attr;				// 36: FAT attributes (1 RO, 2 HID, 4 SYS, 0x10 DIR, 0x20 ARC)
	unsigned long long blocks;		// 40: 512-byte blocks allocated
	long long ctime;			// 48: = mtime (FF_FS_CRTIME 0)
	unsigned long long reserved;		// 56
};

struct kapi_dirent2				// 288 bytes
{
	char name[256];				// 0: up to 255 characters (kapi_dirent cut at 127)
	unsigned long long size;		// 256
	long long mtime;			// 264
	unsigned mode;				// 272
	unsigned attr;				// 276
	unsigned long long ino;			// 280
};

struct kapi_spawn_attr				// 64 bytes
{
	const char *path;			// 0: the program (resolved against cwd)
	const char *argv;			// 8: block "a\0b\0\0" (argv[0] first), <= 64 KB
	const char *envp;			// 16: same format; 0 = the caller's initial environment
	const char *cwd;			// 24: 0 = the caller's
	void *in, *out;				// 32, 40: stream handles of the caller, or 0
	unsigned long long reserved;		// 48: 0 (a future stderr)
	unsigned flags;				// 56: 0
	unsigned reserved2;			// 60
};

struct kapi_proc_status				// 16 bytes
{
	int code;				// 0: the exit status (FAULT -11, KILLED / OOM -9)
	int reason;				// 4: KAPI_PROC_*
	int pid;				// 8
	int reserved;				// 12
};

struct kapi_clock_info				// 48 bytes
{
	unsigned long long cnt;			// 0: CNTPCT_EL0 at the sample
	unsigned long long freq;		// 8: CNTFRQ_EL0
	long long utc_us;			// 16: UTC microseconds since 1970 at cnt
	int tz_minutes;				// 24: local - UTC (set_timezone)
	unsigned flags;				// 28: KAPI_CLOCK_*
	unsigned long long boot_cnt;		// 32: CNTPCT at boot
	unsigned long long reserved;		// 40
};

// (v75, WP-NET) BSD sockets (IPv4: TCP, UDP) and poll.
#define KAPI_AF_INET		2
#define KAPI_SOCK_STREAM	1
#define KAPI_SOCK_DGRAM		2
#define KAPI_SOCKF_NONBLOCK	1
#define KAPI_MSG_PEEK		0x2
#define KAPI_MSG_DONTWAIT	0x40
#define KAPI_MSG_WAITALL	0x100
#define KAPI_SHUT_RD		0
#define KAPI_SHUT_WR		1
#define KAPI_SHUT_RDWR		2
#define KAPI_SO_ERROR		1		// get: pending error (positive errno), cleared
#define KAPI_SO_NONBLOCK	2		// get / set 0/1
#define KAPI_SO_RCVTIMEO_MS	3
#define KAPI_SO_SNDTIMEO_MS	4
#define KAPI_SO_BROADCAST	5
#define KAPI_SO_NREAD		6		// get: bytes in the carry buffer, 1 if more is ready
#define KAPI_SO_TYPE		7
#define KAPI_SO_ACCEPTCONN	8
#define KAPI_POLLIN		0x001
#define KAPI_POLLPRI		0x002
#define KAPI_POLLOUT		0x004
#define KAPI_POLLERR		0x008
#define KAPI_POLLHUP		0x010
#define KAPI_POLLNVAL		0x020
#define KAPI_PK_NONE		0
#define KAPI_PK_SOCKET		1
#define KAPI_PK_STREAM		2
#define KAPI_PK_FILE		3
#define KAPI_POLL_MAX		1024

struct kapi_sockaddr				// 16 bytes, IPv4 only
{
	unsigned short family;			// 0: KAPI_AF_INET
	unsigned short port;			// 2: host byte order
	unsigned char addr[4];			// 4: a.b.c.d
	unsigned char zero[8];			// 8
};

struct kapi_pollfd				// 16 bytes
{
	int kind;				// 0: KAPI_PK_*
	int h;					// 4: socket number, or a handle's value (<= 0xFFFFFF)
	short events;				// 8
	short revents;				// 10
	int reserved;				// 12
};

// (v76, WP-IPC) Local sockets, handles passed between processes, shared memory (docs/POSIX-PLAN.md
// §14). A local socket's number is a handle of the caller's table (always >= KAPI_SOCK_LOCAL_BASE;
// IP sockets are 0..255): the sock_* calls and poll (KAPI_PK_SOCKET) take either.
#define KAPI_AF_UNIX		1
#define KAPI_SOCK_SEQPACKET	5
#define KAPI_SOCK_LOCAL_BASE	0x10000		// local socket numbers are >= this
#define KAPI_MSG_CTRUNC		0x8		// recvmsg out: handles dropped (no room in the array / table)
#define KAPI_MSG_TRUNC		0x20		// recvmsg out: a datagram cut to the buffers
#define KAPI_MSG_NOSIGNAL	0x4000		// accepted, ignored (no signals: EPIPE)
#define KAPI_SO_RCVBUF		9		// local sockets: the receive queue's limit (bytes)
#define KAPI_SO_SNDBUF		10		// local sockets: the largest datagram / packet (bytes)
#define KAPI_SO_PEERPID		11		// get: the peer's pid (local sockets; -ENOTCONN)
#define KAPI_SO_DOMAIN		12		// get: KAPI_AF_INET / KAPI_AF_UNIX
#define KAPI_HK_NONE		0		// a handle's kind (struct kapi_handle_xfer)
#define KAPI_HK_OFILE		1		// a file_open handle (the description is shared)
#define KAPI_HK_STREAM		2		// a stream handle: a pipe (both ends), file_in / file_out
#define KAPI_HK_SOCKET		3		// an IP socket number (0..255)
#define KAPI_HK_LSOCK		4		// a local socket
#define KAPI_HK_SHM		5		// a shared memory object
#define KAPI_HXF_WRITER		1		// kapi_handle_xfer.flags: a pipe's write end (a STREAM): the
						// pipe's end-of-file then waits for this holder too
#define KAPI_IPC_HANDLES_MAX	256		// handles per message, per spawn_ex2
#define KAPI_IPC_IOV_MAX	64		// iovecs per message
#define KAPI_SHM_ALLOW_SEALING	1		// shm_create: seals may be added (else F_SEAL_SEAL is set)
#define KAPI_SHM_GET_SIZE	1		// shm_ctl ops
#define KAPI_SHM_SET_SIZE	2
#define KAPI_SHM_ADD_SEALS	3
#define KAPI_SHM_GET_SEALS	4
#define KAPI_SHM_GET_ID		5		// a number naming the object system-wide (stat's st_ino)
#define KAPI_SHM_GET_ACCESS	6		// KAPI_O_RDONLY / KAPI_O_RDWR of this handle
#define KAPI_SEAL_SEAL		1		// = Linux's F_SEAL_*
#define KAPI_SEAL_SHRINK	2
#define KAPI_SEAL_GROW		4
#define KAPI_SEAL_WRITE		8
#define KAPI_SHM_NAME_MAX	63		// shm_open's name ("/x" or "x"), without the leading '/'
#define KAPI_VMK_SHM		6		// vm_query: a shm_map region

struct kapi_iovec				// 16 bytes
{
	unsigned long long base;		// 0
	unsigned long long len;			// 8
};

struct kapi_handle_xfer				// 24 bytes
{
	long long h;				// 0: a handle's value, or an IP socket's number
	int kind;				// 8: KAPI_HK_*
	unsigned tag;				// 12: the sender's word, given to the receiver as it is
	int fd;					// 16: spawn_ex2 / get_handles: the child's descriptor
	unsigned flags;				// 20: KAPI_HXF_* (given back as sent)
};

struct kapi_msghdr				// 48 bytes
{
	const struct kapi_iovec *iov;		// 0: the data (sendmsg: read, recvmsg: written)
	struct kapi_handle_xfer *handles;	// 8: sendmsg: to pass; recvmsg: received (0: none)
	unsigned iovcnt;			// 16: <= KAPI_IPC_IOV_MAX
	unsigned nhandles;			// 20: sendmsg: count; recvmsg: capacity in, received out
	unsigned flags;				// 24: recvmsg out: KAPI_MSG_TRUNC / KAPI_MSG_CTRUNC
	unsigned reserved;			// 28: 0
	unsigned long long reserved2[2];	// 32: 0
};

// (v77) A program image (image_list): a program file held once in memory (kern/image.h).
#define KAPI_IMG_KEPT		1		// preloaded: stays when no process runs it
#define KAPI_IMG_LOADING	2		// being read from its file
#define KAPI_IMG_UNNAMED	4		// unloaded, or its file changed: only its processes still use it
#define KAPI_IMG_LIB		8		// (v83) a shared library (lib_open), not a program

// (v85) A channel of the sound's mixer (sound_clients).
#define KAPI_SOUND_NAME	24
struct kapi_sound_client
{
	unsigned pid;			// the program
	int	 volume;		// 0..100
	int	 mute;			// 0 / 1
	int	 peak;			// its level now, 0..32767 (a meter)
	int	 queued;		// frames waiting in its stream
	char	 name[KAPI_SOUND_NAME];	// the program's name ("media", "koton", "basic")
	int	 reserved[4];
};

// (v84) The sound's outputs (sound_output; SD:/etc/sound.ini "output = auto | jack | usb | hdmi").
#define KAPI_SND_OUT_AUTO	0		// a USB audio device if there is one, else the jack, else HDMI
#define KAPI_SND_OUT_JACK	1		// the 3.5 mm jack (PWM)
#define KAPI_SND_OUT_USB	2		// a USB headset / DAC
#define KAPI_SND_OUT_HDMI	3		// the screen (HDMI 0)
#define KAPI_SND_OUT_NOW(r)	((r) & 0xFF)		// sound_output's result: what plays now (0: nothing)
#define KAPI_SND_OUT_ASKED(r)	(((r) >> 8) & 0xFF)	// ... what is asked for
#define KAPI_SND_OUT_HAS(r, o)	((((r) >> 16) >> (o)) & 1)	// ... is output o there
#define KAPI_IMG_PATH_MAX	256

// (v81) set_cursor: the pointer's shapes (kernel/gui/cursors.inc, drawn by tools/gui/gen_cursors.py).
#define KAPI_CURSOR_ARROW	0
#define KAPI_CURSOR_HAND	1		// a link, something to click
#define KAPI_CURSOR_TEXT	2		// text that can be selected or typed (the I bar)
#define KAPI_CURSOR_MOVE	3		// four arrows: something moved
#define KAPI_CURSOR_SIZE_H	4		// two arrows, left and right (a column's edge, a splitter)
#define KAPI_CURSOR_SIZE_V	5		// up and down
#define KAPI_CURSOR_SIZE_NWSE	6		// a corner dragged: top left / bottom right
#define KAPI_CURSOR_SIZE_NESW	7		// top right / bottom left
#define KAPI_CURSOR_CELL	8		// a thick cross: a spreadsheet's cells
#define KAPI_CURSOR_CROSSHAIR	9		// a thin cross: a precise point (drawing)
#define KAPI_CURSOR_WAIT	10		// an hourglass: the app is busy
#define KAPI_CURSOR_NO		11		// a barred circle: not here
#define KAPI_CURSOR_COUNT	12

// (v80) cpu_stats: a core's role, the microseconds it was busy since the boot, its owner.
#define KAPI_CORE_SYSTEM	0		// the scheduler's: the kernel and every process (core 0)
#define KAPI_CORE_SOUND		1		// renders the sound (core 1)
#define KAPI_CORE_APP		2		// an app core (kapi_core_acquire): pid = its owner, 0 when free
#define KAPI_CORE_NETWORK	3		// the network stack (netcore=1: core 3)
#define KAPI_CPU_CORES		8

struct kapi_cpu_core				// 16 bytes
{
	unsigned long long busy_us;		// 0: system/network: its tasks ran (not the idle one); sound:
						//    rendering; app: its jobs ran
	unsigned role;				// 8: KAPI_CORE_*
	unsigned pid;				// 12: an app core's owner (0: free; the other roles: 0)
};

struct kapi_cpu_stats				// 144 bytes
{
	unsigned long long now_us;		// 0: the clock when these were read (two reads: a load)
	unsigned cores;				// 8: how many of core[] are filled
	unsigned reserved;			// 12: 0
	struct kapi_cpu_core core[KAPI_CPU_CORES];	// 16
};

// (v80) net_stats: the payload bytes through a process's sockets (TCP and UDP) since it started
// (pid 0: through every process's since the boot), its sockets open now.
struct kapi_net_stats				// 24 bytes
{
	unsigned long long rx_bytes;		// 0
	unsigned long long tx_bytes;		// 8
	unsigned sockets;			// 16
	unsigned reserved;			// 20: 0
};

struct kapi_image_info				// 280 bytes
{
	unsigned long long size;		// 0: bytes of memory it holds (once, whatever the processes)
	unsigned long long file_size;		// 8: its file's size when it was loaded
	unsigned refs;				// 16: the processes mapping it (+ the tasks loading / waiting)
	unsigned flags;				// 20: KAPI_IMG_*
	char path[KAPI_IMG_PATH_MAX];		// 24: its key: the program's canonical path (lower case)
};

// The v75 structures' layout (the 64-bit ABI: pointers are 8 bytes).
#define KAPI_CHECK_SIZE(type, size) \
	KAPI_STATIC_ASSERT (sizeof (struct type) == (size), "sizeof (struct " #type ") is not " #size)
#define KAPI_CHECK_FIELD(type, field, off) \
	KAPI_STATIC_ASSERT (__builtin_offsetof (struct type, field) == (off), #type "." #field " is not at " #off)
KAPI_CHECK_SIZE (kapi_vm_region, 32);
KAPI_CHECK_FIELD (kapi_vm_region, flags, 28);
KAPI_CHECK_SIZE (kapi_vm_stats, 48);
KAPI_CHECK_SIZE (kapi_thread_attr, 64);
KAPI_CHECK_FIELD (kapi_thread_attr, name, 32);
KAPI_CHECK_FIELD (kapi_thread_attr, flags, 40);
KAPI_CHECK_FIELD (kapi_thread_attr, reserved, 48);
KAPI_CHECK_SIZE (kapi_thread_info, 32);
KAPI_CHECK_FIELD (kapi_thread_info, guard, 24);
KAPI_CHECK_SIZE (kapi_stat, 64);
KAPI_CHECK_FIELD (kapi_stat, mode, 24);
KAPI_CHECK_FIELD (kapi_stat, blocks, 40);
KAPI_CHECK_SIZE (kapi_dirent2, 288);
KAPI_CHECK_FIELD (kapi_dirent2, size, 256);
KAPI_CHECK_FIELD (kapi_dirent2, ino, 280);
KAPI_CHECK_SIZE (kapi_spawn_attr, 64);
KAPI_CHECK_FIELD (kapi_spawn_attr, in, 32);
KAPI_CHECK_FIELD (kapi_spawn_attr, flags, 56);
KAPI_CHECK_SIZE (kapi_proc_status, 16);
KAPI_CHECK_SIZE (kapi_clock_info, 48);
KAPI_CHECK_FIELD (kapi_clock_info, tz_minutes, 24);
KAPI_CHECK_FIELD (kapi_clock_info, boot_cnt, 32);
KAPI_CHECK_SIZE (kapi_sockaddr, 16);
KAPI_CHECK_FIELD (kapi_sockaddr, addr, 4);
KAPI_CHECK_SIZE (kapi_pollfd, 16);
KAPI_CHECK_FIELD (kapi_pollfd, revents, 10);
KAPI_CHECK_SIZE (kapi_iovec, 16);
KAPI_CHECK_SIZE (kapi_handle_xfer, 24);
KAPI_CHECK_FIELD (kapi_handle_xfer, kind, 8);
KAPI_CHECK_FIELD (kapi_handle_xfer, fd, 16);
KAPI_CHECK_SIZE (kapi_msghdr, 48);
KAPI_CHECK_FIELD (kapi_msghdr, iovcnt, 16);
KAPI_CHECK_FIELD (kapi_msghdr, flags, 24);
KAPI_CHECK_SIZE (kapi_image_info, 280);
KAPI_CHECK_FIELD (kapi_image_info, path, 24);

struct TKApiTable
{
	unsigned version;		// KAPI_ABI_VERSION the kernel filled

	// --- windowing ---
	unsigned *(*create_window) (int w, int h, const char *title);
	unsigned *(*create_window_ex) (int x, int y, int w, int h, const char *title,
				       unsigned flags);
	unsigned *(*resize_window) (int w, int h);
	int (*launch) (const char *name);
	int (*toggle_app) (const char *name);
	int (*raise_app) (const char *name);
	int (*list_windows) (char *buf, unsigned size);
	int (*wallpaper_generate) (unsigned base, int points, unsigned seed);
	void (*present) (void);
	unsigned (*get_ticks) (void);
	void (*msleep) (unsigned ms);
	void (*yield) (void);
	void (*exit) (int status);

	// (The kernel-drawn widget API -- add_button/label/checkbox/textbox/progress/
	// slider/textarea/scrollbar/icon + widget_get/set_* -- was removed once every app
	// moved to the user-side uikit toolkit. Kernel modal dialogs draw their own
	// controls internally; nothing calls these through the table any more.)

	// --- events ---
	void (*pump_events) (void);
	void (*wait_for_exit) (void);
	int (*should_exit) (void);

	// --- app-drawn text + keyboard ---
	void (*draw_text) (int, int, const char *, unsigned);
	int (*font_width) (void);
	int (*font_height) (void);
	void (*set_key_handler) (gui_handler);

	// --- enumeration + clock ---
	int (*list_apps) (char *, unsigned);
	int (*get_datetime) (int *, int *, int *, int *, int *, int *);

	// --- console + files ---
	int (*write) (int, const void *, unsigned);
	void *(*open) (const char *);
	int (*read) (void *, void *, unsigned);
	unsigned (*fsize) (void *);
	void (*close) (void *);
	int (*save_file) (const char *, const void *, unsigned);

	// --- v1 additions (append below; bump KAPI_ABI_VERSION) ---
	// The calling app's folder: "SD:apps/<name>.app/" into buf. Returns length.
	int (*app_dir) (char *buf, unsigned size);

	// --- v2 additions ---
	// Canvas-click handler: GUI_EVENT_CANVAS_CLICK with (clientX<<16)|clientY when a
	// press lands in the client area on no widget. For app-drawn mouse UIs.
	void (*set_click_handler) (gui_handler);

	// --- v3 additions ---
	// Directory listing (FatFs). opendir returns a handle (0 on failure); readdir
	// fills *ent and returns 1, or 0 at end; closedir releases it.
	void *(*opendir) (const char *path);
	int   (*readdir) (void *dir, struct kapi_dirent *ent);
	void  (*closedir) (void *dir);

	// --- v4 additions (stdio / streams / processes) ---
	void *(*pipe) (void);				// in-memory FIFO stream
	void *(*file_in) (const char *path);		// file -> stream (read)
	void *(*file_out) (const char *path, int append); // stream -> file (trunc/append)
	int   (*stream_read) (void *h, void *buf, unsigned len);   // 0 = EOF
	int   (*stream_write) (void *h, const void *buf, unsigned len);
	void  (*stream_close) (void *h);		// drop a ref
	int   (*stdin_read) (void *buf, unsigned len);	// this task's stdin (0 = EOF)
	int   (*stdout_write) (const void *buf, unsigned len);	// this task's stdout
	void *(*spawn) (const char *path, const char *args, void *in, void *out); // -> proc
	int   (*wait) (void *proc);			// block (cooperative) -> exit status
	int   (*get_args) (char *buf, unsigned size);	// this task's argv string

	// --- v5 additions ---
	int   (*stream_read_nb) (void *h, void *buf, unsigned len); // >0 / 0=EOF / -1=block

	// --- v6 additions ---
	void  (*stream_eof) (void *h);		// signal EOF to readers (writer done)
	int   (*proc_done) (void *proc);	// 1 if a spawned process has finished

	// --- v7 additions ---
	int   (*mkdir) (const char *path);	// 0 ok / -1
	int   (*remove) (const char *path);	// file or empty dir
	int   (*rename) (const char *from, const char *to);
	void  (*cursor_pos) (int *x, int *y);	// cursor, relative to this window's client

	// --- v8 additions ---
	int   (*list_tasks) (char *buf, unsigned size);	// "<state><kind> <name>" per line
	int   (*kill) (const char *name);		// kill an app by name (not kernel/self)

	// (v9/v10 message_box + file_open/file_save were removed: kernel modal dialogs are
	// gone -- apps use the user-side uidialog.hpp (ui::MessageBox / ui::FileDialog).)

	// --- v11 additions ---
	// Run an ELF at an absolute SD path with an argv string (fire-and-forget: no
	// stdio, no wait handle; the task name is derived from the path). Returns 1/0.
	// Used by the file manager to open documents (app + file) and run programs.
	int   (*exec) (const char *path, const char *args);
	// Framebuffer dimensions (for edge-pinned/borderless windows like the panel).
	void  (*screen_size) (int *w, int *h);

	// --- v12 additions ---
	// Move the calling app's window (outer top-left, screen coords). For borderless
	// windows that re-position themselves, e.g. the panel keeping itself centered.
	void  (*move_window) (int x, int y);

	// --- v13 additions (app-drawn desktop wallpaper) ---
	// Map the shared screen-sized wallpaper buffer (0x00RRGGBB) into this app and
	// return its VA (+ dims via w/h). Draw into it, then wallpaper_commit() to make
	// it the live background. Frames are kernel-owned -> persists after the app exits.
	unsigned *(*wallpaper_buffer) (int *w, int *h);
	void      (*wallpaper_commit) (void);

	// --- v14 additions (ps / kill by PID) ---
	// list_procs: one line per task "<pid> <a|k> <state> <name>" (pid 0 = kernel
	// task). kill_pid: nForce 0 = clean close (window exit flag), 1 = hard terminate;
	// returns 1 killed/signalled, 0 no such pid, -1 protected (kernel task or self).
	int (*list_procs) (char *buf, unsigned size);
	int (*kill_pid) (int pid, int force);

	// --- v15 additions (keyboard layout) ---
	// set_keymap: switch to a compiled-in country map ("FR","US","DE","UK","ES",
	// "IT","DV"); 1 ok / 0 unknown-or-no-keyboard. get_keymap: current name into buf.
	int (*set_keymap) (const char *name);
	int (*get_keymap) (char *buf, unsigned size);

	// (v16 set_window_theme removed -- window chrome is drawn user-side; runtime
	// re-theming would be a user-side toolkit concern + a re-decorate broadcast.)

	// --- v17 additions (working directory) ---
	// chdir: set the calling task's cwd (path resolved + verified as a dir); 1/0.
	// getcwd: current cwd into buf. All file kapis resolve relative paths against it,
	// and a spawned child inherits the spawner's cwd.
	int (*chdir) (const char *path);
	int (*getcwd) (char *buf, unsigned size);

	// --- v18 additions (shell as a process) ---
	// This task's own stdin/stdout stream handles, to wire into spawned children
	// (the cmd shell passes its stdin to the first stage; 0 if none).
	void *(*stdin_stream) (void);
	void *(*stdout_stream) (void);

	// --- v19 additions (kernel log viewer) ---
	// Read the next kernel log event (a tee of CLogger's ring; the logs still go to
	// their normal output). 1 + fills severity (0=panic..4=debug)/source/message, or
	// 0 if empty. For a real-time log viewer (kmsg).
	int (*klog_read) (int *severity, char *src, unsigned src_cap, char *msg, unsigned msg_cap);

	// --- v20 additions (verbose logging) ---
	// Toggle / read the kernel's verbose-logging flag (app lifecycle logs). The
	// `verbose` command persists the choice in SD:system.ini.
	int (*set_verbose) (int on);
	int (*get_verbose) (void);

	// --- v21 additions (TCP/IP sockets over WLAN) ---
	// net_status: 1 + dotted IPv4 into ip[] if the link is up, else 0 (ip="").
	// tcp_connect: resolve host (dotted-quad or DNS name) + connect; returns a
	// handle >=0, or <0 on error (-1 no net, -2 too many sockets, -3 DNS fail,
	// -5 connect fail). tcp_send: blocking send (5 s timeout); bytes sent or <0.
	// tcp_recv: NON-BLOCKING; >0 bytes, 0 nothing yet, <0 closed/error. tcp_close:
	// drop the connection + free the handle. Sockets are auto-closed if the owning
	// process dies.
	int  (*net_status) (char *ip, unsigned cap);
	int  (*tcp_connect) (const char *host, unsigned port);
	int  (*tcp_send) (int sock, const void *buf, unsigned len);
	int  (*tcp_recv) (int sock, void *buf, unsigned len);
	void (*tcp_close) (int sock);

	// --- v22 additions (full pointer stream for app-side widget toolkits) ---
	// Opt-in: register a handler that receives GUI_EVENT_PTR_MOVE/DOWN/UP/ENTER/LEAVE
	// for this window's client area, with value = (changed<<40)|(buttons<<32)|
	// (x<<16)|y (client coords; `changed` = the button 1/2/4 for DOWN/UP). Lets a
	// user-space toolkit (uikit.h) own its widgets. The legacy set_click_handler is
	// unchanged.
	void (*set_pointer_handler) (gui_handler fn);

	// --- v23 additions (memory info) ---
	// System memory snapshot, all in KB: *total RAM, *free (unallocated page region +
	// free heap), *app (owned by user processes), *page_kb (page size). Any pointer
	// may be 0. Returns 1. For the memory monitor + accounting.
	int (*meminfo) (unsigned long *total_kb, unsigned long *free_kb,
			unsigned long *app_kb, unsigned *page_kb);

	// --- v24 additions (per-process heap) ---
	// Unix-style sbrk: move the calling app's heap break by `increment` bytes
	// (mapping fresh pages as it grows), return the previous break, or (void*)-1 on
	// failure. The foundation for a user-space allocator (user/Runtime/umm.h: malloc/free +
	// operator new/delete). Heap pages are owned by the address space -> freed on exit.
	void *(*sbrk) (long increment);

	// --- v25 additions (power) ---
	// Reboot the machine immediately (Circle reboot(); does not return). Used to
	// apply settings that are only read at boot -- e.g. wpaconf rewriting the WLAN
	// config in SD:/etc/wpa_supplicant.conf, which the kernel reads during bring-up.
	void (*reboot) (void);

	// --- v26 additions (keyboard readiness) ---
	// 1 if a USB keyboard is attached & ready, else 0. The kernel no longer applies
	// any keyboard layout from cmdline; the `keyb` tool (run from autostart) polls
	// this then calls set_keymap -- it may start before USB enumeration finishes.
	int (*kbd_ready) (void);

	// --- v27 additions (file-based keymaps) ---
	// Load a keyboard layout from a SD:/etc/keymaps/<X>.kmap blob: header "OKM1" +
	// u16 rows(128) + u16 cols(5) + rows*cols u16 table (row-major m_KeyMap[phy][tab]).
	// The kernel validates + copies it into the live keyboard; the caller frees the
	// buffer. `name` is recorded for get_keymap. Returns 1 on success, 0 otherwise.
	// New layouts can be added as files with no kernel rebuild (see tools/keymaps).
	int (*set_keymap_data) (const char *name, const void *data, unsigned len);

	// --- v28 additions (user-side window chrome) ---
	// get_chrome: fill *out with the calling app's window surfaces (content canvas +
	// the active/inactive chrome copies + insets + title) so a user-side toolkit can
	// draw the title bar / borders / close box. Returns 1, or 0 if the app has no
	// window. draw_text_buf: render kernel-font text (transparent background) into an
	// arbitrary app-mapped 0x00RRGGBB buffer (dstW x dstH) at (x,y) -- used to draw the
	// title into the chrome buffer (the kernel font is the only font apps have). The
	// kernel still owns chrome BEHAVIOUR (title-bar drag, close-box hit-test).
	int  (*get_chrome) (struct kapi_chrome *out);
	void (*draw_text_buf) (unsigned *dst, int dstW, int dstH, int x, int y,
			       const char *s, unsigned color);

	// --- v30 additions (hardware RNG) ---
	// Fill buf[len] with random bytes from the Pi's hardware RNG (Circle
	// CBcmRandomNumberGenerator). For cryptographic seeding -- e.g. the TLS entropy
	// source in user/Libs/tls/onyx_tls.hpp. Returns the number of bytes written (== len).
	int (*random) (void *buf, unsigned len);

	// --- v33 additions (memory detail for memmon) ---
	// detected_kb = firmware-reported physical board RAM (CMachineInfo::GetRAMSize) --
	// e.g. 4 GB even though the managed total (meminfo) is ~3 GB on a 4 GB board.
	// apppool_kb / apppool_free_kb = total / free of the HIGH page zone that backs app
	// frames (palloc_high -- ELF segments, heaps, decoded images). Any out-ptr may be 0.
	int (*ram_detail) (unsigned long *detected_kb, unsigned long *apppool_kb,
			   unsigned long *apppool_free_kb, unsigned long *above4g_kb,
			   unsigned *nsegments);

	// --- v34 additions (scroll-wheel speed) ---
	// Lines scrolled per wheel notch, applied system-wide: the WM multiplies the raw
	// notch by this factor before delivering GUI_EVENT_PTR_WHEEL, so every toolkit/app
	// feels it at once. set clamps to [1,16]; the theme editor persists it in
	// SD:/etc/theme.txt (wheelspeed=N) and the kernel restores it at boot.
	void (*set_wheel_speed) (int lines_per_notch);
	int  (*get_wheel_speed) (void);

	// --- v35 additions (shell surfaces -- activity-shell compositor) ---
	// A shared pixel surface (0x00RRGGBB). The shell creates one sized to a viewport
	// (surface_create -> id > 0), passes the id to an app; both map it (surface_map ->
	// the surface VA in the caller's address space; SAME physical frames, so the app
	// draws straight into the pixels the shell composes from). surface_size fills the
	// agreed w/h; surface_present yields toward the compositing shell; surface_destroy
	// frees it (owner only -- also auto-freed when the owner process dies).
	int       (*surface_create) (int w, int h);
	unsigned *(*surface_map) (int id);
	int       (*surface_size) (int id, int *w, int *h);
	void      (*surface_present) (int id);
	int       (*surface_destroy) (int id);

	// Activity-shell IPC (the kernel is a thin message router; see kern/ipc.h). A user
	// compositor calls register_shell to become THE shell. Apps post to it with
	// shell_request (the kernel stamps the caller's pid). The shell replies / pushes
	// async events with mailbox_send(target_pid,...). Both drain with mailbox_recv,
	// which fills *from_pid / *type and returns the payload length (or -1 if empty;
	// blocking != 0 waits for a message). Messages are opaque {from_pid,type,bytes};
	// `type` meaning is the user shell protocol. from_pid 0 == from the kernel.
	int  (*register_shell) (void);
	int  (*shell_request) (int type, const void *in, unsigned len);
	int  (*mailbox_send) (int target_pid, int type, const void *in, unsigned len);
	int  (*mailbox_recv) (int *from_pid, int *type, void *buf, unsigned cap, int blocking);

	// --- v36 additions (memory primitives) ---
	// memset/memcpy/memmove: user-side code (the EL0 table's, kern/el0.h: no system call;
	// a bad pointer is the app's own fault and kills it).
	// GCC may emit calls to these even in -ffreestanding code; user/kapi.h defines
	// weak memset/memcpy/memmove symbols that forward here.
	void *(*memset) (void *dst, int c, unsigned long n);
	void *(*memcpy) (void *dst, const void *src, unsigned long n);
	void *(*memmove) (void *dst, const void *src, unsigned long n);

	// --- v37 additions (TCP server side) ---
	// tcp_listen: bind + listen on a local port; returns a LISTENING handle >=0 (only
	// for tcp_accept / tcp_close), or <0 (-1 no net / bad port, -2 too many sockets,
	// -6 bind failed / port in use). tcp_accept: BLOCKS until a peer connects; returns
	// a connected handle (tcp_send/recv/close) and the peer's dotted IP in ip[], or <0.
	// Both handles are auto-closed if the owning process dies.
	int  (*tcp_listen) (unsigned port);
	int  (*tcp_accept) (int listen_sock, char *ip, unsigned cap);

	// --- v38 additions (remote screen: vncd) ---
	// screen_grab: composite the current screen (what the display shows, cursor
	// included) into dst = w*h 0x00RRGGBB pixels; w/h must be the screen size; 1 / 0,
	// or 2 = nothing changed since the previous grab into the same dst (left as is).
	// inject_pointer / inject_key: feed input through the same path as the USB mouse /
	// keyboard (buttons bit0 left, bit1 right, bit2 middle; wheel = signed notches;
	// keys = cooked string: chars, "\n" Enter, "\b" Backspace, VT100 escapes).
	int  (*screen_grab) (unsigned *dst, int w, int h);
	void (*inject_pointer) (int x, int y, unsigned buttons, int wheel);
	void (*inject_key) (const char *keys);

	// --- v39 additions (system menu bar) ---
	// set_menu: declare the calling app's menus on its window + the callback that
	// receives (sender 0, GUI_EVENT_MENU = 14, item id). Spec = '\n'-separated lines:
	//   "M<title>"                    start a menu
	//   "I<id>\t<label>\t<shortcut>"  an item (id >= 0; shortcut text e.g. "^O", may be empty)
	//   "-"                           a separator
	// get_menu: the ACTIVE app window's spec + title (the topmost window that is not
	// the menu bar, the desktop or borderless); returns a serial that changes when the
	// active window or its menu changes (0 = none). menu_command: send GUI_EVENT_MENU(id)
	// to the active window (id -1 = ask it to close, like its close box); 1 if delivered.
	int      (*set_menu) (const char *spec, gui_handler handler);
	unsigned (*get_menu) (char *buf, unsigned cap, char *title, unsigned title_cap);
	int      (*menu_command) (int id);

	// --- v40 additions (named IPC services, clipboard, opacity, session end) ---
	// ipc_register: become service `name` (1 ok / 0 held by another live process);
	// ipc_lookup: pid of a service or 0. Talk with mailbox_send / mailbox_recv
	// (payload <= 512 bytes). clipboard_set: store a typed blob (1 text, 2 file
	// paths '\n'-separated; <= 64 KB); clipboard_get: copy <= cap bytes, return the
	// full length (0 = empty) + type + a serial bumped on every set.
	// set_window_alpha: the caller's window opacity 0..255 (255 = opaque).
	// shutdown: unmount the SD card, then mode 1 = restart, 0 = halt.
	int  (*ipc_register) (const char *name);
	int  (*ipc_lookup) (const char *name);
	int  (*clipboard_set) (int type, const void *data, unsigned len);
	int  (*clipboard_get) (int *type, void *buf, unsigned cap, unsigned *serial);
	void (*set_window_alpha) (int alpha);
	void (*shutdown) (int mode);

	// --- v41 additions (full-screen apps) ---
	// fullscreen_begin: the caller takes the whole screen; returns a screen-sized
	// 0x00RRGGBB back buffer (+ w/h) mapped in the app; the compositor stops drawing
	// windows and all pointer (screen coords) / key input goes to the caller.
	// present_fb: show the back buffer (copy to the framebuffer) and yield.
	// fullscreen_end: give the screen back (automatic when the app exits).
	unsigned *(*fullscreen_begin) (int *w, int *h);
	void      (*present_fb) (void);
	void      (*fullscreen_end) (void);

	// --- v42 additions (drag & drop, keyboard modifiers) ---
	// drag_begin: start dragging (type 1 text / 2 file paths '\n'-separated, <= 4 KB)
	// from the caller's window while the left button is held; `label` rides on the
	// cursor. The window under the cursor gets GUI_EVENT_DRAG_OVER (16), the one under
	// the release GUI_EVENT_DROP (15) -- lValue = (flags << 32) | (x << 16) | y, client
	// coords, flags DND_F_COPY (Ctrl) / DND_F_LEAVE -- and the source GUI_EVENT_DRAG_DONE
	// (17): lValue = (flags << 32) | target pid, flags DND_F_COPY / DND_F_CANCEL (Esc) /
	// DND_F_DESKTOP (no window, or the desktop). All go to the pointer handler.
	// drag_data: the last payload (copies <= cap, returns the full length + type).
	// get_modifiers: MOD_CTRL 1 / MOD_SHIFT 2 / MOD_ALT 4 (held now);
	// inject_modifiers: set them (vncd).
	int      (*drag_begin) (int type, const void *data, unsigned len, const char *label);
	int      (*drag_data) (int *type, void *buf, unsigned cap);
	unsigned (*get_modifiers) (void);
	void     (*inject_modifiers) (unsigned mods);

	// --- v43 additions (network tools) ---
	// net_ping: one ICMP echo to host (name or dotted IP), waiting <= timeout_ms; returns
	// the round-trip time in microseconds, or -1 net down / -3 unresolved / -4 timeout /
	// -5 send failed; ip (if given) receives the resolved address. net_resolve: DNS name
	// -> dotted IP (1 / 0). net_info: netstat text -- "key value" lines (up, hostname, ip,
	// mask, gateway, dns, dhcp) then "tcp <h> listen|conn <port> <remote ip> <pid>".
	int (*net_ping) (const char *host, unsigned seq, unsigned timeout_ms, char *ip, unsigned cap);
	int (*net_resolve) (const char *host, char *ip, unsigned cap);
	int (*net_info) (char *buf, unsigned cap);

	// --- v44 additions (user-space file-system providers, kern/vfs.h) ---
	// vfs_register: serve every path starting with `prefix` ("FTP:") -- 1 / 0 (taken).
	// The file kapis on such paths become requests: vfs_next (req, blocking) takes the
	// next one (1 / 0 none; blocking = wait up to ~0.5 s first), vfs_req_data copies its payload (SAVE data) from offset,
	// vfs_reply (id, status, data, len) answers it and wakes the caller.
	int (*vfs_register) (const char *prefix);
	int (*vfs_next) (struct kapi_vfs_req *req, int blocking);
	int (*vfs_req_data) (unsigned id, void *buf, unsigned cap, unsigned offset);
	int (*vfs_reply) (unsigned id, int status, const void *data, unsigned len);

	// --- v45 additions (Wi-Fi) ---
	// wlan_scan: scan the air (~3 s, blocks the caller) and fill up to max access points,
	// strongest first, one per BSSID; returns how many (0 none / no Wi-Fi). While not
	// associated wpa_supplicant scans too: a scan then may delay its next attempt.
	int (*wlan_scan) (struct kapi_wlan_ap *out, int max);

	// --- v46 additions (sound: kern/sound.h, the 3.5 mm jack by PWM) ---
	// sound_acquire: become the owner of the audio output (started on first use): 1 ok,
	// 0 another process has it, -1 no audio. Only the owner can play; its release (or
	// exit) silences and frees the output. sound_start: voice 0..15 plays a note until
	// sound_stop -- frequency in milli-Hz (440 Hz = 440000), wave SOUND_*, volume 0..255.
	// sound_stop (voice) / (-1) all. sound_write: PCM, s16 stereo frames at SOUND_RATE,
	// mixed with the voices; non-blocking, returns the frames taken (0 = the ~0.5 s ring
	// is full: try again later). sound_status: 1 if the output runs; rate, free frames of
	// the ring, owner pid (0 = free). Calls from a non-owner return -1.
	int  (*sound_acquire) (void);
	void (*sound_release) (void);
	// (sound_start / sound_stop / sound_instrument: RETIRED 2026-10-05 -- the synthesizer left the
	// kernel for AudioKit, ak_fm_*; the slots stay and answer -1)
	int  (*sound_start) (int voice, unsigned millihz, int wave, int volume);
	int  (*sound_stop) (int voice);
	int  (*sound_write) (const short *frames, unsigned nframes);
	int  (*sound_status) (unsigned *rate, unsigned *free_frames, unsigned *owner_pid);

	// --- v47 additions (FM) ---
	// sound_instrument: give voice 0..15 an FM instrument; sound_start (voice, milliHz,
	// SOUND_FM, volume) then keys it on (its envelopes restart), sound_stop keys it off
	// (the release rate fades it). Owner only (-1 otherwise).
	int  (*sound_instrument) (int voice, const struct kapi_fm_instrument *ins);

	// --- v48 additions (held keys, for games: key events only say "pressed") ---
	// key_held: 1 while the key is held down AND the caller's window has the keyboard,
	// else 0. key = KEY_UP/DOWN/LEFT/RIGHT, KEY_ENTER, 27 (Esc), ' ', 'a'..'z' (the US
	// position of the key on a USB keyboard), '0'..'9'. inject_key_held: vncd's key down /
	// up (same codes).
	int  (*key_held) (int key);
	void (*inject_key_held) (int key, int down);

	// --- v49 additions ---
	// exec_as: like exec, the process (its window, list_windows, raise_app, kill) named
	// `name` instead of after the path. 1 = started.
	int  (*exec_as) (const char *path, const char *args, const char *name);

	// --- v50 additions (USB gamepads) ---
	// pad_state: pad 0..KAPI_PAD_MAX-1 (in the order they were plugged): 1 and *out
	// filled if a pad is there, else 0.
	int  (*pad_state) (int index, struct kapi_pad *out);

	// --- v51 additions (app cores) ---
	// core_acquire: reserve a free app core (2 or 3) for the caller; its number, or -1.
	// core_run: call fn (arg) there, on the stack whose top is given (16-byte aligned,
	// the caller's memory); 0, or -1 (not yours / still running / bad address). fn runs
	// in the caller's address space and must make NO kapi call and no allocation.
	// core_state: KAPI_CORE_IDLE (fn returned), _RUNNING, _FAULT (it faulted: stopped,
	// see kmsg) or _NOTYOURS. core_release: stops fn if it runs, frees the core (done
	// for the app on exit anyway). Stop fn cleanly with a flag it polls, then release.
	int  (*core_acquire) (void);
	int  (*core_run) (int core, void (*fn) (void *), void *arg, void *stack_top);
	int  (*core_state) (int core);
	void (*core_release) (int core);

	// --- v52 additions (the V3D GPU) ---
	// gpu_info: brings the GPU up on first use; its description in buf ("V3D 4.2 ...");
	// 1 = usable, 0 = not (buf says why). gpu_draw: n vertices (a triangle list, n a
	// multiple of 3, at most KAPI_GPU_MAX_VERTS), x y z in normalized device coordinates
	// (-1..1, y up; z -1 near .. 1 far, depth-tested: the nearest wins), colours
	// interpolated across each triangle; the picture (w x h, cleared to clear = 0xRRGGBB)
	// lands in pixels (0x00RRGGBB, stride = pixels per row). 0 ok, -1 no GPU, -2 bad
	// arguments, -3 the GPU did not finish (it is then left off).
	int  (*gpu_info) (char *buf, unsigned cap);
	int  (*gpu_draw) (const struct kapi_gpu_vertex *v, unsigned n, unsigned clear,
			  unsigned *pixels, int w, int h, int stride);

	// --- v53 additions (the GPU: textures, batches) ---
	// gpu_texture: handle < 0 makes a texture, else replaces the pixels of that one; w x h
	// (1..KAPI_GPU_MAX_TEXSIZE) pixels 0xAARRGGBB, stride in pixels. Returns the handle (>= 0)
	// or -1 no GPU, -2 bad arguments, -4 no memory / no free texture. pixels == 0 frees the
	// handle (0). The textures of a program are freed when it ends.
	// gpu_render: nb batches (KAPI_GPU_MAX_BATCHES at most) over nv vertices, one frame
	// into f; 0 ok, -1 no GPU, -2 bad arguments, -3 the GPU did not finish (left off).
	int  (*gpu_texture) (int handle, const unsigned *pixels, int w, int h, int stride);
	int  (*gpu_render) (const struct kapi_gpu_frame *f, const struct kapi_gpu_vertex3 *v, unsigned nv,
			    const struct kapi_gpu_batch *b, unsigned nb);

	// --- v55 additions ---
	// fullscreen_direct: after fullscreen_begin, the framebuffer the display scans out,
	// mapped in the caller (0x00RRGGBB, uncached: write it, do not read it back much):
	// what is written there is on the screen at once (no double buffering: tearing is
	// possible), and present_fb stops copying the back buffer (it only yields). *stride:
	// pixels a row. 0 if not possible (then keep the back buffer). fullscreen_end ends it.
	unsigned *(*fullscreen_direct) (int *w, int *h, int *stride);

	// --- v56 additions (the window-level remote desktop, rdpd) ---
	// win_list: the windows, bottom to top (at most max) -> how many. win_read: the pixels
	// (0x00RRGGBB) of a rectangle of window id's client area (part 0) or of its frame (part
	// 1 active, 2 inactive: ow x oh, the client area inside is not drawn there) into dst
	// (stride in pixels), clipped to it -> 0, -1 no such window / part. win_raise: to the front (it gets the keys);
	// win_close: asked to close (as its close box) -> 0 / -1.
	int (*win_list) (struct kapi_win_info *out, int max);
	int (*win_read) (unsigned id, int part, int x, int y, int w, int h, unsigned *dst, int stride);
	int (*win_raise) (unsigned id);
	int (*win_close) (unsigned id);

	// --- v57 additions ---
	// seek: the read position of a file opened with open() -> 0, -1 (not seekable: FTP:...).
	// A big file's cluster map is built at its first seek (FatFs fast seek): then any position
	// is reached without walking the FAT.
	int (*seek) (void *h, unsigned long long pos);

	// --- v58 additions ---
	// code_alloc: size bytes (rounded up to 64 KB pages) of zeroed memory the app may write
	// AND execute (a JIT's generated code) -> its address, 0 (the 768 MB arena is full / no
	// memory). After writing code: clean the D-cache and invalidate the I-cache over it
	// (DC CVAU, DSB ISH, IC IVAU, DSB ISH, ISB). Freed when the app exits.
	void *(*code_alloc) (unsigned long size);

	// --- v59 additions ---
	// fsize64: the size of a file opened with open(), whole (fsize and readdir's size stop at
	// 0xFFFFFFFF: a file over 4 GB on an exFAT partition).
	unsigned long long (*fsize64) (void *h);

	// --- v60 additions ---
	// sound_volume: the master volume, 0 (silent) .. 10 (full), and mute (1 / 0), for all the
	// sound (voices + stream); -1 keeps a value. -> the volume now | 0x100 if muted. Not kept
	// by the kernel across a reboot: the menu bar applies the saved one (SD:/etc/sound.ini).
	int (*sound_volume) (int volume, int mute);
	// wlan_reconnect: wpa_supplicant reads SD:/etc/wpa_supplicant.conf again and associates by
	// it (a network added / its password changed), then DHCP starts over. 0 asked (the joining
	// takes seconds: watch net_status), -1 no Wi-Fi running.
	int (*wlan_reconnect) (void);
	// --- v61 ---
	// gpu_program: handle < 0 makes a program from *p (the QPU code copied), else replaces that
	// one; p = 0 frees it -> the handle, -1 no GPU, -2 bad arguments, -4 no memory / no handle.
	// A program's handles are freed when it ends.
	int (*gpu_program) (int handle, const struct kapi_gpu_program *p);
	// gpu_render2: nb batches over nv vertices of `stride` floats (>= every program's inputs),
	// the uniforms uni[nuni] -> 0, -1 no GPU, -2 bad arguments, -3 the GPU did not finish.
	int (*gpu_render2) (const struct kapi_gpu_frame *f, const float *v, unsigned nv, unsigned stride,
			    const struct kapi_gpu_batch2 *b, unsigned nb, const unsigned *uni, unsigned nuni);
	// --- v62 ---
	// gpu_render3: as gpu_render2, each batch's vertices where they are in v[nfloats] (off,
	// stride), their x / y framed on the way -- x' = view[0] x + view[1] w, y' = view[2] y +
	// view[3] w (view 0: as they are) -> as gpu_render2.
	int (*gpu_render3) (const struct kapi_gpu_frame *f, const float *v, unsigned nfloats,
			    const struct kapi_gpu_batch3 *b, unsigned nb, const unsigned *uni, unsigned nuni,
			    const float *view);
	// --- v63 ---
	// gpu_vbuf: `bytes` of memory the GPU reads too (up to 8 a program, 64 MB each; freed when
	// it ends) -> its address, 0 none. gpu_render3 with v[nfloats] inside one draws the vertices
	// where they are: their x / y framed IN PLACE (draw them again with view 0), the triangles
	// that need clipping clipped into the buffer's end past nfloats (keep room there).
	void *(*gpu_vbuf) (unsigned bytes);
	// --- v64 ---
	// win_minimise: window id (0: the caller's) minimised -- not shown, no input -- until win_raise
	// or raise_app brings it back (KAPI_WIN_MINIMISED in win_list) -> 0, -1 no such window.
	int (*win_minimise) (unsigned id);
	// win_geometry: the caller's window and the work area into *out -> 0, -1 no window.
	int (*win_geometry) (struct kapi_win_geom *out);
	// resize_window2: as resize_window, but the canvas and the frame's copies grow past their first
	// size when needed (new memory, at the same addresses: their pixels are lost -- redraw them; the
	// frame: get_chrome again) -> the canvas, *stride its pixels a row; 0 (no memory: size kept).
	unsigned *(*resize_window2) (int w, int h, int *stride);
	// --- v65 ---
	// desk: the workspaces (virtual desktops). set >= 0 shows desk `set`, count > 0 sets how many
	// there are (1 .. KAPI_DESK_MAX; the windows of the desks dropped go to the last one); -1 / 0
	// keep them -> the current desk | the count << 8 | a counter bumped at every change << 16.
	int (*desk) (int set, int count);
	// win_desk: window id (0: the caller's) to desk n (-1: every desk; -2: only asked) -> its desk
	// (-1: every desk), -3 no such window. (A topmost / backmost window stays on every desk.)
	int (*win_desk) (unsigned id, int n);
	// --- v66 ---
	// screen_set: the screen's resolution now (w x h: 640 x 480 .. 2560 x 1600, w even), between two
	// frames; every window kept on the screen and sent GUI_EVENT_DISPLAY_RESIZE -> 0; -1 a size out
	// of bounds; -2 not now (a full-screen app, the debug console); -3 the firmware refused it (the
	// old size kept). Not kept across a reboot: cmdline.txt's width= / height= are.
	int (*screen_set) (int w, int h);
	// --- v67 --- threads (kern/thread.h). Timeouts in ms: 0 = only try, KAPI_WAIT_FOREVER.
	// thread_create: run fn (arg) in a new thread of this process, on a stack of stack_size bytes
	// (0: 256 KB; 16 KB .. 16 MB), named "<app>:<name>" (name 0: "<app>:<tid>") -> its tid (>= 2;
	// the main thread is 1); -1 no memory, -2 too many threads (32 besides the main one). Its
	// return value is its exit code. The process ends with its main thread (the others with it).
	int (*thread_create) (int (*fn) (void *), void *arg, unsigned stack_size, const char *name);
	// thread_exit: end the calling thread with that code (the main thread: the process, as exit).
	void (*thread_exit) (int code);
	// thread_join: wait for thread tid to end -> 0 (*code its exit code; its tid is then free),
	// -1 timeout, -2 no such thread (or joined already), -3 itself.
	int (*thread_join) (int tid, unsigned timeout_ms, int *code);
	// thread_self: the calling thread's tid (1: the main thread).
	int (*thread_self) (void);
	// mutex_create -> a handle (> 0), -1 (256 objects per process). mutex_lock -> 0, -1 timeout,
	// -2 bad handle (or closed while waiting). Recursive; released if its owner thread ends.
	// mutex_unlock -> 0, -1 not the owner, -2 bad handle.
	int (*mutex_create) (void);
	int (*mutex_lock) (int h, unsigned timeout_ms);
	int (*mutex_unlock) (int h);
	// event_create: manual_reset (1: stays set until event_reset; 0: event_wait takes it back, one
	// waiter at a time), initial state -> a handle. event_set / event_reset -> 0; event_wait -> 0
	// set, -1 timeout, -2 bad handle.
	int (*event_create) (int manual_reset, int initial);
	int (*event_set) (int h);
	int (*event_reset) (int h);
	int (*event_wait) (int h, unsigned timeout_ms);
	// barrier_create: for count threads -> a handle. barrier_wait: wait until count threads are
	// in -> 1 for the last one in, 0 for the others (the barrier is then ready again), -2.
	int (*barrier_create) (unsigned count);
	int (*barrier_wait) (int h);
	// sync_close: free a mutex / event / barrier -> 0, -2 (its waiters get -2).
	int (*sync_close) (int h);
	// post: queue fn (ctx, value) for this process's event pump (pump_events / pump_wait /
	// wait_for_exit run it, on the thread that pumps) -> 0, -1 the queue is full (256).
	int (*post) (void (*fn) (void *ctx, long value), void *ctx, long value);
	// pump_wait: sleep until a window event, a post or the close box (or the timeout), then
	// pump_events -> what was pending (0: the timeout).
	int (*pump_wait) (unsigned timeout_ms);
	// --- v68 --- low-latency sound (kern/sound.h), futex, thread priority, USB MIDI.
	// sound_config: for the sound owner: chunks of chunk_frames (64 .. 1024; 0 = 1024) and
	// `ahead` of them rendered ahead (1 .. 4; 0 = 4) -> the latency now in frames ((ahead + 1)
	// x chunk: ~116 ms by default, 256 x 2 = 768 frames ~17 ms), -1 not the owner. Back to the
	// defaults when the owner releases the output (or dies).
	int (*sound_config) (int chunk_frames, int ahead);
	// sound_map: for the sound owner: the mapped PCM ring (struct kapi_sound_ring, emptied the
	// first time), mixed from now on until the owner releases the output; 0 not the owner.
	struct kapi_sound_ring *(*sound_map) (void);
	// wait_word: sleep while *addr == expected (a 4-byte aligned word of this process: data,
	// heap, stack, a shared surface) -> 0 woken or the value differs, 1 timeout (0 ms: only
	// check), -1 a bad address. Waiters are keyed by the word's physical address: a surface's
	// word wakes across processes. The kernel also reads every sleeping word at each 10 ms tick
	// and wakes those that changed (an app core's writes need no wake_word). Spurious wakes are
	// possible: check the word again. wake_word: wake the sleepers on addr -> how many, -1.
	int (*wait_word) (volatile unsigned *addr, unsigned expected, unsigned timeout_ms);
	int (*wake_word) (volatile unsigned *addr);
	// thread_priority: tid (0 the caller, 1 the main thread, >= 2 a thread of this process) to
	// prio 0 normal / 1 "real time" (picked first when ready; a tick preempts an app for it),
	// -1 only asks -> the previous priority, -1 bad prio, -2 no such thread. A real-time thread
	// that uses up its time slice is normal again until it next sleeps / yields by itself.
	int (*thread_priority) (int tid, int prio);
	// midi_read: up to max USB MIDI events (struct kapi_midi_event), oldest first, taken out of
	// the kernel's queue (256 events; one queue for the system: one reader at a time) -> how
	// many (0: none; never waits), -1 bad arguments. midi_devices: MIDI devices attached now.
	int (*midi_read) (struct kapi_midi_event *ev, int max);
	int (*midi_devices) (void);
	// screen_native: the monitor's own resolution, from its EDID (the preferred timing) -> 1 and
	// *w / *h, 0 unknown (no monitor, no EDID, an analog adapter). (v69)
	int (*screen_native) (int *w, int *h);
	// set_timezone: the local time's offset from UTC, minutes (-720 .. 840), at once (the clock,
	// kapi_get_datetime); system.ini's timezone= sets it at boot -> 1 ok, 0 out of range. (v69)
	int (*set_timezone) (int minutes);
	// --- v70 ---
	// gpu_texture_rect: the pixels (0xAARRGGBB, stride in pixels) of the rectangle x, y, w x h of
	// texture `handle` (the caller's) replaced -- the rest kept, nothing re-uploaded -> 0, -1 no
	// GPU, -2 bad arguments (not the caller's handle, the rectangle not inside the texture).
	int (*gpu_texture_rect) (int handle, int x, int y, int w, int h, const unsigned *pixels, int stride);
	// --- v71 ---
	// vol_info: the volume of `path` ("SD:", "SD1:/x", "RAM:", a relative path: the current
	// folder's) -> 0 and *out filled, -1 no such volume (not mounted).
	int (*vol_info) (const char *path, struct kapi_vol_info *out);
	// --- v73 ---
	// The next event of the caller's window -> 1 (*ev filled), 0: none (or no window). The
	// handler is NOT called: the caller does (ev->handler (ev->sender, ev->event, ev->value)).
	int (*pop_event) (struct kapi_event *ev);
	// The modifiers get_modifiers reports while a key event's handler runs (ev->mods), until put
	// back; returns the previous setting (0xFFFFFFFF: the live state).
	unsigned (*event_mods) (unsigned mods);
	// The next call posted to the process (post) -> 1 (*p filled, not run), 0: none.
	int (*pop_post) (struct kapi_posted *p);
	// pump_wait without the pump: sleep until a window event, a post or the close box (at most
	// timeout_ms; 0: no sleep), -> how many are pending (-1: not a process).
	int (*pump_sleep) (unsigned timeout_ms);
	// --- v74 ---
	// proc_stats: the system-call statistics of process `pid` (0: the caller) -> 0 (*out
	// filled), -1 no such process (or a kernel task), -2 a bad pointer.
	int (*proc_stats) (int pid, struct kapi_syscall_stats *out);

	// --- v75 WP-MEM --- (slots 199..206; docs/POSIX-PLAN.md §3.1). Every v75 call: >= 0
	// success, -KAPI_Exxx failure; every one -KAPI_ENOSYS until its work package lands.
	// vm_map: a region of len bytes (rounded up to 64 KB) in the mmap arena, zero-filled, filled
	// on first touch (KAPI_MAP_POPULATE: now); addr a hint unless KAPI_MAP_FIXED -> the address,
	// -EINVAL (len 0, FIXED not aligned / outside the arena), -ENOMEM (no room, overcommit, > 4096
	// regions), -ENOTSUP (EXEC), -EEXIST (FIXED_NOREPLACE overlaps).
	long long (*vm_map) (unsigned long long addr, unsigned long long len, unsigned prot, unsigned flags);
	// vm_unmap: inside KAPI_VMK_ANON regions (splits them) -> 0 / -EINVAL.
	int (*vm_unmap) (unsigned long long addr, unsigned long long len);
	// vm_protect: ANON regions; present pages re-protected -> 0 / -EINVAL / -ENOMEM (split cap) /
	// -ENOTSUP (EXEC).
	int (*vm_protect) (unsigned long long addr, unsigned long long len, unsigned prot);
	// vm_advise: any lazy region: WILLNEED populates (-ENOMEM); DONTNEED / FREE drop the pages
	// (zero on the next touch; ANON and HEAP only); the others no-op -> 0 / -EINVAL.
	int (*vm_advise) (unsigned long long addr, unsigned long long len, int advice);
	// vm_query -> 0 the region holding addr, 1 the next region above it, -ENOMEM none above,
	// -EFAULT.
	int (*vm_query) (unsigned long long addr, struct kapi_vm_region *out);
	// vm_stats: pid 0 = self -> 0 / -ESRCH / -EFAULT.
	int (*vm_stats) (int pid, struct kapi_vm_stats *out);
	// thread_create_ex: a thread with its stack size, TLS (TPIDR_EL0), name, flags, priority
	// -> tid >= 2 / -EAGAIN (32 running) / -ENOMEM / -EINVAL / -EFAULT.
	int (*thread_create_ex) (const struct kapi_thread_attr *attr);
	// thread_info: tid 0 = self, 1 = main -> 0 / -ESRCH / -EFAULT.
	int (*thread_info) (int tid, struct kapi_thread_info *out);

	// --- v75 WP-FILE/PROC --- (slots 207..228; docs/POSIX-PLAN.md §3.2)
	// file_open: KAPI_O_* flags -> handle > 0 / -errno.
	long long (*file_open) (const char *path, unsigned flags, unsigned mode);
	// file_read / file_write: off -1 at the handle's offset (advanced; a write with APPEND: at the
	// end), else at off (pread / pwrite) -> bytes (read: 0 = the end) / -errno.
	long long (*file_read) (long long h, void *buf, unsigned long long len, long long off);
	long long (*file_write) (long long h, const void *buf, unsigned long long len, long long off);
	// file_seek: KAPI_SEEK_* -> the new offset / -EINVAL / -EBADF.
	long long (*file_seek) (long long h, long long off, int whence);
	// file_truncate: grows with zeros.
	int (*file_truncate) (long long h, long long size);
	int (*file_sync) (long long h);
	int (*file_stat) (long long h, struct kapi_stat *out);
	int (*file_close) (long long h);
	int (*path_stat) (const char *path, struct kapi_stat *out);
	// path_unlink: a file (-EISDIR on a directory); KAPI_UNLINK_DIR: a directory (-ENOTDIR /
	// -ENOTEMPTY).
	int (*path_unlink) (const char *path, unsigned flags);
	// path_mkdir -> 0 / -EEXIST / -ENOENT (no parent).
	int (*path_mkdir) (const char *path, unsigned mode);
	// path_rename: replaces `to`; -EXDEV across volumes.
	int (*path_rename) (const char *from, const char *to);
	int (*path_utime) (const char *path, long long mtime);
	// dir_read: an opendir handle -> 1 / 0 the end / -EBADF / -EFAULT.
	int (*dir_read) (void *dir, struct kapi_dirent2 *out);
	// stream_write_nb -> n (> 0) / -EAGAIN (full) / -EBADF.
	int (*stream_write_nb) (void *h, const void *buf, unsigned len);
	// spawn_ex: argv / envp blocks given -> a process handle / -errno.
	long long (*spawn_ex) (const struct kapi_spawn_attr *a);
	// proc_wait -> 1 ended (the handle closed unless KAPI_WAIT_KEEP) / 0 running (NOHANG) / -EBADF.
	int (*proc_wait) (void *proc, unsigned flags, struct kapi_proc_status *out);
	// get_argv / get_env: the block ("a\0b\0\0"), filled up to cap -> the block's size;
	// argv[0] = the path.
	int (*get_argv) (char *buf, unsigned cap);
	int (*get_env) (char *buf, unsigned cap);
	// getpid: which 0 the pid, 1 the parent's.
	int (*getpid) (int which);
	int (*clock_info) (struct kapi_clock_info *out);
	int (*sleep_us) (unsigned long long us);

	// --- v75 WP-NET --- (slots 229..241; docs/POSIX-PLAN.md §3.3)
	// sock_open: KAPI_SOCK_STREAM / _DGRAM, KAPI_SOCKF_NONBLOCK -> socket >= 0 / -EPROTONOSUPPORT /
	// -ENFILE (table full) / -ENETDOWN.
	int (*sock_open) (int type, unsigned flags);
	// sock_connect -> 0 / -EINPROGRESS / -EALREADY / -EISCONN / -ECONNREFUSED / -ETIMEDOUT /
	// -ENETUNREACH / -EBADF.
	int (*sock_connect) (int s, const struct kapi_sockaddr *to);
	// sock_bind -> 0 / -EADDRINUSE / -EINVAL.
	int (*sock_bind) (int s, const struct kapi_sockaddr *addr);
	// sock_listen: backlog clamped 1..32.
	int (*sock_listen) (int s, int backlog);
	// sock_accept: flags KAPI_SOCKF_NONBLOCK for the new socket -> socket / -EAGAIN.
	int (*sock_accept) (int s, struct kapi_sockaddr *peer, unsigned flags);
	// sock_send: KAPI_MSG_* -> bytes / -EAGAIN / -EPIPE / -ENOTCONN / -EDESTADDRREQ.
	long long (*sock_send) (int s, const void *buf, unsigned long long len, unsigned flags,
				const struct kapi_sockaddr *to);
	// sock_recv -> bytes, 0 = orderly end / -EAGAIN / -ECONNRESET / -ENOTCONN / -ETIMEDOUT.
	long long (*sock_recv) (int s, void *buf, unsigned long long len, unsigned flags,
				struct kapi_sockaddr *from);
	int (*sock_shutdown) (int s, int how);
	int (*sock_close) (int s);
	// sock_getopt / sock_setopt: KAPI_SO_*.
	int (*sock_getopt) (int s, int opt, int *value);
	int (*sock_setopt) (int s, int opt, int value);
	// sock_name: peer 0 the local address, 1 the remote one (-ENOTCONN).
	int (*sock_name) (int s, int peer, struct kapi_sockaddr *out);
	// poll: up to KAPI_POLL_MAX entries, timeout_ms -1 forever, 0 only check -> the ready count /
	// 0 / -EINVAL / -EFAULT.
	int (*poll) (struct kapi_pollfd *fds, unsigned n, int timeout_ms);

	// --- v76 WP-IPC --- (slots 242..252; docs/POSIX-PLAN.md §14)
	// sock_pair: KAPI_SOCK_STREAM / _SEQPACKET / _DGRAM, KAPI_SOCKF_NONBLOCK -> 0, sv[0] and sv[1]
	// two connected local sockets / -EPROTONOSUPPORT / -EMFILE / -ENOMEM / -EFAULT.
	int (*sock_pair) (int type, unsigned flags, int *sv);
	// sock_sendmsg: the iovecs gathered into one message (a datagram / packet whole, or stream
	// bytes) with m->nhandles handles (local sockets only, else -EOPNOTSUPP) -> bytes / -EAGAIN /
	// -EPIPE / -EMSGSIZE / -ENOBUFS / -EBADF (a handle) / -EINVAL / -EFAULT.
	long long (*sock_sendmsg) (int s, const struct kapi_msghdr *m, unsigned flags);
	// sock_recvmsg: into the iovecs; the handles carried added to the caller's table and written
	// to m->handles (m->nhandles, m->flags updated) -> bytes, 0 = the end / -EAGAIN / -EFAULT.
	long long (*sock_recvmsg) (int s, struct kapi_msghdr *m, unsigned flags);
	// shm_create: an anonymous object of size bytes (zero-filled; 0 allowed), KAPI_SHM_ALLOW_SEALING
	// -> a handle / -ENOMEM / -EMFILE.
	long long (*shm_create) (unsigned long long size, unsigned flags);
	// shm_open: a named object (KAPI_O_RDONLY / RDWR | CREAT | EXCL | TRUNC) -> a handle / -ENOENT /
	// -EEXIST / -EINVAL / -ENAMETOOLONG / -EACCES.
	long long (*shm_open) (const char *name, unsigned oflags, unsigned mode);
	int (*shm_unlink) (const char *name);			// -> 0 / -ENOENT
	// shm_ctl: KAPI_SHM_* (SET_SIZE: arg the size; ADD_SEALS: arg the seals) -> the value asked / 0 /
	// -EPERM (sealed) / -EBUSY (mapped: no shrink, no write seal) / -EINVAL / -EBADF.
	long long (*shm_ctl) (long long h, int op, unsigned long long arg);
	// shm_map: [off, off + len) of the object mapped MAP_SHARED (as vm_map: addr a hint unless
	// KAPI_MAP_FIXED / _NOREPLACE, KAPI_MAP_POPULATE; vm_unmap / vm_protect / vm_advise work on it)
	// -> the address / -EACCES (write on a read-only handle) / -EPERM (write-sealed) / -EINVAL /
	// -ENOMEM / -EBADF.
	long long (*shm_map) (long long h, unsigned long long addr, unsigned long long len, unsigned prot,
			      unsigned flags, unsigned long long off);
	// handle_close: a shm, local socket, file_open or stream handle closed -> 0 / -EBADF.
	int (*handle_close) (long long h);
	// spawn_ex2: spawn_ex, and n handles of the caller (KAPI_HK_*: duplicated, the caller keeps its
	// own) given to the child, which reads them with get_handles -> a process handle / -errno.
	long long (*spawn_ex2) (const struct kapi_spawn_attr *a, const struct kapi_handle_xfer *handles, unsigned n);
	// get_handles: the handles the spawner gave (h: in this process's table now; fd, kind, tag as
	// given), up to cap written -> how many there are (0: none).
	int (*get_handles) (struct kapi_handle_xfer *out, unsigned cap);

	// --- v77: program images (kern/image.h; proc/image.cpp, sys/kapi.cpp) ---
	// A path here is a program file's (relative: to the caller's working directory); its canonical
	// form is the image's key (lower case, the volume first: "sd:/apps/x.app/main").
	// image_preload: the program loaded ahead and kept in memory: returns at once, a kernel task
	// reads the file; from then on a run of that path maps the image without reading the card.
	// Kept already (or being loaded for it): 0, nothing done -> 0 / -ENOENT (no such file) /
	// -ENAMETOOLONG / -ENOMEM / -EFAULT. A load that fails later is in the kernel log.
	int (*image_preload) (const char *path);
	// image_unload: the path's image loses its pin and its name at once: no new process maps it;
	// its memory is freed when the last process running it ends -> 0 / -ENOENT (no image) / -EFAULT.
	int (*image_unload) (const char *path);
	// image_list: path 0: the live images, up to cap written -> how many there are. path: the
	// image a run of that path would map -> 1 (out[0] written if cap > 0) / 0 (none) / -EFAULT.
	int (*image_list) (const char *path, struct kapi_image_info *out, unsigned cap);

	// --- v79: what the kernel is (sys/kapi.cpp, buildstamp.cpp) ---
	// kernel_info: "key value" lines, one a line: name (Onyx), abi (KAPI_ABI_VERSION), built
	// (the date and time of the kernel's link), rev (the source's git revision, "+" when it had
	// changes), machine (aarch64), model (the board's name), ram (MB). Up to cap - 1 bytes
	// written and a NUL -> the text's whole length / -EFAULT. Keys may be added.
	int (*kernel_info) (char *buf, unsigned cap);

	// --- v80: the cores' load, the network's bytes by process (sys/kapi.cpp) ---
	// cpu_stats: every core's role, busy time and owner -> 0 / -EFAULT. The load between two
	// reads: (busy_us' - busy_us) / (now_us' - now_us).
	int (*cpu_stats) (struct kapi_cpu_stats *out);
	// net_stats: pid's bytes received and sent, its open sockets (pid 0: all) -> 0 / -EFAULT. A
	// process that used no socket: zeros.
	int (*net_stats) (int pid, struct kapi_net_stats *out);

	// --- v81: the pointer's shape (gui/window.cpp) ---
	// set_cursor: the shape shown while the pointer is over the caller's window's client area (or
	// while that window holds the pointer: a button down) -> the shape it had / -1 (no window, an
	// unknown shape). Kept until changed; the frame, the title bar and the other windows show
	// their own. An app sets it as the pointer moves (uikit: uk_cursor, from a widget's onMouse).
	int (*set_cursor) (int shape);

	// --- v82: a window resized by its frame (gui/window.cpp) ---
	// win_resizable: on != 0, the caller's window's edges and corners can be dragged; its client
	// area is never made smaller than min_w x min_h -> 0 / -1 (no window, a borderless or fixed
	// one). The kernel only shows the outline: at the release the window's pointer handler gets
	// GUI_EVENT_WINRESIZE, lValue = (x << 48) | (y << 32) | (client_w << 16) | client_h (x, y: the
	// frame's top left on the screen, 16 bits signed each), and the app resizes and moves itself.
	int (*win_resizable) (int on, int min_w, int min_h);

	// --- v83: shared libraries (kern/image.h; proc/image.cpp, kernel.cpp; docs/SHARED-LIBS-PLAN.md) ---
	// lib_open: the shared library `name` mapped into the caller -> its export table, or 0 with
	// *err (if not 0) = -KAPI_E*. name: a bare name ("uikit": SD:/lib/uikit.so) or a path (relative: to
	// the working directory). The library's file is read once for the whole system (the calling
	// task reads it, as a program's start), placed by the kernel, its data relocated once; every
	// process maps the same code at the same address and gets its own copy of the data. Mapped in
	// the caller already: the same table. It stays mapped until the process ends (no lib_close).
	// The table starts with `unsigned version, size; int (*init) (const void *imports);` -- the
	// caller calls init once (user/Runtime/lib.h's lib_bind does) -- and is append-only, as this one.
	// min_version: the table's version must be >= it, else -ENOTSUP. Other errors: -ENOENT (no
	// such file), -EINVAL (not a library of user/Runtime/lib.ld's shape; a relocation other than
	// R_AARCH64_RELATIVE), -ENOMEM (memory, or no room in the arena), -EMFILE (16 libraries in the
	// process), -EIO, -ENAMETOOLONG, -EFAULT.
	const void *(*lib_open) (const char *name, unsigned min_version, int *err);

	// --- v84: the sound's output (sys/sound.cpp) ---
	// sound_output: out = KAPI_SND_OUT_AUTO / _JACK / _USB / _HDMI: that output from now on (the
	// running sound switches at once; an output that is not there -- no USB device -- plays nothing
	// until it is); out = -1: nothing changed -> what plays now (KAPI_SND_OUT_NOW, 0: nothing yet or
	// no device), what is asked (KAPI_SND_OUT_ASKED) and the outputs present (KAPI_SND_OUT_HAS), or -1
	// (a bad value). Not kept across a restart by the kernel: the Sound applet writes SD:/etc/sound.ini
	// ("output = usb"), read when the sound first starts.
	int (*sound_output) (int out);

	// --- v85: the sound's mixer (sys/sound.cpp) ---
	// sound_clients: the programs that have a channel now -> how many (out: up to max of them; 0 / 0:
	// only the count). sound_client_volume: a channel's volume 0..100 (-1: kept) and mute 0 / 1 (-1:
	// kept), remembered for the program's name until the restart (the Sound applet writes
	// SD:/etc/mixer.ini: "media = 60", "media.mute = 1") -> volume | 0x100 if muted, -1: no such channel.
	int (*sound_clients) (struct kapi_sound_client *out, int max);
	int (*sound_client_volume) (unsigned pid, int volume, int mute);
};

// The v75 entries' slots (an entry's index in 8-byte words: its system-call number). The blocks
// are append-only: a slot never moves.
#define KAPI_CHECK_SLOT(name, n) \
	KAPI_STATIC_ASSERT (__builtin_offsetof (struct TKApiTable, name) == (n) * 8, "kapi " #name " is not slot " #n)
KAPI_CHECK_SLOT (proc_stats, 198);
KAPI_CHECK_SLOT (vm_map, 199);
KAPI_CHECK_SLOT (vm_unmap, 200);
KAPI_CHECK_SLOT (vm_protect, 201);
KAPI_CHECK_SLOT (vm_advise, 202);
KAPI_CHECK_SLOT (vm_query, 203);
KAPI_CHECK_SLOT (vm_stats, 204);
KAPI_CHECK_SLOT (thread_create_ex, 205);
KAPI_CHECK_SLOT (thread_info, 206);
KAPI_CHECK_SLOT (file_open, 207);
KAPI_CHECK_SLOT (file_read, 208);
KAPI_CHECK_SLOT (file_write, 209);
KAPI_CHECK_SLOT (file_seek, 210);
KAPI_CHECK_SLOT (file_truncate, 211);
KAPI_CHECK_SLOT (file_sync, 212);
KAPI_CHECK_SLOT (file_stat, 213);
KAPI_CHECK_SLOT (file_close, 214);
KAPI_CHECK_SLOT (path_stat, 215);
KAPI_CHECK_SLOT (path_unlink, 216);
KAPI_CHECK_SLOT (path_mkdir, 217);
KAPI_CHECK_SLOT (path_rename, 218);
KAPI_CHECK_SLOT (path_utime, 219);
KAPI_CHECK_SLOT (dir_read, 220);
KAPI_CHECK_SLOT (stream_write_nb, 221);
KAPI_CHECK_SLOT (spawn_ex, 222);
KAPI_CHECK_SLOT (proc_wait, 223);
KAPI_CHECK_SLOT (get_argv, 224);
KAPI_CHECK_SLOT (get_env, 225);
KAPI_CHECK_SLOT (getpid, 226);
KAPI_CHECK_SLOT (clock_info, 227);
KAPI_CHECK_SLOT (sleep_us, 228);
KAPI_CHECK_SLOT (sock_open, 229);
KAPI_CHECK_SLOT (sock_connect, 230);
KAPI_CHECK_SLOT (sock_bind, 231);
KAPI_CHECK_SLOT (sock_listen, 232);
KAPI_CHECK_SLOT (sock_accept, 233);
KAPI_CHECK_SLOT (sock_send, 234);
KAPI_CHECK_SLOT (sock_recv, 235);
KAPI_CHECK_SLOT (sock_shutdown, 236);
KAPI_CHECK_SLOT (sock_close, 237);
KAPI_CHECK_SLOT (sock_getopt, 238);
KAPI_CHECK_SLOT (sock_setopt, 239);
KAPI_CHECK_SLOT (sock_name, 240);
KAPI_CHECK_SLOT (poll, 241);
KAPI_CHECK_SLOT (sock_pair, 242);
KAPI_CHECK_SLOT (sock_sendmsg, 243);
KAPI_CHECK_SLOT (sock_recvmsg, 244);
KAPI_CHECK_SLOT (shm_create, 245);
KAPI_CHECK_SLOT (shm_open, 246);
KAPI_CHECK_SLOT (shm_unlink, 247);
KAPI_CHECK_SLOT (shm_ctl, 248);
KAPI_CHECK_SLOT (shm_map, 249);
KAPI_CHECK_SLOT (handle_close, 250);
KAPI_CHECK_SLOT (spawn_ex2, 251);
KAPI_CHECK_SLOT (get_handles, 252);
KAPI_CHECK_SLOT (image_preload, 253);
KAPI_CHECK_SLOT (image_unload, 254);
KAPI_CHECK_SLOT (image_list, 255);
KAPI_CHECK_SLOT (kernel_info, 256);
KAPI_CHECK_SLOT (cpu_stats, 257);
KAPI_CHECK_SLOT (net_stats, 258);
KAPI_CHECK_SLOT (set_cursor, 259);
KAPI_CHECK_SLOT (win_resizable, 260);
KAPI_CHECK_SLOT (lib_open, 261);
KAPI_CHECK_SLOT (sound_output, 262);
KAPI_CHECK_SLOT (sound_clients, 263);
KAPI_CHECK_SLOT (sound_client_volume, 264);

#ifdef __cplusplus
}
#endif

#endif // _kern_kapi_abi_h
