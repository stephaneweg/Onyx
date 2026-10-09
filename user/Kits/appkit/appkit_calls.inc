//
// appkit_calls.inc -- AppKit's functions: every kapi_* call of appkit.h, through the kernel's table.
//
// This is the ONLY code that reads the kernel's table (KT, at KAPI_TABLE_VA: kern/kapi_abi.h). It is
// compiled into AppKit (appkit.c, KAPI_IMPL: the functions, exported by name -- appkit.abi) and, for
// the tests of the table and the PC builds, inline into the program (appkit.h's end). When the kernel's
// table is restructured, the bodies here are adapted so that each name still does what the programs
// expect; AppKit is rebuilt, no program is. A name is never removed nor renamed: a call that is gone
// keeps a body that answers -KAPI_ENOSYS (or does it another way). (2026-10-08: the 46 window calls
// moved to UIKit's uk_win_*; their slots stay as retired placeholders, below -- appkit.abi says so.)
//
// One line a call: KAPI_CALL (result, name, (arguments), { body }) -- in appkit.h's order; what each
// call does is said there, at its declaration.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#define KT	((const struct TKApiTable *) KAPI_TABLE_VA)
#ifdef KAPI_IMPL
#define KAPI_CALL(ret, name, args, ...)	ret name args __VA_ARGS__
#else
#define KAPI_CALL(ret, name, args, ...)	static inline ret name args __VA_ARGS__
#endif
// (The window calls -- kapi_create_window, kapi_present, kapi_set_menu, kapi_win_list... -- were here until
// 2026-10-08, their bodies speaking to Elegant (appkit_ws.inc): they are UIKit's now, uk_win_* (uikit/win.h,
// its port uikit/port/port_desktop.cpp). AppKit keeps the kernel's side: kapi_ws_ctl, the pump, the full
// screen's primitives.)
// Their slots stay, retired (appkit.abi): a program built before 2026-10-08 that calls one gets 0 (a null
// canvas, nothing listed, false) or the call's old error value -- never another function. Not in appkit.h.
#ifdef KAPI_IMPL
KAPI_CALL (long, kapi_retired_16, (void), { return 0; })	// (was kapi_create_window)
KAPI_CALL (long, kapi_retired_17, (void), { return 0; })	// (was kapi_create_window_ex)
KAPI_CALL (long, kapi_retired_18, (void), { return 0; })	// (was kapi_cursor_pos)
KAPI_CALL (long, kapi_retired_19, (void), { return 1 << 8; })	// (was kapi_desk)
KAPI_CALL (long, kapi_retired_21, (void), { return 0; })	// (was kapi_drag_begin)
KAPI_CALL (long, kapi_retired_22, (void), { return 0; })	// (was kapi_drag_data)
KAPI_CALL (long, kapi_retired_23, (void), { return 0; })	// (was kapi_draw_text)
KAPI_CALL (long, kapi_retired_52, (void), { return 0; })	// (was kapi_get_chrome)
KAPI_CALL (long, kapi_retired_57, (void), { return 0; })	// (was kapi_get_menu)
KAPI_CALL (long, kapi_retired_61, (void), { return 1; })	// (was kapi_get_wheel_speed)
KAPI_CALL (long, kapi_retired_95, (void), { return 0; })	// (was kapi_list_windows)
KAPI_CALL (long, kapi_retired_103, (void), { return 0; })	// (was kapi_menu_command)
KAPI_CALL (long, kapi_retired_107, (void), { return 0; })	// (was kapi_move_window)
KAPI_CALL (long, kapi_retired_130, (void), { return 0; })	// (was kapi_present)
KAPI_CALL (long, kapi_retired_138, (void), { return 0; })	// (was kapi_raise_app)
KAPI_CALL (long, kapi_retired_147, (void), { return 0; })	// (was kapi_resize_window)
KAPI_CALL (long, kapi_retired_148, (void), { return 0; })	// (was kapi_resize_window2)
KAPI_CALL (long, kapi_retired_156, (void), { return 0; })	// (was kapi_set_click_handler)
KAPI_CALL (long, kapi_retired_157, (void), { return -1; })	// (was kapi_set_cursor)
KAPI_CALL (long, kapi_retired_158, (void), { return 0; })	// (was kapi_set_key_handler)
KAPI_CALL (long, kapi_retired_161, (void), { return 0; })	// (was kapi_set_menu)
KAPI_CALL (long, kapi_retired_162, (void), { return 0; })	// (was kapi_set_pointer_handler)
KAPI_CALL (long, kapi_retired_165, (void), { return 0; })	// (was kapi_set_wheel_speed)
KAPI_CALL (long, kapi_retired_166, (void), { return 0; })	// (was kapi_set_window_alpha)
KAPI_CALL (long, kapi_retired_237, (void), { return -1; })	// (was kapi_toggle_app)
KAPI_CALL (long, kapi_retired_253, (void), { return 0; })	// (was kapi_wallpaper_buffer)
KAPI_CALL (long, kapi_retired_254, (void), { return 0; })	// (was kapi_wallpaper_commit)
KAPI_CALL (long, kapi_retired_255, (void), { return 0; })	// (was kapi_wallpaper_generate)
KAPI_CALL (long, kapi_retired_256, (void), { return -1; })	// (was kapi_win_close)
KAPI_CALL (long, kapi_retired_257, (void), { return -3; })	// (was kapi_win_desk)
KAPI_CALL (long, kapi_retired_258, (void), { return -1; })	// (was kapi_win_geometry)
KAPI_CALL (long, kapi_retired_259, (void), { return 0; })	// (was kapi_win_list)
KAPI_CALL (long, kapi_retired_260, (void), { return -1; })	// (was kapi_win_minimise)
KAPI_CALL (long, kapi_retired_261, (void), { return -1; })	// (was kapi_win_raise)
KAPI_CALL (long, kapi_retired_262, (void), { return -1; })	// (was kapi_win_read)
KAPI_CALL (long, kapi_retired_263, (void), { return -1; })	// (was kapi_win_resizable)
KAPI_CALL (long, kapi_retired_299, (void), { return -1; })	// (was kapi_cursor_shown)
KAPI_CALL (long, kapi_retired_305, (void), { return 0; })	// (was kapi_win_destroy)
KAPI_CALL (long, kapi_retired_306, (void), { return -1; })	// (was kapi_win_new)
KAPI_CALL (long, kapi_retired_307, (void), { return -1; })	// (was kapi_win_select)
KAPI_CALL (long, kapi_retired_308, (void), { return 0; })	// (was kapi_tray_activate)
KAPI_CALL (long, kapi_retired_309, (void), { return 0; })	// (was kapi_tray_clear)
KAPI_CALL (long, kapi_retired_310, (void), { return 0; })	// (was kapi_tray_icon)
KAPI_CALL (long, kapi_retired_311, (void), { return 0; })	// (was kapi_tray_list)
KAPI_CALL (long, kapi_retired_312, (void), { return 0; })	// (was kapi_tray_set)
KAPI_CALL (long, kapi_retired_313, (void), { return -1; })	// (was kapi_win_move)
#endif

