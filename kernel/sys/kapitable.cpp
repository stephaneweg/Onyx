//
// kapitable.cpp -- defines the published kapi function table and fills it with the
// addresses of the kapi_* functions (in sys/kapi.cpp). The table lives on its own
// 64 KB page (identity-mapped) and is mapped read-only at KAPI_TABLE_VA into every
// app's address space (see mm/addrspace.cpp), so apps call the kernel through it
// without linking against kernel addresses.
//
#include <kern/kapitable.h>
#include <kern/kapi_abi.h>
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
int kapi_message_box (const char *, const char *, int);
int kapi_file_open (char *, unsigned, const char *);
int kapi_file_save (char *, unsigned, const char *, const char *);
int kapi_exec (const char *, const char *);
void kapi_screen_size (int *, int *);
void kapi_move_window (int, int);
unsigned *kapi_wallpaper_buffer (int *, int *);
void kapi_wallpaper_commit (void);
int kapi_list_procs (char *, unsigned);
int kapi_kill_pid (int, int);
int kapi_set_keymap (const char *);
int kapi_get_keymap (char *, unsigned);
void kapi_set_window_theme (unsigned, unsigned, unsigned);
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
int kapi_set_wallpaper (const char *);
int kapi_wallpaper_generate (unsigned, int, unsigned);
void kapi_present (void);
unsigned kapi_get_ticks (void);
void kapi_msleep (unsigned);
void kapi_yield (void);
void kapi_exit (int);

unsigned long kapi_add_button (int, int, int, int, const char *, gui_handler);
unsigned long kapi_add_label (int, int, int, int, const char *);
unsigned long kapi_add_checkbox (int, int, int, int, const char *, gui_handler);
unsigned long kapi_add_textbox (int, int, int, int, gui_handler);
unsigned long kapi_add_progress (int, int, int, int);
unsigned long kapi_add_slider (int, int, int, int, gui_handler);
unsigned long kapi_add_textarea (int, int, int, int, gui_handler);
unsigned long kapi_add_scrollbar_v (int, int, int, int, gui_handler);
unsigned long kapi_add_scrollbar_h (int, int, int, int, gui_handler);
unsigned long kapi_add_icon (int, int, int, int, const char *, const char *, gui_handler);

int kapi_widget_get_text (unsigned long, char *, unsigned);
void kapi_widget_set_text (unsigned long, const char *);
int kapi_widget_get_checked (unsigned long);
int kapi_widget_get_value (unsigned long);
void kapi_widget_set_value (unsigned long, int);
void kapi_widget_set_rect (unsigned long, int, int, int, int);
void kapi_widget_set_icon (unsigned long, const char *);

void kapi_pump_events (void);
void kapi_wait_for_exit (void);
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
void *kapi_sbrk (long);
void kapi_reboot (void);
int kapi_kbd_ready (void);
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

}  // extern "C"

// The table, on its own 64 KB page so a single mapping covers it exactly.
__attribute__ ((aligned (0x10000))) static TKApiTable s_Table;

u64 KApiTablePhys (void)
{
	return (u64) (uintptr) &s_Table;	// identity region: PA == kernel VA
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
	t->set_wallpaper     = kapi_set_wallpaper;
	t->wallpaper_generate = kapi_wallpaper_generate;
	t->present           = kapi_present;
	t->get_ticks         = kapi_get_ticks;
	t->msleep            = kapi_msleep;
	t->yield             = kapi_yield;
	t->exit              = kapi_exit;

	t->add_button        = kapi_add_button;
	t->add_label         = kapi_add_label;
	t->add_checkbox      = kapi_add_checkbox;
	t->add_textbox       = kapi_add_textbox;
	t->add_progress      = kapi_add_progress;
	t->add_slider        = kapi_add_slider;
	t->add_textarea      = kapi_add_textarea;
	t->add_scrollbar_v   = kapi_add_scrollbar_v;
	t->add_scrollbar_h   = kapi_add_scrollbar_h;
	t->add_icon          = kapi_add_icon;

	t->widget_get_text   = kapi_widget_get_text;
	t->widget_set_text   = kapi_widget_set_text;
	t->widget_get_checked = kapi_widget_get_checked;
	t->widget_get_value  = kapi_widget_get_value;
	t->widget_set_value  = kapi_widget_set_value;
	t->widget_set_rect   = kapi_widget_set_rect;
	t->widget_set_icon   = kapi_widget_set_icon;

	t->pump_events       = kapi_pump_events;
	t->wait_for_exit     = kapi_wait_for_exit;
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
	t->message_box       = kapi_message_box;
	t->file_open         = kapi_file_open;
	t->file_save         = kapi_file_save;
	t->exec              = kapi_exec;
	t->screen_size       = kapi_screen_size;
	t->move_window       = kapi_move_window;
	t->wallpaper_buffer  = kapi_wallpaper_buffer;
	t->wallpaper_commit  = kapi_wallpaper_commit;
	t->list_procs        = kapi_list_procs;
	t->kill_pid          = kapi_kill_pid;
	t->set_keymap        = kapi_set_keymap;
	t->get_keymap        = kapi_get_keymap;
	t->set_window_theme  = kapi_set_window_theme;
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
}
