// Native.cs -- obcore.dll (the compiler, the VM and the screen, pc/obcore/obcore.cpp) and the
// Latin-1 strings it speaks.
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace OnyxBasic
{
	static class Latin1
	{
		public static readonly Encoding Enc = Encoding.GetEncoding (28591);
		public static string Read (IntPtr p)
		{
			if (p == IntPtr.Zero) return "";
			int n = 0; while (Marshal.ReadByte (p, n) != 0) n++;
			var b = new byte[n]; Marshal.Copy (p, b, 0, n);
			return Enc.GetString (b);
		}
		public static int Write (string s, IntPtr buf, int cap)
		{
			if (cap <= 0) return 0;
			var b = Enc.GetBytes (s ?? "");
			int n = Math.Min (b.Length, cap - 1);
			Marshal.Copy (b, 0, buf, n); Marshal.WriteByte (buf, n, 0);
			return n;
		}
		public static byte[] Z (string s) { var b = Enc.GetBytes (s ?? ""); Array.Resize (ref b, b.Length + 1); return b; }
		public static string FromZ (byte[] b) { int n = Array.IndexOf (b, (byte) 0); return Enc.GetString (b, 0, n < 0 ? b.Length : n); }
	}

	// Key codes (Onyx's KEY_*, basscreen.h's K_*).
	static class K
	{
		public const int Backspace = 8, Tab = 9, Enter = 13, Esc = 27, Up = 0x100, Down = 0x101, Left = 0x102, Right = 0x103,
			Home = 0x104, End = 0x105, PgUp = 0x106, PgDn = 0x107, Del = 0x108, F1 = 0x110;
	}

	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate int OpenWindowFn (int w, int h, IntPtr title);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate void ResizeFn (int w, int h);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate void PresentFn (IntPtr px, int w, int h, int sx, int sy);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate int ControlFn (int id, int kind, int x, int y, int w, int h, IntPtr text, int val);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate void SetTextFn (int id, IntPtr s);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate int GetTextFn (int id, IntPtr buf, int cap);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate int GetValueFn (int id);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate void SetValueFn (int id, int v);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate void NotifyFn (IntPtr title, IntPtr text);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate int MsgBoxFn (IntPtr title, IntPtr text, int buttons);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate int ClipboardFn (IntPtr buf, int cap);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate void SetClipboardFn (IntPtr s);
	[UnmanagedFunctionPointer (CallingConvention.Cdecl)] delegate int FileDialogFn (int save, IntPtr dir, IntPtr name, IntPtr outp, int cap);

	[StructLayout (LayoutKind.Sequential)]
	struct ObCallbacks
	{
		public IntPtr openWindow, resizeWindow, present, control, setText, getText, getValue, setValue,
			notify, msgbox, clipboard, setClipboard, fileDialog;
	}

	static class Native
	{
		const string Dll = "obcore.dll";
		[DllImport (Dll, CallingConvention = CallingConvention.Cdecl)] public static extern int ob_check (byte[] src, out int line, byte[] msg, int cap);
		[DllImport (Dll, CallingConvention = CallingConvention.Cdecl)] public static extern int ob_words (byte[] buf, int cap);
		[DllImport (Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
		public static extern int ob_run (byte[] src, string sd, string cwd, byte[] args, byte[] title, ref ObCallbacks cb, out int line, byte[] msg, int cap);
		[DllImport (Dll, CallingConvention = CallingConvention.Cdecl)] public static extern void ob_key (int k);
		[DllImport (Dll, CallingConvention = CallingConvention.Cdecl)] public static extern void ob_keyheld (int k, int down);
		[DllImport (Dll, CallingConvention = CallingConvention.Cdecl)] public static extern void ob_mouse (int x, int y, int b);
		[DllImport (Dll, CallingConvention = CallingConvention.Cdecl)] public static extern void ob_event (int id);
		[DllImport (Dll, CallingConvention = CallingConvention.Cdecl)] public static extern void ob_stop ();

		// Compile only: 0 = fine, else the line (1-based) and the message.
		public static int Check (string src, out string msg)
		{
			var m = new byte[200];
			int r = ob_check (Latin1.Z (src), out int line, m, m.Length);
			msg = Latin1.FromZ (m);
			return r == 0 ? 0 : Math.Max (line, 1);
		}
		public static string[] Words ()
		{
			var b = new byte[16384];
			int n = ob_words (b, b.Length);
			return Latin1.Enc.GetString (b, 0, n).Split (new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
		}
	}
}
