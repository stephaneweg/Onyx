//
// k_misc.cpp -- the system calls about the machine (onyxrun.h): the apps' list, the volumes (SD: and RAM: on host
// folders), the keyboard's layout (the PC's own: its characters come cooked), and the hardware the runner does not
// have yet answered as a Pi without it answers: no gamepad, no Wi-Fi, no GPIO, no network (until the host's sockets
// are bridged), no sound output (sound_acquire -1), no GPU (gpu_info "", until the GPU bridge).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include <filesystem>
#include <algorithm>
#include <string.h>
#include <chrono>

namespace fs = std::filesystem;

static int k_list_apps (u64 buf, unsigned cap)
{
	std::error_code ec;
	std::vector<std::string> names;
	std::string h = host_path ("SD:/apps", "SD:/");
	for (auto &e : fs::directory_iterator (fs::u8path (h), ec))
	{
		std::string n = e.path ().filename ().u8string ();
		if (e.is_directory (ec) && n.size () > 4 && n.compare (n.size () - 4, 4, ".app") == 0) names.push_back (n.substr (0, n.size () - 4));
	}
	std::sort (names.begin (), names.end ());
	std::string s;
	for (auto &n : names) s += n + "\n";
	gstr_out (buf, cap, s);
	return (int) names.size ();
}

static void space_of (const std::string &root, unsigned long long *total, unsigned long long *freeb)
{
	std::error_code ec;
	fs::space_info si = fs::space (fs::u8path (root), ec);
	*total = ec ? 0 : (unsigned long long) si.capacity;
	*freeb = ec ? 0 : (unsigned long long) si.available;
}

static int k_vol_info (u64 path, u64 out)
{
	std::string p;
	struct kapi_vol_info *o = G<struct kapi_vol_info> (out, MEM_W);
	if (!o || !gstr (path, p, 1024)) return -1;
	std::string a = onyx_abs (p.c_str (), cur ()->cwd);
	std::string vol = a.substr (0, a.find (':'));
	std::string root = vol == "SD" ? g_Run.root : vol == "RAM" ? g_Run.ramRoot : "";
	if (root.empty ()) return -1;
	memset (o, 0, sizeof *o);
	space_of (root, &o->total, &o->free);
	o->used = o->total - o->free;
	strcpy (o->type, vol == "RAM" ? "RAM" : "FAT32");
	return 0;
}

static int k_vol_list (u64 out, int max, unsigned flags)
{
	(void) flags;
	const char *names[2] = { "SD", "RAM" };
	int n = 0;
	for (int i = 0; i < 2; i++)
	{
		if (n >= max) break;
		struct kapi_volume *v = G<struct kapi_volume> (out + (u64) n * sizeof (struct kapi_volume), MEM_W);
		if (!v) return -KAPI_EFAULT;
		memset (v, 0, sizeof *v);
		strcpy (v->name, names[i]);
		v->state = KAPI_VST_MOUNTED;
		v->flags = i == 0 ? KAPI_VF_SYSTEM : KAPI_VF_RAM;
		v->gen = 1;
		space_of (i == 0 ? g_Run.root : g_Run.ramRoot, &v->total, &v->free);
		v->device_size = v->total;
		strcpy (v->type, i == 0 ? "FAT32" : "RAM");
		strcpy (v->label, i == 0 ? "ONYX" : "");
		strcpy (v->device, i == 0 ? "emmc1" : "");
		n++;
	}
	return 2;
}

static int k_get_keymap (u64 buf, unsigned cap)	{ return gstr_out (buf, cap, "PC") > 0 ? 1 : 0; }
static int k_set_keymap (u64)				{ return 1; }
static int k_set_keymap_data (u64, u64, unsigned)	{ return 1; }
static int k_pad_state (int, u64)			{ return 0; }
static int k_wlan_scan (u64, int)			{ return 0; }
static int k_wlan_reconnect (void)			{ return -1; }
static int k_gpio_ctl (int, u64, u64, u64)		{ return -KAPI_ENODEV; }
static int k_sound_acquire (void)			{ return -1; }
static void k_sound_release (void)			{ }
static int k_sound_write (u64, unsigned)		{ return -1; }
static int k_sound_status (u64 rate, u64 freeFrames, u64 owner)
{
	gput<unsigned> (rate, 44100); gput<unsigned> (freeFrames, 0); gput<unsigned> (owner, 0);
	return 0;
}
static int k_sound_volume (int vol, int mute)		{ (void) vol; (void) mute; return 10; }
static int k_sound_output (int)			{ return 0; }
static int k_sound_clients (u64, int)			{ return 0; }
static int k_midi_devices (void)			{ return 0; }
static int k_midi_read (u64, int)			{ return 0; }