KAPI_CALL (unsigned, kapi_abi_version, (void), { return KT->version; })
KAPI_CALL (unsigned long long, kapi_table_slot, (unsigned slot), { return ((const unsigned long long *) KAPI_TABLE_VA)[slot]; })
KAPI_CALL (int, kapi_launch, (const char *n), { return KT->launch (n); })
KAPI_CALL (int, kapi_list_tasks, (char *b, unsigned s), { return KT->list_tasks (b, s); })
KAPI_CALL (int, kapi_kill, (const char *name), { return KT->kill (name); })
KAPI_CALL (int, kapi_list_procs, (char *b, unsigned s), { return KT->list_procs (b, s); })
KAPI_CALL (int, kapi_kill_pid, (int pid, int force), { return KT->kill_pid (pid, force); })
KAPI_CALL (int, kapi_proc_tree, (int pid, int op, int *out, unsigned cap),
	{ return KT->version >= 91 && KT->proc_tree ? KT->proc_tree (pid, op, out, cap) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_set_keymap, (const char *name), { return KT->set_keymap (name); })
KAPI_CALL (int, kapi_get_keymap, (char *b, unsigned s), { return KT->get_keymap (b, s); })
KAPI_CALL (int, kapi_exec, (const char *path, const char *args), { return KT->exec (path, args); })
KAPI_CALL (void, kapi_screen_size, (int *w, int *h), { KT->screen_size (w, h); })
KAPI_CALL (unsigned, kapi_get_ticks, (void), { return KT->get_ticks (); })
KAPI_CALL (void, kapi_msleep, (unsigned ms), { KT->msleep (ms); })
KAPI_CALL (void, kapi_yield, (void), { KT->yield (); })
KAPI_CALL (void, kapi_exit, (int s), { KT->exit (s); })
KAPI_CALL (void, kapi_pump_events, (void), { KT->pump_events (); })
KAPI_CALL (void, kapi_wait_for_exit, (void), { KT->wait_for_exit (); })
KAPI_CALL (int, kapi_should_exit, (void), { return KT->should_exit (); })
KAPI_CALL (void, kapi_draw_text_buf, (unsigned *dst, int dw, int dh, int x, int y, const char *s, unsigned c), { KT->draw_text_buf (dst, dw, dh, x, y, s, c); })
KAPI_CALL (int, kapi_font_width, (void), { return KT->font_width (); })
KAPI_CALL (int, kapi_font_height, (void), { return KT->font_height (); })
KAPI_CALL (int, kapi_meminfo, (unsigned long *total_kb, unsigned long *free_kb, unsigned long *app_kb, unsigned *page_kb), { return KT->meminfo (total_kb, free_kb, app_kb, page_kb); })
KAPI_CALL (int, kapi_ram_detail, (unsigned long *detected_kb, unsigned long *apppool_kb, unsigned long *apppool_free_kb, unsigned long *above4g_kb, unsigned *nsegments), { return KT->ram_detail (detected_kb, apppool_kb, apppool_free_kb, above4g_kb, nsegments); })
KAPI_CALL (void *, kapi_sbrk, (long inc), { return KT->sbrk (inc); })
KAPI_CALL (int, kapi_list_apps, (char *b, unsigned s), { return KT->list_apps (b, s); })
KAPI_CALL (int, kapi_get_datetime, (int *y, int *mo, int *d, int *h, int *mi, int *se), { return KT->get_datetime (y, mo, d, h, mi, se); })
KAPI_CALL (int, kapi_app_dir, (char *b, unsigned s), { return KT->app_dir (b, s); })
KAPI_CALL (int, kapi_write, (int fd, const void *b, unsigned n), { return KT->write (fd, b, n); })
KAPI_CALL (void *, kapi_open, (const char *p), { return KT->open (p); })
KAPI_CALL (int, kapi_read, (void *h, void *b, unsigned n), { return KT->read (h, b, n); })
KAPI_CALL (unsigned, kapi_fsize, (void *h), { return KT->fsize (h); })
KAPI_CALL (void, kapi_close, (void *h), { KT->close (h); })
KAPI_CALL (int, kapi_save_file, (const char *p, const void *b, unsigned n), { return KT->save_file (p, b, n); })
KAPI_CALL (int, kapi_chdir, (const char *p), { return KT->chdir (p); })
KAPI_CALL (int, kapi_getcwd, (char *b, unsigned n), { return KT->getcwd (b, n); })
KAPI_CALL (void *, kapi_opendir, (const char *p), { return KT->opendir (p); })
KAPI_CALL (int, kapi_readdir, (void *d, struct kapi_dirent *e), { return KT->readdir (d, e); })
KAPI_CALL (void, kapi_closedir, (void *d), { KT->closedir (d); })
KAPI_CALL (int, kapi_mkdir, (const char *p), { return KT->mkdir (p); })
KAPI_CALL (int, kapi_remove, (const char *p), { return KT->remove (p); })
KAPI_CALL (int, kapi_rename, (const char *from, const char *to), { return KT->rename (from, to); })
KAPI_CALL (void *, kapi_pipe, (void), { return KT->pipe (); })
KAPI_CALL (void *, kapi_file_in, (const char *p), { return KT->file_in (p); })
KAPI_CALL (void *, kapi_file_out, (const char *p, int append), { return KT->file_out (p, append); })
KAPI_CALL (int, kapi_stream_read, (void *h, void *b, unsigned n), { return KT->stream_read (h, b, n); })
KAPI_CALL (int, kapi_stream_read_nb, (void *h, void *b, unsigned n), { return KT->stream_read_nb (h, b, n); })
KAPI_CALL (int, kapi_stream_write, (void *h, const void *b, unsigned n), { return KT->stream_write (h, b, n); })
KAPI_CALL (void, kapi_stream_close, (void *h), { KT->stream_close (h); })
KAPI_CALL (void, kapi_stream_eof, (void *h), { KT->stream_eof (h); })
KAPI_CALL (int, kapi_proc_done, (void *proc), { return KT->proc_done (proc); })
KAPI_CALL (int, kapi_stdin_read, (void *b, unsigned n), { return KT->stdin_read (b, n); })
KAPI_CALL (int, kapi_stdout_write, (const void *b, unsigned n), { return KT->stdout_write (b, n); })
KAPI_CALL (void *, kapi_spawn, (const char *path, const char *args, void *in, void *out), { return KT->spawn (path, args, in, out); })
KAPI_CALL (int, kapi_wait, (void *proc), { return KT->wait (proc); })
KAPI_CALL (int, kapi_get_args, (char *b, unsigned n), { return KT->get_args (b, n); })
KAPI_CALL (void *, kapi_stdin, (void), { return KT->stdin_stream (); })
KAPI_CALL (void *, kapi_stdout, (void), { return KT->stdout_stream (); })
KAPI_CALL (int, kapi_klog_read, (int *sev, char *src, unsigned sc, char *msg, unsigned mc), { return KT->klog_read (sev, src, sc, msg, mc); })
KAPI_CALL (int, kapi_set_verbose, (int on), { return KT->set_verbose (on); })
KAPI_CALL (int, kapi_get_verbose, (void), { return KT->get_verbose (); })
KAPI_CALL (int, kapi_net_status, (char *ip, unsigned cap), { return KT->net_status (ip, cap); })
KAPI_CALL (int, kapi_tcp_connect, (const char *host, unsigned port), { return KT->tcp_connect (host, port); })
KAPI_CALL (int, kapi_tcp_send, (int sock, const void *buf, unsigned len), { return KT->tcp_send (sock, buf, len); })
KAPI_CALL (int, kapi_tcp_recv, (int sock, void *buf, unsigned len), { return KT->tcp_recv (sock, buf, len); })
KAPI_CALL (void, kapi_tcp_close, (int sock), { KT->tcp_close (sock); })
KAPI_CALL (int, kapi_tcp_listen, (unsigned port), { return KT->tcp_listen (port); })
KAPI_CALL (int, kapi_tcp_accept, (int listen_sock, char *ip, unsigned cap), { return KT->tcp_accept (listen_sock, ip, cap); })
KAPI_CALL (int, kapi_screen_grab, (unsigned *dst, int w, int h), { return KT->screen_grab (dst, w, h); })
KAPI_CALL (void, kapi_inject_pointer, (int x, int y, unsigned buttons, int wheel), { KT->inject_pointer (x, y, buttons, wheel); })
KAPI_CALL (void, kapi_inject_key, (const char *keys), { KT->inject_key (keys); })
KAPI_CALL (int, kapi_ipc_register, (const char *name), { return KT->ipc_register (name); })
KAPI_CALL (int, kapi_ipc_lookup, (const char *name), { return KT->ipc_lookup (name); })
KAPI_CALL (int, kapi_clipboard_set, (int type, const void *d, unsigned n), { return KT->clipboard_set (type, d, n); })
KAPI_CALL (int, kapi_clipboard_get, (int *type, void *b, unsigned cap, unsigned *serial), { return KT->clipboard_get (type, b, cap, serial); })
KAPI_CALL (void, kapi_shutdown, (int mode), { KT->shutdown (mode); })
KAPI_CALL (unsigned *, kapi_fullscreen_begin, (int *w, int *h), { return KT->fullscreen_begin (w, h); })
KAPI_CALL (void, kapi_present_fb, (void), { KT->present_fb (); })
KAPI_CALL (void, kapi_fullscreen_end, (void), { KT->fullscreen_end (); })
KAPI_CALL (unsigned, kapi_get_modifiers, (void), { return KT->get_modifiers (); })
KAPI_CALL (void, kapi_inject_modifiers, (unsigned mods), { KT->inject_modifiers (mods); })
KAPI_CALL (int, kapi_net_ping, (const char *host, unsigned seq, unsigned timeout_ms, char *ip, unsigned cap), { return KT->net_ping (host, seq, timeout_ms, ip, cap); })
KAPI_CALL (int, kapi_net_resolve, (const char *host, char *ip, unsigned cap), { return KT->net_resolve (host, ip, cap); })
KAPI_CALL (int, kapi_net_info, (char *buf, unsigned cap), { return KT->net_info (buf, cap); })
KAPI_CALL (int, kapi_vfs_register, (const char *prefix), { return KT->vfs_register (prefix); })
KAPI_CALL (int, kapi_vfs_next, (struct kapi_vfs_req *req, int blocking), { return KT->vfs_next (req, blocking); })
KAPI_CALL (int, kapi_vfs_req_data, (unsigned id, void *buf, unsigned cap, unsigned offset), { return KT->vfs_req_data (id, buf, cap, offset); })
KAPI_CALL (int, kapi_vfs_reply, (unsigned id, int status, const void *data, unsigned len), { return KT->vfs_reply (id, status, data, len); })
KAPI_CALL (int, kapi_wlan_scan, (struct kapi_wlan_ap *out, int max), { return KT->wlan_scan (out, max); })
KAPI_CALL (int, kapi_sound_acquire, (void), { return KT->sound_acquire (); })
KAPI_CALL (void, kapi_sound_release, (void), { KT->sound_release (); })
KAPI_CALL (int, kapi_sound_start, (int voice, unsigned millihz, int wave, int volume), { return KT->sound_start (voice, millihz, wave, volume); })
KAPI_CALL (int, kapi_sound_stop, (int voice), { return KT->sound_stop (voice); })
KAPI_CALL (int, kapi_sound_write, (const short *frames, unsigned n), { return KT->sound_write (frames, n); })
KAPI_CALL (int, kapi_sound_status, (unsigned *rate, unsigned *free_frames, unsigned *owner), { return KT->sound_status (rate, free_frames, owner); })
KAPI_CALL (int, kapi_sound_instrument, (int voice, const struct kapi_fm_instrument *ins), { return KT->sound_instrument (voice, ins); })
KAPI_CALL (int, kapi_key_held, (int key), { return KT->version >= 48 ? KT->key_held (key) : 0; })
KAPI_CALL (void, kapi_inject_key_held, (int key, int down), { if (KT->version >= 48) KT->inject_key_held (key, down); })
KAPI_CALL (int, kapi_exec_as, (const char *path, const char *args, const char *name), { return KT->version >= 49 ? KT->exec_as (path, args, name) : KT->exec (path, args); })
KAPI_CALL (int, kapi_pad_state, (int index, struct kapi_pad *out), { return KT->version >= 50 ? KT->pad_state (index, out) : 0; })
KAPI_CALL (int, kapi_core_acquire, (void), { return KT->version >= 51 ? KT->core_acquire () : -1; })
KAPI_CALL (int, kapi_core_run, (int core, void (*fn) (void *), void *arg, void *stack_top), { return KT->version >= 51 ? KT->core_run (core, fn, arg, stack_top) : -1; })
KAPI_CALL (int, kapi_core_state, (int core), { return KT->version >= 51 ? KT->core_state (core) : KAPI_CORE_NOTYOURS; })
KAPI_CALL (void, kapi_core_release, (int core), { if (KT->version >= 51) KT->core_release (core); })
KAPI_CALL (int, kapi_gpu_info, (char *buf, unsigned cap), { if (KT->version >= 52) return KT->gpu_info (buf, cap); if (buf && cap) buf[0] = 0; return 0; })
KAPI_CALL (int, kapi_gpu_draw, (const struct kapi_gpu_vertex *v, unsigned n, unsigned clear, unsigned *pixels, int w, int h, int stride),
	{ return KT->version >= 52 ? KT->gpu_draw (v, n, clear, pixels, w, h, stride) : -1; })
KAPI_CALL (int, kapi_gpu_texture, (int handle, const unsigned *pixels, int w, int h, int stride),
	{ return KT->version >= 53 ? KT->gpu_texture (handle, pixels, w, h, stride) : -1; })
KAPI_CALL (int, kapi_gpu_render, (const struct kapi_gpu_frame *f, const struct kapi_gpu_vertex3 *v, unsigned nv,
				    const struct kapi_gpu_batch *b, unsigned nb),
	{ return KT->version >= 53 ? KT->gpu_render (f, v, nv, b, nb) : -1; })
KAPI_CALL (unsigned *, kapi_fullscreen_direct, (int *w, int *h, int *stride),
	{ return KT->version >= 55 ? KT->fullscreen_direct (w, h, stride) : 0; })
KAPI_CALL (int, kapi_seek, (void *h, unsigned long long pos), { return KT->version >= 57 ? KT->seek (h, pos) : -1; })
KAPI_CALL (void *, kapi_code_alloc, (unsigned long size), { return KT->version >= 58 ? KT->code_alloc (size) : 0; })
KAPI_CALL (unsigned long long, kapi_fsize64, (void *h), { return KT->version >= 59 ? KT->fsize64 (h) : KT->fsize (h); })
KAPI_CALL (int, kapi_vol_info, (const char *path, struct kapi_vol_info *out), { return KT->version >= 71 && KT->vol_info ? KT->vol_info (path, out) : -1; })
KAPI_CALL (int, kapi_pop_event, (struct kapi_event *ev), { return KT->version >= 73 ? KT->pop_event (ev) : 0; })
KAPI_CALL (unsigned, kapi_event_mods, (unsigned mods), { return KT->version >= 73 ? KT->event_mods (mods) : 0xFFFFFFFFu; })
KAPI_CALL (int, kapi_pop_post, (struct kapi_posted *p), { return KT->version >= 73 ? KT->pop_post (p) : 0; })
KAPI_CALL (int, kapi_pump_sleep, (unsigned timeout_ms), { return KT->version >= 73 ? KT->pump_sleep (timeout_ms) : 0; })
KAPI_CALL (int, kapi_proc_stats, (int pid, struct kapi_syscall_stats *out), { return KT->version >= 74 && KT->proc_stats ? KT->proc_stats (pid, out) : -1; })
KAPI_CALL (long long, kapi_vm_map, (unsigned long long addr, unsigned long long len, unsigned prot, unsigned flags),
	{ return KT->version >= 75 && KT->vm_map ? KT->vm_map (addr, len, prot, flags) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_vm_unmap, (unsigned long long addr, unsigned long long len),
	{ return KT->version >= 75 && KT->vm_unmap ? KT->vm_unmap (addr, len) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_vm_protect, (unsigned long long addr, unsigned long long len, unsigned prot),
	{ return KT->version >= 75 && KT->vm_protect ? KT->vm_protect (addr, len, prot) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_vm_advise, (unsigned long long addr, unsigned long long len, int advice),
	{ return KT->version >= 75 && KT->vm_advise ? KT->vm_advise (addr, len, advice) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_vm_query, (unsigned long long addr, struct kapi_vm_region *out),
	{ return KT->version >= 75 && KT->vm_query ? KT->vm_query (addr, out) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_vm_stats, (int pid, struct kapi_vm_stats *out),
	{ return KT->version >= 75 && KT->vm_stats ? KT->vm_stats (pid, out) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_thread_create_ex, (const struct kapi_thread_attr *attr),
	{ return KT->version >= 75 && KT->thread_create_ex ? KT->thread_create_ex (attr) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_thread_info, (int tid, struct kapi_thread_info *out),
	{ return KT->version >= 75 && KT->thread_info ? KT->thread_info (tid, out) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_file_open, (const char *path, unsigned flags, unsigned mode),
	{ return KT->version >= 75 && KT->file_open ? KT->file_open (path, flags, mode) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_file_read, (long long h, void *buf, unsigned long long len, long long off),
	{ return KT->version >= 75 && KT->file_read ? KT->file_read (h, buf, len, off) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_file_write, (long long h, const void *buf, unsigned long long len, long long off),
	{ return KT->version >= 75 && KT->file_write ? KT->file_write (h, buf, len, off) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_file_seek, (long long h, long long off, int whence),
	{ return KT->version >= 75 && KT->file_seek ? KT->file_seek (h, off, whence) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_file_truncate, (long long h, long long size),
	{ return KT->version >= 75 && KT->file_truncate ? KT->file_truncate (h, size) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_file_sync, (long long h),
	{ return KT->version >= 75 && KT->file_sync ? KT->file_sync (h) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_file_stat, (long long h, struct kapi_stat *out),
	{ return KT->version >= 75 && KT->file_stat ? KT->file_stat (h, out) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_file_close, (long long h),
	{ return KT->version >= 75 && KT->file_close ? KT->file_close (h) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_path_stat, (const char *path, struct kapi_stat *out),
	{ return KT->version >= 75 && KT->path_stat ? KT->path_stat (path, out) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_path_unlink, (const char *path, unsigned flags),
	{ return KT->version >= 75 && KT->path_unlink ? KT->path_unlink (path, flags) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_path_mkdir, (const char *path, unsigned mode),
	{ return KT->version >= 75 && KT->path_mkdir ? KT->path_mkdir (path, mode) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_path_rename, (const char *from, const char *to),
	{ return KT->version >= 75 && KT->path_rename ? KT->path_rename (from, to) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_path_utime, (const char *path, long long mtime),
	{ return KT->version >= 75 && KT->path_utime ? KT->path_utime (path, mtime) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_dir_read, (void *dir, struct kapi_dirent2 *out),
	{ return KT->version >= 75 && KT->dir_read ? KT->dir_read (dir, out) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_stream_write_nb, (void *h, const void *buf, unsigned len),
	{ return KT->version >= 75 && KT->stream_write_nb ? KT->stream_write_nb (h, buf, len) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_spawn_ex, (const struct kapi_spawn_attr *a),
	{ return KT->version >= 75 && KT->spawn_ex ? KT->spawn_ex (a) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_proc_wait, (void *proc, unsigned flags, struct kapi_proc_status *out),
	{ return KT->version >= 75 && KT->proc_wait ? KT->proc_wait (proc, flags, out) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_get_argv, (char *buf, unsigned cap),
	{ return KT->version >= 75 && KT->get_argv ? KT->get_argv (buf, cap) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_get_env, (char *buf, unsigned cap),
	{ return KT->version >= 75 && KT->get_env ? KT->get_env (buf, cap) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_getpid, (int which),
	{ return KT->version >= 75 && KT->getpid ? KT->getpid (which) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_clock_info, (struct kapi_clock_info *out),
	{ return KT->version >= 75 && KT->clock_info ? KT->clock_info (out) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sleep_us, (unsigned long long us),
	{ return KT->version >= 75 && KT->sleep_us ? KT->sleep_us (us) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sock_open, (int type, unsigned flags),
	{ return KT->version >= 75 && KT->sock_open ? KT->sock_open (type, flags) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sock_connect, (int s, const struct kapi_sockaddr *to),
	{ return KT->version >= 75 && KT->sock_connect ? KT->sock_connect (s, to) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sock_bind, (int s, const struct kapi_sockaddr *addr),
	{ return KT->version >= 75 && KT->sock_bind ? KT->sock_bind (s, addr) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sock_listen, (int s, int backlog),
	{ return KT->version >= 75 && KT->sock_listen ? KT->sock_listen (s, backlog) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sock_accept, (int s, struct kapi_sockaddr *peer, unsigned flags),
	{ return KT->version >= 75 && KT->sock_accept ? KT->sock_accept (s, peer, flags) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_sock_send, (int s, const void *buf, unsigned long long len, unsigned flags, const struct kapi_sockaddr *to),
	{ return KT->version >= 75 && KT->sock_send ? KT->sock_send (s, buf, len, flags, to) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_sock_recv, (int s, void *buf, unsigned long long len, unsigned flags, struct kapi_sockaddr *from),
	{ return KT->version >= 75 && KT->sock_recv ? KT->sock_recv (s, buf, len, flags, from) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sock_shutdown, (int s, int how),
	{ return KT->version >= 75 && KT->sock_shutdown ? KT->sock_shutdown (s, how) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sock_close, (int s),
	{ return KT->version >= 75 && KT->sock_close ? KT->sock_close (s) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sock_getopt, (int s, int opt, int *value),
	{ return KT->version >= 75 && KT->sock_getopt ? KT->sock_getopt (s, opt, value) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sock_setopt, (int s, int opt, int value),
	{ return KT->version >= 75 && KT->sock_setopt ? KT->sock_setopt (s, opt, value) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sock_name, (int s, int peer, struct kapi_sockaddr *out),
	{ return KT->version >= 75 && KT->sock_name ? KT->sock_name (s, peer, out) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_poll, (struct kapi_pollfd *fds, unsigned n, int timeout_ms),
	{ return KT->version >= 75 && KT->poll ? KT->poll (fds, n, timeout_ms) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sock_pair, (int type, unsigned flags, int *sv),
	{ return KT->version >= 76 && KT->sock_pair ? KT->sock_pair (type, flags, sv) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_sock_sendmsg, (int s, const struct kapi_msghdr *m, unsigned flags),
	{ return KT->version >= 76 && KT->sock_sendmsg ? KT->sock_sendmsg (s, m, flags) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_sock_recvmsg, (int s, struct kapi_msghdr *m, unsigned flags),
	{ return KT->version >= 76 && KT->sock_recvmsg ? KT->sock_recvmsg (s, m, flags) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_shm_create, (unsigned long long size, unsigned flags),
	{ return KT->version >= 76 && KT->shm_create ? KT->shm_create (size, flags) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_shm_open, (const char *name, unsigned oflags, unsigned mode),
	{ return KT->version >= 76 && KT->shm_open ? KT->shm_open (name, oflags, mode) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_shm_unlink, (const char *name),
	{ return KT->version >= 76 && KT->shm_unlink ? KT->shm_unlink (name) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_shm_ctl, (long long h, int op, unsigned long long arg),
	{ return KT->version >= 76 && KT->shm_ctl ? KT->shm_ctl (h, op, arg) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_shm_map, (long long h, unsigned long long addr, unsigned long long len, unsigned prot,
				      unsigned flags, unsigned long long off),
	{ return KT->version >= 76 && KT->shm_map ? KT->shm_map (h, addr, len, prot, flags, off) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_handle_close, (long long h),
	{ return KT->version >= 76 && KT->handle_close ? KT->handle_close (h) : -KAPI_ENOSYS; })
KAPI_CALL (long long, kapi_spawn_ex2, (const struct kapi_spawn_attr *a, const struct kapi_handle_xfer *handles, unsigned n),
	{ return KT->version >= 76 && KT->spawn_ex2 ? KT->spawn_ex2 (a, handles, n) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_get_handles, (struct kapi_handle_xfer *out, unsigned cap),
	{ return KT->version >= 76 && KT->get_handles ? KT->get_handles (out, cap) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_image_preload, (const char *path),
	{ return KT->version >= 77 && KT->image_preload ? KT->image_preload (path) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_image_unload, (const char *path),
	{ return KT->version >= 77 && KT->image_unload ? KT->image_unload (path) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_image_list, (const char *path, struct kapi_image_info *out, unsigned cap),
	{ return KT->version >= 77 && KT->image_list ? KT->image_list (path, out, cap) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_kernel_info, (char *buf, unsigned cap),
	{ if (KT->version >= 79 && KT->kernel_info) return KT->kernel_info (buf, cap); if (buf && cap) buf[0] = 0; return -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_cpu_stats, (struct kapi_cpu_stats *out),
	{
	if (KT->version >= 80 && KT->cpu_stats) return KT->cpu_stats (out);
	if (out) __builtin_memset (out, 0, sizeof *out);
	return -KAPI_ENOSYS;
})
KAPI_CALL (int, kapi_net_stats, (int pid, struct kapi_net_stats *out),
	{
	if (KT->version >= 80 && KT->net_stats) return KT->net_stats (pid, out);
	if (out) __builtin_memset (out, 0, sizeof *out);
	return -KAPI_ENOSYS;
})
KAPI_CALL (const void *, kapi_lib_open, (const char *name, unsigned min_version, int *err),
	{
	if (KT->version >= 83 && KT->lib_open) return KT->lib_open (name, min_version, err);
	if (err) *err = -KAPI_ENOSYS;
	return 0;
})
KAPI_CALL (const void *, kapi_lib_open_as, (const char *path, const char *alias, unsigned min_version, int *err),
	{
	if (KT->version >= 97 && KT->lib_open_as) return KT->lib_open_as (path, alias, min_version, err);
	if (err) *err = -KAPI_ENOSYS;
	return 0;
})
KAPI_CALL (int, kapi_sound_output, (int out),
	{ return KT->version >= 84 && KT->sound_output ? KT->sound_output (out) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sound_clients, (struct kapi_sound_client *out, int max),
	{ return KT->version >= 85 && KT->sound_clients ? KT->sound_clients (out, max) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_sound_client_volume, (unsigned pid, int volume, int mute),
	{ return KT->version >= 85 && KT->sound_client_volume ? KT->sound_client_volume (pid, volume, mute) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_is_protected, (void),
	{ return KT->version >= 73 && (unsigned long long) KT->memcpy >= KAPI_TABLE_VA && (unsigned long long) KT->memcpy < KAPI_TABLE_VA + 0x20000; })
KAPI_CALL (int, kapi_sound_volume, (int volume, int mute), { return KT->version >= 60 ? KT->sound_volume (volume, mute) : 10; })
KAPI_CALL (int, kapi_wlan_reconnect, (void), { return KT->version >= 60 ? KT->wlan_reconnect () : -1; })
KAPI_CALL (int, kapi_gpu_program, (int handle, const struct kapi_gpu_program *p),
	{ return KT->version >= 61 ? KT->gpu_program (handle, p) : -1; })
KAPI_CALL (int, kapi_gpu_render2, (const struct kapi_gpu_frame *f, const float *v, unsigned nv, unsigned stride,
				    const struct kapi_gpu_batch2 *b, unsigned nb, const unsigned *uni, unsigned nuni),
	{ return KT->version >= 61 ? KT->gpu_render2 (f, v, nv, stride, b, nb, uni, nuni) : -1; })
KAPI_CALL (int, kapi_gpu_render3, (const struct kapi_gpu_frame *f, const float *v, unsigned nfloats,
				    const struct kapi_gpu_batch3 *b, unsigned nb, const unsigned *uni, unsigned nuni, const float *view),
	{ return KT->version >= 62 ? KT->gpu_render3 (f, v, nfloats, b, nb, uni, nuni, view) : -1; })
KAPI_CALL (void *, kapi_gpu_vbuf, (unsigned bytes), { return KT->version >= 63 ? KT->gpu_vbuf (bytes) : 0; })
KAPI_CALL (int, kapi_gpu_texture_rect, (int handle, int x, int y, int w, int h, const unsigned *pixels, int stride),
	{ return KT->version >= 70 ? KT->gpu_texture_rect (handle, x, y, w, h, pixels, stride) : -1; })
KAPI_CALL (int, kapi_screen_set, (int w, int h), { return KT->version >= 66 ? KT->screen_set (w, h) : -4; })
KAPI_CALL (int, kapi_thread_create, (int (*fn) (void *), void *arg, unsigned stack_size, const char *name),
	{ return KT->version >= 67 ? KT->thread_create (fn, arg, stack_size, name) : -3; })
KAPI_CALL (void, kapi_thread_exit, (int code), { if (KT->version >= 67) KT->thread_exit (code); KT->exit (code); })
KAPI_CALL (int, kapi_thread_join, (int tid, unsigned timeout_ms, int *code),
	{ return KT->version >= 67 ? KT->thread_join (tid, timeout_ms, code) : -2; })
KAPI_CALL (int, kapi_thread_self, (void), { return KT->version >= 67 ? KT->thread_self () : 1; })
KAPI_CALL (int, kapi_mutex_create, (void), { return KT->version >= 67 ? KT->mutex_create () : -1; })
KAPI_CALL (int, kapi_mutex_lock, (int h, unsigned timeout_ms), { return KT->version >= 67 ? KT->mutex_lock (h, timeout_ms) : -2; })
KAPI_CALL (int, kapi_mutex_unlock, (int h), { return KT->version >= 67 ? KT->mutex_unlock (h) : -2; })
KAPI_CALL (int, kapi_event_create, (int manual_reset, int initial), { return KT->version >= 67 ? KT->event_create (manual_reset, initial) : -1; })
KAPI_CALL (int, kapi_event_set, (int h), { return KT->version >= 67 ? KT->event_set (h) : -2; })
KAPI_CALL (int, kapi_event_reset, (int h), { return KT->version >= 67 ? KT->event_reset (h) : -2; })
KAPI_CALL (int, kapi_event_wait, (int h, unsigned timeout_ms), { return KT->version >= 67 ? KT->event_wait (h, timeout_ms) : -2; })
KAPI_CALL (int, kapi_barrier_create, (unsigned count), { return KT->version >= 67 ? KT->barrier_create (count) : -1; })
KAPI_CALL (int, kapi_barrier_wait, (int h), { return KT->version >= 67 ? KT->barrier_wait (h) : -2; })
KAPI_CALL (int, kapi_sync_close, (int h), { return KT->version >= 67 ? KT->sync_close (h) : -2; })
KAPI_CALL (int, kapi_post, (void (*fn) (void *ctx, long value), void *ctx, long value),
	{ return KT->version >= 67 ? KT->post (fn, ctx, value) : -2; })
KAPI_CALL (int, kapi_pump_wait, (unsigned timeout_ms),
	{ if (KT->version >= 67) return KT->pump_wait (timeout_ms); KT->msleep (timeout_ms > 16 ? 16 : timeout_ms); KT->pump_events (); return 0; })
KAPI_CALL (void, kapi_lock, (volatile int *l),
	{
	while (kapi__xchg (l, 1) != 0)
	{
		if (kapi__core () == 0) KT->yield (); else kapi__pause ();
	}
})
KAPI_CALL (int, kapi_wait_word, (volatile unsigned *addr, unsigned expected, unsigned timeout_ms),
	{ return KT->version >= 68 ? KT->wait_word (addr, expected, timeout_ms) : -1; })
KAPI_CALL (int, kapi_wake_word, (volatile unsigned *addr), { return KT->version >= 68 ? KT->wake_word (addr) : -1; })
KAPI_CALL (int, kapi_thread_priority, (int tid, int prio), { return KT->version >= 68 ? KT->thread_priority (tid, prio) : -2; })
KAPI_CALL (int, kapi_midi_read, (struct kapi_midi_event *ev, int max), { return KT->version >= 68 ? KT->midi_read (ev, max) : 0; })
KAPI_CALL (int, kapi_midi_devices, (void), { return KT->version >= 68 ? KT->midi_devices () : 0; })
KAPI_CALL (int, kapi_screen_native, (int *w, int *h), { return KT->version >= 69 ? KT->screen_native (w, h) : 0; })
KAPI_CALL (int, kapi_set_timezone, (int minutes), { return KT->version >= 69 ? KT->set_timezone (minutes) : 0; })
KAPI_CALL (int, kapi_sound_config, (int chunk_frames, int ahead),
	{ return KT->version >= 68 ? KT->sound_config (chunk_frames, ahead) : -1; })
KAPI_CALL (struct kapi_sound_ring *, kapi_sound_map, (void),
	{ return KT->version >= 68 ? KT->sound_map () : 0; })
KAPI_CALL (void, kapi_reboot, (void), { KT->reboot (); })
KAPI_CALL (int, kapi_kbd_ready, (void), { return KT->kbd_ready (); })
KAPI_CALL (int, kapi_set_keymap_data, (const char *name, const void *data, unsigned len), { return KT->set_keymap_data (name, data, len); })
KAPI_CALL (int, kapi_random, (void *buf, unsigned len), { return KT->random (buf, len); })
KAPI_CALL (int, kapi_surface_create, (int w, int h), { return KT->surface_create (w, h); })
KAPI_CALL (unsigned *, kapi_surface_map, (int id), { return KT->surface_map (id); })
KAPI_CALL (int, kapi_surface_size, (int id, int *w, int *h), { return KT->surface_size (id, w, h); })
KAPI_CALL (void, kapi_surface_present, (int id), { KT->surface_present (id); })
KAPI_CALL (int, kapi_surface_destroy, (int id), { return KT->surface_destroy (id); })
KAPI_CALL (int, kapi_register_shell, (void), { return 0; })		// (the activity shell is gone: the name stays)
KAPI_CALL (int, kapi_shell_request, (int type, const void *in, unsigned len), { (void) type; (void) in; (void) len; return -1; })
KAPI_CALL (int, kapi_mailbox_send, (int target_pid, int type, const void *in, unsigned len), { return KT->mailbox_send (target_pid, type, in, len); })
KAPI_CALL (int, kapi_mailbox_recv, (int *from_pid, int *type, void *buf, unsigned cap, int blocking), { return KT->mailbox_recv (from_pid, type, buf, cap, blocking); })
KAPI_CALL (long, kapi_ws_ctl, (int op, long a0, long a1, long a2),
	{ return KT->version >= 89 && KT->ws_ctl ? KT->ws_ctl (op, a0, a1, a2) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_vol_list, (struct kapi_volume *out, int max, unsigned flags),
	{ return KT->version >= 93 && KT->vol_list ? KT->vol_list (out, max, flags) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_vol_eject, (const char *vol, unsigned flags),
	{ return KT->version >= 93 && KT->vol_eject ? KT->vol_eject (vol, flags) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_vol_mount, (const char *vol),
	{ return KT->version >= 93 && KT->vol_mount ? KT->vol_mount (vol) : -KAPI_ENOSYS; })
KAPI_CALL (int, kapi_vol_format, (const char *vol, const struct kapi_format *fmt),
	{ return KT->version >= 93 && KT->vol_format ? KT->vol_format (vol, fmt) : -KAPI_ENOSYS; })

// The memory primitives (appkit.h: real symbols, the C names are aliased onto them at link time).
#ifndef _WIN32		// (the Windows build of the apps, pc/Koton: its C library's; no weak symbols there)
#ifdef __cplusplus
extern "C" {
#endif
#ifdef KAPI_IMPL
#define KAPI_MEM
#else
#define KAPI_MEM	__attribute__ ((weak))
#endif
KAPI_MEM void *kapi_memset (void *dst, int c, unsigned long n)                 { return KT->memset (dst, c, n); }
KAPI_MEM void *kapi_memcpy (void *dst, const void *src, unsigned long n)       { return KT->memcpy (dst, src, n); }
KAPI_MEM void *kapi_memmove (void *dst, const void *src, unsigned long n)      { return KT->memmove (dst, src, n); }
#undef KAPI_MEM
#ifdef __cplusplus
}
#endif
#endif

#undef KAPI_CALL
