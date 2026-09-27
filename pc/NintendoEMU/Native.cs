// Native.cs -- nemucore.dll (core/nemucore.cpp): the Onyx emulator cores, the sound (waveOut),
// the pads (XInput). Paths go as UTF-8.
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace NintendoEMU
{
	enum Sys { None, GB, GBC, GBA, NES, SNES, N64, GC }

	static class Native
	{
		const string DLL = "nemucore.dll";
		const CallingConvention CC = CallingConvention.Cdecl;

		public static byte[] Z (string s) { var b = Encoding.UTF8.GetBytes (s ?? ""); Array.Resize (ref b, b.Length + 1); return b; }
		public static string FromZ (byte[] b) { int n = Array.IndexOf (b, (byte) 0); return Encoding.UTF8.GetString (b, 0, n < 0 ? b.Length : n); }

		[DllImport (DLL, CallingConvention = CC)] static extern int ne_system_of (byte[] path);
		[DllImport (DLL, CallingConvention = CC)] static extern IntPtr ne_open (byte[] path, int rate, byte[] err, int cap);
		[DllImport (DLL, CallingConvention = CC)] public static extern int ne_system (IntPtr h);
		[DllImport (DLL, CallingConvention = CC)] static extern void ne_title (IntPtr h, byte[] o, int cap);
		[DllImport (DLL, CallingConvention = CC)] public static extern int ne_fps1000 (IntPtr h);
		[DllImport (DLL, CallingConvention = CC)] public static extern int ne_aspect1000 (IntPtr h);
		[DllImport (DLL, CallingConvention = CC)] public static extern void ne_set_scale (IntPtr h, int s);
		[DllImport (DLL, CallingConvention = CC)] public static extern void ne_set_keys (IntPtr h, byte[] vk);
		[DllImport (DLL, CallingConvention = CC)] public static extern void ne_reset (IntPtr h);
		[DllImport (DLL, CallingConvention = CC)] public static extern int ne_run_frame (IntPtr h, int draw);
		[DllImport (DLL, CallingConvention = CC)] public static extern int ne_audio (IntPtr h, short[] lr, int max);
		[DllImport (DLL, CallingConvention = CC)] public static extern uint ne_video (IntPtr h, IntPtr px, int cap, out int w, out int hh);
		[DllImport (DLL, CallingConvention = CC)] public static extern int ne_is_3d (IntPtr h);
		[DllImport (DLL, CallingConvention = CC)] static extern int ne_halted (IntPtr h, byte[] msg, int cap);
		[DllImport (DLL, CallingConvention = CC)] public static extern void ne_save (IntPtr h);
		[DllImport (DLL, CallingConvention = CC)] public static extern void ne_close (IntPtr h);
		[DllImport (DLL, CallingConvention = CC)] static extern int ne_thumb (byte[] path, uint[] px, uint[] banner, byte[] name, int cap);
		[DllImport (DLL, CallingConvention = CC)] public static extern int ne_audio_open (int rate);
		[DllImport (DLL, CallingConvention = CC)] public static extern int ne_audio_queued ();
		[DllImport (DLL, CallingConvention = CC)] public static extern void ne_audio_write (short[] lr, int frames);
		[DllImport (DLL, CallingConvention = CC)] public static extern void ne_audio_close ();
		[DllImport (DLL, CallingConvention = CC)] public static extern uint ne_pad_buttons (int port);

		public static Sys SystemOf (string path) { try { return (Sys) ne_system_of (Z (path)); } catch { return Sys.None; } }
		public static IntPtr Open (string path, int rate, out string err)
		{
			var e = new byte[512];
			IntPtr h = ne_open (Z (path), rate, e, e.Length);
			err = FromZ (e);
			return h;
		}
		public static string Title (IntPtr h) { var b = new byte[128]; ne_title (h, b, b.Length); return FromZ (b).Trim (); }
		public static string Halted (IntPtr h) { var b = new byte[128]; return ne_halted (h, b, b.Length) != 0 ? FromZ (b) : null; }
		// the library's picture: 1 = px (160 x 144), 2 = a label (name), 3 = a GameCube banner (96 x 32) + name
		public static int Thumb (string path, uint[] px, uint[] banner, out string name)
		{
			var b = new byte[160];
			int r = ne_thumb (Z (path), px, banner, b, b.Length);
			name = FromZ (b).Trim ();
			return r;
		}

		public static string SystemName (Sys s)
		{
			switch (s)
			{
			case Sys.GB: return "Game Boy";
			case Sys.GBC: return "Game Boy Color";
			case Sys.GBA: return "Game Boy Advance";
			case Sys.NES: return "NES";
			case Sys.SNES: return "Super Nintendo";
			case Sys.N64: return "Nintendo 64";
			case Sys.GC: return "GameCube";
			}
			return "?";
		}
		public const string Extensions = "*.gb;*.gbc;*.gba;*.nes;*.sfc;*.smc;*.z64;*.n64;*.v64;*.iso;*.gcm;*.dol";
	}

	// PAD_* bits of ne_pad_buttons (the place on an Xbox-style pad)
	static class Pad
	{
		public const uint Up = 1, Down = 2, Left = 4, Right = 8, A = 16, B = 32, X = 64, Y = 128, L = 256, R = 512,
			L2 = 1024, R2 = 2048, Select = 4096, Start = 8192;
	}
}
