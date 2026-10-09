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
