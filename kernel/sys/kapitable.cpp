//
// kapitable.cpp -- the kernel's kapi table: TKApiTable (kern/kapi_abi.h) filled with the
// addresses of the kapi_* functions (sys/kapi.cpp...). It is the SYSTEM-CALL table: an app's
// "svc #0" with slot n in x8 runs entry n (sys/el0.cpp, El0SyncHandler). Apps never see it --
// their KAPI_TABLE_VA page is the EL0 table (kern/el0.h), whose entries point at EL0 stubs
// that make those system calls. The layout is the ABI (append-only): every field is kept.
//
#include <kern/kapitable.h>
#include <kern/kapi_abi.h>
#include <kern/appcore.h>
#include <kern/v3d.h>
#include <kern/el0.h>			// kapi_proc_stats (v74)
#include <circle/types.h>

// The kapi_* functions (defined in sys/kapi.cpp). Declared here with the ABI's
// signatures (handler params as gui_handler) so they assign straight into the
// table; C linkage matches them to the void*-taking definitions by symbol name.
extern "C" {

unsigned *kapi_create_window (int, int, const char *);
unsigned *kapi_create_window_ex (int, int, int, int, const char *, unsigned);
unsigned *kapi_resize_window (int, int);
int kapi_launch (const char *);
int kapi_toggle_app (const char *);
int kapi_raise_app (const char *);
int kapi_list_windows (char *, unsigned);
int kapi_list_tasks (char *, unsigned);
int kapi_kill (const char *);
int kapi_exec (const char *, const char *);
int kapi_exec_as (const char *, const char *, const char *);
int kapi_pad_state (int, struct kapi_pad *);
void kapi_screen_size (int *, int *);
void kapi_move_window (int, int);
unsigned *kapi_wallpaper_buffer (int *, int *);
void kapi_wallpaper_commit (void);
int kapi_list_procs (char *, unsigned);
int kapi_kill_pid (int, int);
int kapi_set_keymap (const char *);
int kapi_get_keymap (char *, unsigned);
int kapi_chdir (const char *);
int kapi_getcwd (char *, unsigned);
void *kapi_stdin (void);
void *kapi_stdout (void);
int kapi_klog_read (int *, char *, unsigned, char *, unsigned);
int kapi_set_verbose (int);
int kapi_get_verbose (void);
int kapi_net_status (char *, unsigned);
int kapi_tcp_connect (const char *, unsigned);
int kapi_tcp_send (int, const void *, unsigned);
int kapi_tcp_recv (int, void *, unsigned);
void kapi_tcp_close (int);
int kapi_tcp_listen (unsigned);
int kapi_tcp_accept (int, char *, unsigned);
int kapi_screen_grab (unsigned *, int, int);
void kapi_inject_pointer (int, int, unsigned, int);
void kapi_inject_key (const char *);
int kapi_set_menu (const char *, void *);
unsigned kapi_get_menu (char *, unsigned, char *, unsigned);
int kapi_menu_command (int);
int kapi_ipc_register (const char *);
int kapi_ipc_lookup (const char *);
int kapi_clipboard_set (int, const void *, unsigned);
int kapi_clipboard_get (int *, void *, unsigned, unsigned *);
void kapi_set_window_alpha (int);
void kapi_shutdown (int);
unsigned *kapi_fullscreen_begin (int *, int *);
void kapi_present_fb (void);
void kapi_fullscreen_end (void);
unsigned *kapi_fullscreen_direct (int *, int *, int *);
int kapi_win_list (struct kapi_win_info *, int);
int kapi_win_read (unsigned, int, int, int, int, int, unsigned *, int);
int kapi_win_raise (unsigned);
int kapi_win_close (unsigned);
int kapi_seek (void *, unsigned long long);
void *kapi_code_alloc (unsigned long);
unsigned long long kapi_fsize64 (void *);
int  kapi_drag_begin (int, const void *, unsigned, const char *);
int  kapi_drag_data (int *, void *, unsigned);
unsigned kapi_get_modifiers (void);
void kapi_inject_modifiers (unsigned);
int  kapi_net_ping (const char *, unsigned, unsigned, char *, unsigned);
int  kapi_net_resolve (const char *, char *, unsigned);
int  kapi_net_info (char *, unsigned);
int  kapi_vfs_register (const char *);
int  kapi_vfs_next (struct kapi_vfs_req *, int);
int  kapi_vfs_req_data (unsigned, void *, unsigned, unsigned);
int  kapi_vfs_reply (unsigned, int, const void *, unsigned);
int  kapi_wlan_scan (struct kapi_wlan_ap *, int);
int  kapi_wlan_reconnect (void);
int  kapi_gpu_program (int, const struct kapi_gpu_program *);
int  kapi_gpu_render2 (const struct kapi_gpu_frame *, const float *, unsigned, unsigned, const struct kapi_gpu_batch2 *, unsigned, const unsigned *, unsigned);
int  kapi_gpu_render3 (const struct kapi_gpu_frame *, const float *, unsigned, const struct kapi_gpu_batch3 *, unsigned, const unsigned *, unsigned, const float *);
void *kapi_gpu_vbuf (unsigned);
int  kapi_gpu_texture_rect (int, int, int, int, int, const unsigned *, int);
int  kapi_vol_info (const char *, struct kapi_vol_info *);
int  kapi_pop_event (struct kapi_event *);		// (v73)
unsigned kapi_event_mods (unsigned);
int  kapi_pop_post (struct kapi_posted *);
int  kapi_pump_sleep (unsigned);
int  kapi_win_minimise (unsigned);
int  kapi_win_geometry (struct kapi_win_geom *);
unsigned *kapi_resize_window2 (int, int, int *);
int  kapi_desk (int, int);
int  kapi_win_desk (unsigned, int);
int  kapi_screen_set (int, int);
int  kapi_thread_create (int (*) (void *), void *, unsigned, const char *);
void kapi_thread_exit (int);
int  kapi_thread_join (int, unsigned, int *);
int  kapi_thread_self (void);
int  kapi_mutex_create (void);
int  kapi_mutex_lock (int, unsigned);
int  kapi_mutex_unlock (int);
int  kapi_event_create (int, int);
int  kapi_event_set (int);
int  kapi_event_reset (int);
int  kapi_event_wait (int, unsigned);
int  kapi_barrier_create (unsigned);
int  kapi_barrier_wait (int);
int  kapi_sync_close (int);
int  kapi_post (void (*) (void *, long), void *, long);
int  kapi_wait_word (volatile unsigned *, unsigned, unsigned);
int  kapi_wake_word (volatile unsigned *);
int  kapi_thread_priority (int, int);
int  kapi_midi_read (struct kapi_midi_event *, int);
int  kapi_midi_devices (void);
int  kapi_sound_acquire (void);
void kapi_sound_release (void);
int  kapi_sound_start (int, unsigned, int, int);
int  kapi_sound_stop (int);
int  kapi_sound_write (const short *, unsigned);
int  kapi_sound_status (unsigned *, unsigned *, unsigned *);
int  kapi_sound_volume (int, int);
int  kapi_sound_instrument (int, const struct kapi_fm_instrument *);
int  kapi_sound_config (int, int);
struct kapi_sound_ring *kapi_sound_map (void);
int  kapi_screen_native (int *, int *);
int  kapi_set_timezone (int);
int  kapi_key_held (int);
void kapi_inject_key_held (int, int);
int kapi_wallpaper_generate (unsigned, int, unsigned);
void kapi_present (void);
unsigned kapi_get_ticks (void);
void kapi_msleep (unsigned);
void kapi_yield (void);
void kapi_exit (int);

int kapi_should_exit (void);

void kapi_draw_text (int, int, const char *, unsigned);
int kapi_font_width (void);
int kapi_font_height (void);
void kapi_set_key_handler (gui_handler);

int kapi_list_apps (char *, unsigned);
int kapi_get_datetime (int *, int *, int *, int *, int *, int *);

int kapi_write (int, const void *, unsigned);
void *kapi_open (const char *);
int kapi_read (void *, void *, unsigned);
unsigned kapi_fsize (void *);
void kapi_close (void *);
int kapi_save_file (const char *, const void *, unsigned);

int kapi_app_dir (char *, unsigned);
void kapi_set_click_handler (gui_handler);
void kapi_set_pointer_handler (gui_handler);
int kapi_meminfo (unsigned long *, unsigned long *, unsigned long *, unsigned *);
int kapi_ram_detail (unsigned long *, unsigned long *, unsigned long *, unsigned long *, unsigned *);
void kapi_set_wheel_speed (int);
int kapi_get_wheel_speed (void);
void *kapi_sbrk (long);
void kapi_reboot (void);
int kapi_kbd_ready (void);
int kapi_set_keymap_data (const char *, const void *, unsigned);
int kapi_get_chrome (struct kapi_chrome *);
void kapi_draw_text_buf (unsigned *, int, int, int, int, const char *, unsigned);
int kapi_random (void *, unsigned);
void *kapi_opendir (const char *);
int kapi_readdir (void *, struct kapi_dirent *);
void kapi_closedir (void *);
int kapi_mkdir (const char *);
int kapi_remove (const char *);
int kapi_rename (const char *, const char *);
void kapi_cursor_pos (int *, int *);

void *kapi_pipe (void);
void *kapi_file_in (const char *);
void *kapi_file_out (const char *, int);
int kapi_stream_read (void *, void *, unsigned);
int kapi_stream_read_nb (void *, void *, unsigned);
int kapi_stream_write (void *, const void *, unsigned);
void kapi_stream_close (void *);
void kapi_stream_eof (void *);
int kapi_proc_done (void *);
int kapi_stdin_read (void *, unsigned);
int kapi_stdout_write (const void *, unsigned);
void *kapi_spawn (const char *, const char *, void *, void *);
int kapi_wait (void *);
int kapi_get_args (char *, unsigned);

int kapi_surface_create (int, int);
unsigned *kapi_surface_map (int);
int kapi_surface_size (int, int *, int *);
void kapi_surface_present (int);
int kapi_surface_destroy (int);

int kapi_register_shell (void);
int kapi_shell_request (int, const void *, unsigned);
int kapi_mailbox_send (int, int, const void *, unsigned);
int kapi_mailbox_recv (int *, int *, void *, unsigned, int);

// v75 WP-MEM (sys/vm.cpp)
long long kapi_vm_map (unsigned long long ulAddr, unsigned long long ulLen, unsigned nProt, unsigned nFlags);
int kapi_vm_unmap (unsigned long long ulAddr, unsigned long long ulLen);
int kapi_vm_protect (unsigned long long ulAddr, unsigned long long ulLen, unsigned nProt);
int kapi_vm_advise (unsigned long long ulAddr, unsigned long long ulLen, int nAdvice);
int kapi_vm_query (unsigned long long ulAddr, struct kapi_vm_region *pOut);
int kapi_vm_stats (int nPid, struct kapi_vm_stats *pOut);
int kapi_thread_create_ex (const struct kapi_thread_attr *pAttr);
int kapi_thread_info (int nTid, struct kapi_thread_info *pOut);

// v75 WP-FILE/PROC (sys/ofile.cpp, sys/procx.cpp)
long long kapi_file_open (const char *pPath, unsigned nFlags, unsigned nMode);
long long kapi_file_read (long long h, void *pBuf, unsigned long long nLen, long long nOff);
long long kapi_file_write (long long h, const void *pBuf, unsigned long long nLen, long long nOff);
long long kapi_file_seek (long long h, long long nOff, int nWhence);
int kapi_file_truncate (long long h, long long nSize);
int kapi_file_sync (long long h);
int kapi_file_stat (long long h, struct kapi_stat *pOut);
int kapi_file_close (long long h);
int kapi_path_stat (const char *pPath, struct kapi_stat *pOut);
int kapi_path_unlink (const char *pPath, unsigned nFlags);
int kapi_path_mkdir (const char *pPath, unsigned nMode);
int kapi_path_rename (const char *pFrom, const char *pTo);
int kapi_path_utime (const char *pPath, long long nMTime);
int kapi_dir_read (void *hDir, struct kapi_dirent2 *pOut);
int kapi_stream_write_nb (void *h, const void *pBuf, unsigned nLen);
long long kapi_spawn_ex (const struct kapi_spawn_attr *pAttr);
int kapi_proc_wait (void *hProc, unsigned nFlags, struct kapi_proc_status *pOut);
int kapi_get_argv (char *pBuf, unsigned nCap);
int kapi_get_env (char *pBuf, unsigned nCap);
int kapi_getpid (int nWhich);
int kapi_clock_info (struct kapi_clock_info *pOut);
int kapi_sleep_us (unsigned long long nMicros);

// v75 WP-NET (sys/bsdsock.cpp)
int kapi_sock_open (int nType, unsigned nFlags);
int kapi_sock_connect (int s, const struct kapi_sockaddr *pTo);
int kapi_sock_bind (int s, const struct kapi_sockaddr *pAddr);
int kapi_sock_listen (int s, int nBacklog);
int kapi_sock_accept (int s, struct kapi_sockaddr *pPeer, unsigned nFlags);
long long kapi_sock_send (int s, const void *pBuf, unsigned long long nLen, unsigned nFlags, const struct kapi_sockaddr *pTo);
long long kapi_sock_recv (int s, void *pBuf, unsigned long long nLen, unsigned nFlags, struct kapi_sockaddr *pFrom);
int kapi_sock_shutdown (int s, int nHow);
int kapi_sock_close (int s);
int kapi_sock_getopt (int s, int nOpt, int *pValue);
int kapi_sock_setopt (int s, int nOpt, int nValue);
int kapi_sock_name (int s, int nPeer, struct kapi_sockaddr *pOut);
int kapi_poll (struct kapi_pollfd *pFds, unsigned n, int nTimeoutMs);
// v76 WP-IPC (sys/lsock.cpp, sys/shm.cpp, sys/vm.cpp, sys/procx.cpp)
int kapi_sock_pair (int nType, unsigned nFlags, int *pSv);
long long kapi_sock_sendmsg (int s, const struct kapi_msghdr *pM, unsigned nFlags);
long long kapi_sock_recvmsg (int s, struct kapi_msghdr *pM, unsigned nFlags);
long long kapi_shm_create (unsigned long long nSize, unsigned nFlags);
long long kapi_shm_open (const char *pName, unsigned nOFlags, unsigned nMode);
int kapi_shm_unlink (const char *pName);
long long kapi_shm_ctl (long long h, int nOp, unsigned long long nArg);
long long kapi_shm_map (long long h, unsigned long long ulAddr, unsigned long long ulLen, unsigned nProt,
			unsigned nFlags, unsigned long long ulOff);
int kapi_handle_close (long long h);
long long kapi_spawn_ex2 (const struct kapi_spawn_attr *pA, const struct kapi_handle_xfer *pH, unsigned n);
int kapi_get_handles (struct kapi_handle_xfer *pOut, unsigned nCap);
// v77 program images (sys/kapi.cpp over proc/image.cpp)
int kapi_image_preload (const char *pPath);
int kapi_image_unload (const char *pPath);
int kapi_image_list (const char *pPath, struct kapi_image_info *pOut, unsigned nCap);
// v79 what the kernel is (sys/kapi.cpp)
int kapi_kernel_info (char *pBuf, unsigned nCap);
// v80 the cores' load, the network's bytes (sys/kapi.cpp)
int kapi_cpu_stats (struct kapi_cpu_stats *pOut);
int kapi_net_stats (int nPid, struct kapi_net_stats *pOut);
// v81 the pointer's shape (sys/kapi.cpp)
int kapi_set_cursor (int nShape);
// v82 a window resized by its frame (sys/kapi.cpp)
int kapi_win_resizable (int bOn, int nMinW, int nMinH);
// v83 shared libraries (sys/kapi.cpp over proc/image.cpp)
const void *kapi_lib_open (const char *pName, unsigned nMinVersion, int *pErr);
// v84 the sound's output (sys/kapi.cpp over sys/sound.cpp)
int kapi_sound_output (int nOut);
// v85 the sound's mixer
int kapi_sound_clients (struct kapi_sound_client *pOut, int nMax);
int kapi_sound_client_volume (unsigned nPid, int nVolume, int nMute);

}  // extern "C"