// shutdown / reboot: the runner ends (the PC's window with it)
static void k_shutdown (int mode)
{
	rlog ("%s asked the system to %s: the runner ends", cur ()->name.c_str (), mode ? "restart" : "halt");
	fflush (stdout);
	_Exit (0);
}
static void k_reboot (void) { k_shutdown (1); }

KAPI (list_apps, k_list_apps);
KAPI (vol_info, k_vol_info);
KAPI (vol_list, k_vol_list);
KAPI (get_keymap, k_get_keymap);
KAPI (set_keymap, k_set_keymap);
KAPI (set_keymap_data, k_set_keymap_data);
KAPI (pad_state, k_pad_state);
KAPI (wlan_scan, k_wlan_scan);
KAPI (wlan_reconnect, k_wlan_reconnect);
KAPI (gpio_ctl, k_gpio_ctl);
KAPI (sound_acquire, k_sound_acquire);
KAPI (sound_release, k_sound_release);
KAPI (sound_write, k_sound_write);
KAPI (sound_status, k_sound_status);
KAPI (sound_volume, k_sound_volume);
KAPI (sound_output, k_sound_output);
KAPI (sound_clients, k_sound_clients);
KAPI (midi_devices, k_midi_devices);
KAPI (midi_read, k_midi_read);
KAPI (shutdown, k_shutdown);
KAPI (reboot, k_reboot);

// ---- the shared surfaces (kapi v35): a pixel buffer several processes map -------------------------------------
struct Surface { u8 *host; u64 len; int w, h, owner; };
static std::mutex s_SurfM;
static std::map<int, Surface> s_Surf;
static int s_SurfNext = 1;

static int k_surface_create (int w, int h)
{
	if (w < 1 || h < 1 || w > 4096 || h > 4096) return 0;
	u64 len = ALIGN_UP ((u64) w * h * 4);
	u8 *m = host_shared_alloc (len);
	if (!m) return 0;
	std::lock_guard<std::mutex> L (s_SurfM);
	int id = s_SurfNext++;
	s_Surf[id] = Surface { m, len, w, h, cur ()->pid };
	return id;
}

static u64 k_surface_map (int id)
{
	Surface S;
	{
		std::lock_guard<std::mutex> L (s_SurfM);
		auto it = s_Surf.find (id);
		if (it == s_Surf.end ()) return 0;
		S = it->second;
	}
	Proc *P = cur ();
	PLock L (P);
	u64 va = P->mem.find_free (USER_SURFACE_BASE, USER_SURFACE_END, S.len);
	if (!va || !P->mem.map_shared (va, S.len, MEM_R | MEM_W, "a shared surface", S.host, KAPI_VMK_FIXED)) return 0;
	return va;
}

static int k_surface_size (int id, u64 w, u64 h)
{
	std::lock_guard<std::mutex> L (s_SurfM);
	auto it = s_Surf.find (id);
	if (it == s_Surf.end ()) return 0;
	gput<int> (w, it->second.w); gput<int> (h, it->second.h);
	return 1;
}

static void k_surface_present (int) { }

static int k_surface_destroy (int id)
{
	std::lock_guard<std::mutex> L (s_SurfM);
	auto it = s_Surf.find (id);
	if (it == s_Surf.end () || it->second.owner != cur ()->pid) return 0;
	s_Surf.erase (it);			// (its memory kept: the processes that mapped it may still read it)
	return 1;
}

// ---- the process tree (kapi v91) -----------------------------------------------------------------------------------
static void descendants (int pid, std::vector<int> &out)
{
	for (Proc *P : proc_list ())
		if (!P->ended && P->ppid == pid) { out.push_back (P->pid); descendants (P->pid, out); }
}