// The table (kernel memory only: never mapped into an app's space).
static TKApiTable s_Table;

const TKApiTable *KApiKernelTable (void)
{
	return &s_Table;
}

void KApiTableInit (void)
{
	TKApiTable *t = &s_Table;
	t->version = KAPI_ABI_VERSION;

	t->create_window     = kapi_create_window;
	t->create_window_ex  = kapi_create_window_ex;
	t->resize_window     = kapi_resize_window;
	t->launch            = kapi_launch;
	t->toggle_app        = kapi_toggle_app;
	t->raise_app         = kapi_raise_app;
	t->list_windows      = kapi_list_windows;
	t->wallpaper_generate = kapi_wallpaper_generate;
	t->present           = kapi_present;
	t->get_ticks         = kapi_get_ticks;
	t->msleep            = kapi_msleep;
	t->yield             = kapi_yield;
	t->exit              = kapi_exit;

	// pump_events, wait_for_exit (and pump_wait, memset, memcpy, memmove below): 0 here --
	// not system calls. The EL0 table points them at user-side code (kern/el0.h): the pump
	// calls the app's handlers itself, at EL0 (the kernel never runs an app's code).
	t->pump_events       = 0;
	t->wait_for_exit     = 0;
	t->should_exit       = kapi_should_exit;

	t->draw_text         = kapi_draw_text;
	t->font_width        = kapi_font_width;
	t->font_height       = kapi_font_height;
	t->set_key_handler   = kapi_set_key_handler;

	t->list_apps         = kapi_list_apps;
	t->get_datetime      = kapi_get_datetime;

	t->write             = kapi_write;
	t->open              = kapi_open;
	t->read              = kapi_read;
	t->fsize             = kapi_fsize;
	t->close             = kapi_close;
	t->save_file         = kapi_save_file;

	t->app_dir           = kapi_app_dir;
	t->set_click_handler = kapi_set_click_handler;
	t->opendir           = kapi_opendir;
	t->readdir           = kapi_readdir;
	t->closedir          = kapi_closedir;
	t->mkdir             = kapi_mkdir;
	t->remove            = kapi_remove;
	t->rename            = kapi_rename;
	t->cursor_pos        = kapi_cursor_pos;
	t->list_tasks        = kapi_list_tasks;
	t->kill              = kapi_kill;

	t->pipe              = kapi_pipe;
	t->file_in           = kapi_file_in;
	t->file_out          = kapi_file_out;
	t->stream_read       = kapi_stream_read;
	t->stream_write      = kapi_stream_write;
	t->stream_close      = kapi_stream_close;
	t->stdin_read        = kapi_stdin_read;
	t->stdout_write      = kapi_stdout_write;
	t->spawn             = kapi_spawn;
	t->wait              = kapi_wait;
	t->get_args          = kapi_get_args;
	t->stream_read_nb    = kapi_stream_read_nb;
	t->stream_eof        = kapi_stream_eof;
	t->proc_done         = kapi_proc_done;
	t->exec              = kapi_exec;
	t->screen_size       = kapi_screen_size;
	t->move_window       = kapi_move_window;
	t->wallpaper_buffer  = kapi_wallpaper_buffer;
	t->wallpaper_commit  = kapi_wallpaper_commit;
	t->list_procs        = kapi_list_procs;
	t->kill_pid          = kapi_kill_pid;
	t->set_keymap        = kapi_set_keymap;
	t->get_keymap        = kapi_get_keymap;
	t->chdir             = kapi_chdir;
	t->getcwd            = kapi_getcwd;
	t->stdin_stream      = kapi_stdin;
	t->stdout_stream     = kapi_stdout;
	t->klog_read         = kapi_klog_read;
	t->set_verbose       = kapi_set_verbose;
	t->get_verbose       = kapi_get_verbose;
	t->net_status        = kapi_net_status;
	t->tcp_connect       = kapi_tcp_connect;
	t->tcp_send          = kapi_tcp_send;
	t->tcp_recv          = kapi_tcp_recv;
	t->tcp_close         = kapi_tcp_close;
	t->set_pointer_handler = kapi_set_pointer_handler;
	t->meminfo           = kapi_meminfo;
	t->sbrk              = kapi_sbrk;
	t->reboot            = kapi_reboot;
	t->kbd_ready         = kapi_kbd_ready;
	t->set_keymap_data   = kapi_set_keymap_data;
	t->get_chrome        = kapi_get_chrome;
	t->draw_text_buf     = kapi_draw_text_buf;
	t->random            = kapi_random;
	t->ram_detail        = kapi_ram_detail;
	t->set_wheel_speed   = kapi_set_wheel_speed;
	t->get_wheel_speed   = kapi_get_wheel_speed;

	t->surface_create    = kapi_surface_create;
	t->surface_map       = kapi_surface_map;
	t->surface_size      = kapi_surface_size;
	t->surface_present   = kapi_surface_present;
	t->surface_destroy   = kapi_surface_destroy;

	t->register_shell    = kapi_register_shell;
	t->shell_request     = kapi_shell_request;
	t->mailbox_send      = kapi_mailbox_send;
	t->mailbox_recv      = kapi_mailbox_recv;

	// (v36) memset / memcpy / memmove: done at EL0 (the EL0 table's, kern/el0.h), never a
	// system call
	t->memset            = 0;
	t->memcpy            = 0;
	t->memmove           = 0;

	t->tcp_listen        = kapi_tcp_listen;
	t->tcp_accept        = kapi_tcp_accept;

	t->screen_grab       = kapi_screen_grab;
	t->inject_pointer    = kapi_inject_pointer;
	t->inject_key        = kapi_inject_key;

	t->set_menu          = (int (*) (const char *, gui_handler)) kapi_set_menu;
	t->get_menu          = kapi_get_menu;
	t->menu_command      = kapi_menu_command;

	t->ipc_register      = kapi_ipc_register;
	t->ipc_lookup        = kapi_ipc_lookup;
	t->clipboard_set     = kapi_clipboard_set;
	t->clipboard_get     = kapi_clipboard_get;
	t->set_window_alpha  = kapi_set_window_alpha;
	t->shutdown          = kapi_shutdown;

	t->fullscreen_begin  = kapi_fullscreen_begin;
	t->present_fb        = kapi_present_fb;
	t->fullscreen_end    = kapi_fullscreen_end;

	t->drag_begin        = kapi_drag_begin;
	t->drag_data         = kapi_drag_data;
	t->get_modifiers     = kapi_get_modifiers;
	t->inject_modifiers  = kapi_inject_modifiers;

	t->net_ping          = kapi_net_ping;
	t->net_resolve       = kapi_net_resolve;
	t->net_info          = kapi_net_info;

	t->vfs_register      = kapi_vfs_register;
	t->vfs_next          = kapi_vfs_next;
	t->vfs_req_data      = kapi_vfs_req_data;
	t->vfs_reply         = kapi_vfs_reply;

	t->wlan_scan         = kapi_wlan_scan;

	t->sound_acquire     = kapi_sound_acquire;
	t->sound_release     = kapi_sound_release;
	t->sound_start       = kapi_sound_start;
	t->sound_stop        = kapi_sound_stop;
	t->sound_write       = kapi_sound_write;
	t->sound_status      = kapi_sound_status;
	t->sound_instrument  = kapi_sound_instrument;
	t->key_held          = kapi_key_held;
	t->inject_key_held   = kapi_inject_key_held;
	t->exec_as           = kapi_exec_as;
	t->pad_state         = kapi_pad_state;
	t->core_acquire      = kapi_core_acquire;
	t->core_run          = kapi_core_run;
	t->core_state        = kapi_core_state;
	t->core_release      = kapi_core_release;
	t->gpu_info          = kapi_gpu_info;
	t->gpu_draw          = kapi_gpu_draw;
	t->gpu_texture       = kapi_gpu_texture;
	t->gpu_render        = kapi_gpu_render;
	t->fullscreen_direct = kapi_fullscreen_direct;
	t->win_list          = kapi_win_list;
	t->win_read          = kapi_win_read;
	t->win_raise         = kapi_win_raise;
	t->win_close         = kapi_win_close;
	t->seek              = kapi_seek;
	t->code_alloc        = kapi_code_alloc;
	t->fsize64           = kapi_fsize64;
	t->sound_volume      = kapi_sound_volume;
	t->wlan_reconnect    = kapi_wlan_reconnect;
	t->gpu_program       = kapi_gpu_program;
	t->gpu_render2       = kapi_gpu_render2;
	t->gpu_render3       = kapi_gpu_render3;
	t->gpu_vbuf          = kapi_gpu_vbuf;
	t->win_minimise      = kapi_win_minimise;
	t->win_geometry      = kapi_win_geometry;
	t->resize_window2    = kapi_resize_window2;
	t->desk              = kapi_desk;
	t->win_desk          = kapi_win_desk;
	t->screen_set        = kapi_screen_set;
	t->thread_create     = kapi_thread_create;
	t->thread_exit       = kapi_thread_exit;
	t->thread_join       = kapi_thread_join;
	t->thread_self       = kapi_thread_self;
	t->mutex_create      = kapi_mutex_create;
	t->mutex_lock        = kapi_mutex_lock;
	t->mutex_unlock      = kapi_mutex_unlock;
	t->event_create      = kapi_event_create;
	t->event_set         = kapi_event_set;
	t->event_reset       = kapi_event_reset;
	t->event_wait        = kapi_event_wait;
	t->barrier_create    = kapi_barrier_create;
	t->barrier_wait      = kapi_barrier_wait;
	t->sync_close        = kapi_sync_close;
	t->post              = kapi_post;
	t->pump_wait         = 0;			// (user-side: kern/el0.h)
	t->sound_config      = kapi_sound_config;
	t->sound_map         = kapi_sound_map;
	t->wait_word         = kapi_wait_word;
	t->wake_word         = kapi_wake_word;
	t->thread_priority   = kapi_thread_priority;
	t->midi_read         = kapi_midi_read;
	t->midi_devices      = kapi_midi_devices;
	t->screen_native     = kapi_screen_native;
	t->set_timezone      = kapi_set_timezone;
	t->gpu_texture_rect  = kapi_gpu_texture_rect;
	t->vol_info          = kapi_vol_info;
	t->pop_event         = kapi_pop_event;
	t->event_mods        = kapi_event_mods;
	t->pop_post          = kapi_pop_post;
	t->pump_sleep        = kapi_pump_sleep;
	t->proc_stats        = kapi_proc_stats;		// (v74, sys/el0.cpp)
	// --- v75 WP-MEM (sys/vm.cpp) ---
	t->vm_map            = kapi_vm_map;
	t->vm_unmap          = kapi_vm_unmap;
	t->vm_protect        = kapi_vm_protect;
	t->vm_advise         = kapi_vm_advise;
	t->vm_query          = kapi_vm_query;
	t->vm_stats          = kapi_vm_stats;
	t->thread_create_ex  = kapi_thread_create_ex;
	t->thread_info       = kapi_thread_info;
	// --- v75 WP-FILE/PROC (sys/ofile.cpp, sys/procx.cpp) ---
	t->file_open         = kapi_file_open;
	t->file_read         = kapi_file_read;
	t->file_write        = kapi_file_write;
	t->file_seek         = kapi_file_seek;
	t->file_truncate     = kapi_file_truncate;
	t->file_sync         = kapi_file_sync;
	t->file_stat         = kapi_file_stat;
	t->file_close        = kapi_file_close;
	t->path_stat         = kapi_path_stat;
	t->path_unlink       = kapi_path_unlink;
	t->path_mkdir        = kapi_path_mkdir;
	t->path_rename       = kapi_path_rename;
	t->path_utime        = kapi_path_utime;
	t->dir_read          = kapi_dir_read;
	t->stream_write_nb   = kapi_stream_write_nb;
	t->spawn_ex          = kapi_spawn_ex;
	t->proc_wait         = kapi_proc_wait;
	t->get_argv          = kapi_get_argv;
	t->get_env           = kapi_get_env;
	t->getpid            = kapi_getpid;
	t->clock_info        = kapi_clock_info;
	t->sleep_us          = kapi_sleep_us;
	// --- v75 WP-NET (sys/bsdsock.cpp) ---
	t->sock_open         = kapi_sock_open;
	t->sock_connect      = kapi_sock_connect;
	t->sock_bind         = kapi_sock_bind;
	t->sock_listen       = kapi_sock_listen;
	t->sock_accept       = kapi_sock_accept;
	t->sock_send         = kapi_sock_send;
	t->sock_recv         = kapi_sock_recv;
	t->sock_shutdown     = kapi_sock_shutdown;
	t->sock_close        = kapi_sock_close;
	t->sock_getopt       = kapi_sock_getopt;
	t->sock_setopt       = kapi_sock_setopt;
	t->sock_name         = kapi_sock_name;
	t->poll              = kapi_poll;

	// --- v76 WP-IPC (sys/lsock.cpp, sys/shm.cpp, sys/vm.cpp, sys/procx.cpp) ---
	t->sock_pair         = kapi_sock_pair;
	t->sock_sendmsg      = kapi_sock_sendmsg;
	t->sock_recvmsg      = kapi_sock_recvmsg;
	t->shm_create        = kapi_shm_create;
	t->shm_open          = kapi_shm_open;
	t->shm_unlink        = kapi_shm_unlink;
	t->shm_ctl           = kapi_shm_ctl;
	t->shm_map           = kapi_shm_map;
	t->handle_close      = kapi_handle_close;
	t->spawn_ex2         = kapi_spawn_ex2;
	t->get_handles       = kapi_get_handles;

	// --- v77 program images (proc/image.cpp, sys/kapi.cpp) ---
	t->image_preload     = kapi_image_preload;
	t->image_unload      = kapi_image_unload;
	t->image_list        = kapi_image_list;

	// --- v79 what the kernel is (sys/kapi.cpp) ---
	t->kernel_info       = kapi_kernel_info;
	t->cpu_stats         = kapi_cpu_stats;		// (v80)
	t->net_stats         = kapi_net_stats;
	t->set_cursor        = kapi_set_cursor;		// (v81)
	t->win_resizable     = kapi_win_resizable;	// (v82)
	t->lib_open          = kapi_lib_open;		// (v83)
	t->sound_output      = kapi_sound_output;	// (v84)
	t->sound_clients       = kapi_sound_clients;	// (v85)
	t->sound_client_volume = kapi_sound_client_volume;
}