static int k_proc_tree (int pid, int op, u64 out, unsigned cap)
{
	Proc *T = proc_find (pid);
	if (!T || T->ended) return -KAPI_ESRCH;
	std::vector<int> d;
	descendants (pid, d);
	if (op == KAPI_TREE_LIST)
	{
		for (unsigned i = 0; i < d.size () && i < cap; i++) gput<int> (out + i * 4, d[i]);
		return (int) d.size ();
	}
	if (op != KAPI_TREE_KILL && op != KAPI_TREE_KILL_CHILDREN) return -KAPI_EINVAL;
	Proc *me = cur ();
	for (Proc *a = me; a; a = a->ppid > 0 ? proc_find (a->ppid) : 0)	// (never the caller or its ancestors)
		if (a->pid == pid && op == KAPI_TREE_KILL) return -KAPI_EPERM;
	for (int c : d) if (c == me->pid) return -KAPI_EPERM;
	if (op == KAPI_TREE_KILL) d.insert (d.begin (), pid);
	int n = 0;
	for (auto it = d.rbegin (); it != d.rend (); ++it)		// (the leaves first)
	{
		Proc *P = proc_find (*it);
		if (!P || P->ended) continue;
		if (!P->dying.exchange (true)) { P->status = -9; P->reason = KAPI_PROC_KILLED; }
		cpu_stop_all (P);
		n++;
	}
	return n;
}

// ---- statistics (the PC's: approximate) -----------------------------------------------------------------------------
static int k_proc_stats (int pid, u64 out)
{
	Proc *P = pid == 0 ? cur () : proc_find (pid);
	if (!P) return -1;
	struct kapi_syscall_stats *o = G<struct kapi_syscall_stats> (out, MEM_W);
	if (!o) return -2;
	memset (o, 0, sizeof *o);
	o->slots = (unsigned) KAPI_SLOTS_HOST;
	return 0;
}

static int k_cpu_stats (u64 out)
{
	struct kapi_cpu_stats *o = G<struct kapi_cpu_stats> (out, MEM_W);
	if (!o) return -KAPI_EFAULT;
	memset (o, 0, sizeof *o);
	o->now_us = (unsigned long long) std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now ().time_since_epoch ()).count ();
	o->cores = 4;
	o->core[0].role = KAPI_CORE_SYSTEM; o->core[1].role = KAPI_CORE_SOUND; o->core[2].role = KAPI_CORE_APP; o->core[3].role = KAPI_CORE_APP;
	return 0;
}

static int k_net_stats (int, u64 out)
{
	struct kapi_net_stats *o = G<struct kapi_net_stats> (out, MEM_W);
	if (!o) return -KAPI_EFAULT;
	memset (o, 0, sizeof *o);
	return 0;
}

static int k_ram_detail (u64 detected, u64 pool, u64 poolFree, u64 above4g, u64 nseg)
{
	gput<u64> (detected, (u64) 4 << 20); gput<u64> (pool, (u64) 3 << 20); gput<u64> (poolFree, (u64) 2 << 20);
	gput<u64> (above4g, 0); gput<unsigned> (nseg, 1);
	return 1;
}

static u64 k_gpu_vbuf (unsigned) { return 0; }		// (for the QPU programs' render3: not yet)

// ---- the program images (kapi v77): the runner reads a program's file at each start ----------------------------------
static int k_image_preload (u64) { return 0; }
static int k_image_unload (u64) { return -KAPI_ENOENT; }
static int k_image_list (u64, u64, unsigned) { return 0; }

KAPI (surface_create, k_surface_create);
KAPI (surface_map, k_surface_map);
KAPI (surface_size, k_surface_size);
KAPI (surface_present, k_surface_present);
KAPI (surface_destroy, k_surface_destroy);
KAPI (proc_tree, k_proc_tree);
KAPI (proc_stats, k_proc_stats);
KAPI (cpu_stats, k_cpu_stats);
KAPI (net_stats, k_net_stats);
KAPI (ram_detail, k_ram_detail);
KAPI (gpu_vbuf, k_gpu_vbuf);
KAPI (image_preload, k_image_preload);
KAPI (image_unload, k_image_unload);
KAPI (image_list, k_image_list);
