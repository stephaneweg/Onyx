// Native.cs -- forgecore.dll's interface (core/forgecore.cpp says what each call does). Strings are UTF-8 both ways.
// MIT licence (Onyx).
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Forge
{
	static class Core
	{
		const string D = "forgecore.dll";
		const CallingConvention C = CallingConvention.Cdecl;

		// what fc_poll says changed
		public const int FL_FIELDS = 1, FL_PANEL = 2, FL_UI = 4, FL_VIEW = 8, FL_PRINTFILE = 16;
		// what fc_cmd asks to be shown
		public const int REQ_NONE = 0, REQ_GCODE = 1, REQ_PRINTFILE = 2;

		[DllImport (D, CallingConvention = C)] static extern void fc_init (byte[] cfg, byte[] printers);
		[DllImport (D, CallingConvention = C)] public static extern int fc_lang (byte[] path);
		[DllImport (D, CallingConvention = C)] public static extern int fc_poll ();
		[DllImport (D, CallingConvention = C)] public static extern int fc_gl ();
		[DllImport (D, CallingConvention = C)] public static extern void fc_set_accent (uint rgb);
		[DllImport (D, CallingConvention = C)] public static extern void fc_view_draw (IntPtr px, int w, int h, int stride);
		[DllImport (D, CallingConvention = C)] public static extern int fc_view_mouse (int x, int y, int buttons, int wheel, int mods);
		[DllImport (D, CallingConvention = C)] public static extern void fc_view_look (double az, double el);
		[DllImport (D, CallingConvention = C)] public static extern void fc_tick ();
		[DllImport (D, CallingConvention = C)] public static extern void fc_key (int k);
		[DllImport (D, CallingConvention = C)] public static extern int fc_cmd (int id);
		[DllImport (D, CallingConvention = C)] public static extern void fc_tool (int t, int toggle);
		[DllImport (D, CallingConvention = C)] static extern void fc_state (int[] s, int n);
		[DllImport (D, CallingConvention = C)] static extern int fc_text (int which, byte[] buf, int cap);
		[DllImport (D, CallingConvention = C)] static extern int fc_form (byte[] buf, int cap);
		[DllImport (D, CallingConvention = C)] static extern void fc_form_text (int id, byte[] text);
		[DllImport (D, CallingConvention = C)] public static extern int fc_form_enter ();
		[DllImport (D, CallingConvention = C)] public static extern void fc_form_check (int tag, int on);
		[DllImport (D, CallingConvention = C)] public static extern void fc_form_seg (int tag, int sel);
		[DllImport (D, CallingConvention = C)] public static extern void fc_form_drop (int tag, int sel);
		[DllImport (D, CallingConvention = C)] public static extern void fc_form_op (int i);
		[DllImport (D, CallingConvention = C)] public static extern void fc_form_colour (int i);
		[DllImport (D, CallingConvention = C)] public static extern void fc_form_origin (int k);
		[DllImport (D, CallingConvention = C)] static extern int fc_list (int which, byte[] buf, int cap);
		[DllImport (D, CallingConvention = C)] public static extern void fc_pick (int which, int i);
		[DllImport (D, CallingConvention = C)] public static extern int fc_tl_count ();
		[DllImport (D, CallingConvention = C)] static extern int fc_tl_item (int i, out int icon, byte[] buf, int cap);
		[DllImport (D, CallingConvention = C)] public static extern int fc_tl_upto ();
		[DllImport (D, CallingConvention = C)] public static extern void fc_tl_set_upto (int k);
		[DllImport (D, CallingConvention = C)] public static extern void fc_tl_pick (int i, int dbl, int right);
		[DllImport (D, CallingConvention = C)] public static extern void fc_icon (int kind, int s, uint ink, uint acc, uint bg, uint[] px);
		[DllImport (D, CallingConvention = C)] public static extern void fc_new ();
		[DllImport (D, CallingConvention = C)] static extern int fc_open (byte[] path);
		[DllImport (D, CallingConvention = C)] static extern int fc_save (byte[] path);
		[DllImport (D, CallingConvention = C)] static extern int fc_canvas_add (byte[] path, byte[] err, int cap);
		[DllImport (D, CallingConvention = C)] static extern void fc_export (int format, int what, int curve, int view, int binary, int hidden, byte[] title, double[] o);
		[DllImport (D, CallingConvention = C)] static extern int fc_export_write (byte[] path);
		[DllImport (D, CallingConvention = C)] public static extern int fc_doc_segs ();
		[DllImport (D, CallingConvention = C)] static extern int fc_gcode (byte[] checks, int cap, byte[] sum, int scap);
		[DllImport (D, CallingConvention = C)] static extern int fc_gcode_text (byte[] buf, int cap);
		[DllImport (D, CallingConvention = C)] static extern int fc_gcode_write (byte[] path);
		[DllImport (D, CallingConvention = C)] static extern int fc_printfile (byte[] checks, int cap, byte[] sum, int scap, byte[] ext, int ecap);
		[DllImport (D, CallingConvention = C)] static extern int fc_printfile_write (byte[] path);
		[DllImport (D, CallingConvention = C)] static extern void fc_set_hint (byte[] s);

		static byte[] U (string s) => Encoding.UTF8.GetBytes ((s ?? "") + "\0");
		static string S (byte[] b, int n) => Encoding.UTF8.GetString (b, 0, Math.Max (0, Math.Min (n, b.Length - 1)));
		// a string the core writes into a buffer: again, larger, when it did not fit
		static string Get (Func<byte[], int, int> f, int size = 4096)
		{
			var b = new byte[size]; int n = f (b, b.Length);
			if (n >= b.Length) { b = new byte[n + 1]; n = f (b, b.Length); }
			return S (b, n);
		}

		public static void Init (string cfg, string printers) => fc_init (U (cfg), U (printers));
		public static State State ()
		{
			var s = new int[32]; fc_state (s, s.Length); return new State (s);
		}
		public static string Hint => Get ((b, n) => fc_text (0, b, n));
		public static string Path => Get ((b, n) => fc_text (1, b, n));
		public static string Outlines => Get ((b, n) => fc_text (2, b, n));
		public static string Form () => Get ((b, n) => fc_form (b, n), 65536);
		public static void FormText (int id, string text) => fc_form_text (id, U (text));
		public static string[] List (int which) => Get ((b, n) => fc_list (which, b, n)).Split (new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries);
		public static int TlItem (int i, out int icon, out string text)
		{
			var b = new byte[512]; int ic = 0; int fl = fc_tl_item (i, out ic, b, b.Length);
			icon = ic; int n = Array.IndexOf (b, (byte) 0); text = S (b, n < 0 ? b.Length : n); return fl;
		}
		public static bool Open (string path) => fc_open (U (path)) == 0;
		public static bool Save (string path) => fc_save (U (path)) == 0;
		public static string CanvasAdd (string path) => Get ((b, n) => fc_canvas_add (U (path), b, n));
		public static double[] Export (int format, int what, int curve, int view, bool binary, bool hidden, string title)
		{
			var o = new double[8]; fc_export (format, what, curve, view, binary ? 1 : 0, hidden ? 1 : 0, U (title), o); return o;
		}
		public static bool ExportWrite (string path) => fc_export_write (U (path)) == 0;
		public static bool Gcode (out string checks, out string sum)
		{
			var c = new byte[4096]; var s = new byte[1024]; int ok = fc_gcode (c, c.Length, s, s.Length);
			checks = S (c, Array.IndexOf (c, (byte) 0)); sum = S (s, Array.IndexOf (s, (byte) 0)); return ok != 0;
		}
		public static string GcodeText => Get ((b, n) => fc_gcode_text (b, n), 1 << 20);
		public static bool GcodeWrite (string path) => fc_gcode_write (U (path)) == 0;
		public static bool PrintFile (out string checks, out string sum, out string ext)
		{
			var c = new byte[4096]; var s = new byte[1024]; var e = new byte[32]; int ok = fc_printfile (c, c.Length, s, s.Length, e, e.Length);
			checks = S (c, Array.IndexOf (c, (byte) 0)); sum = S (s, Array.IndexOf (s, (byte) 0)); ext = S (e, Array.IndexOf (e, (byte) 0)); return ok != 0;
		}
		public static bool PrintFileWrite (string path) => fc_printfile_write (U (path)) == 0;
		public static void SetHint (string s) => fc_set_hint (U (s));
	}

	// fc_state's numbers, by name
	class State
	{
		readonly int[] s;
		public State (int[] v) { s = v; }
		public int Tool => s[0]; public bool Sketching => s[1] != 0; public bool CamMode => s[2] != 0; public int Gen => s[3];
		public int CamPage => s[4]; public int CamSel => s[5]; public bool CamSim => s[6] != 0; public bool CamDirty => s[7] != 0;
		public int Undo => s[8]; public int Redo => s[9]; public bool Edges => s[10] != 0; public bool Grid => s[11] != 0;
		public bool SeeThrough => s[12] != 0; public bool Snap => s[13] != 0; public int Tris => s[14]; public bool Changed => s[15] != 0;
		public int CamOpKind => s[16]; public int LastShape => s[17]; public bool HasPend => s[18] != 0; public int Bodies => s[19];
		public int Feats => s[20]; public int Upto => s[21]; public int SelFeat => s[22]; public int SelBody => s[23]; public int Step => s[24];
		public int SelCanvas => s[25]; public int Threads => s[26]; public bool Sliced => s[27] != 0; public int SelEl => s[28];
		public bool Resin => CamMode && Gen == 1; public bool Filament => CamMode && Gen == 2; public bool Printer => CamMode && Gen >= 1;
		public string Mode => Sketching ? "sketch" : CamMode ? "cam" + Gen : "design";
	}

	// The core's numbers: tools, icons, commands (fview.h, ficons.h, forgecore.cpp)
	static class T
	{
		public const int SELECT = 0, BOX = 1, CYL = 2, SKETCH = 3, EXTRUDE = 4, FILLET = 5, CHAMFER = 6, MOVE = 7, UNION = 8, SUB = 9, INT = 10, MEASURE = 11,
			PYRAMID = 12, PRISM = 13, TAPER = 14, TORUS = 15, SPHERE = 16, LINE = 17, RECT = 18, CIRCLE = 19, ARC = 20, ARC3 = 21, SPLINE = 22, POINT = 23;
		public static bool Shape (int t) => t == BOX || t == CYL || (t >= PYRAMID && t <= SPHERE);
	}
	static class I
	{
		public const int BOX = 0, CYL = 1, SKETCH = 2, EXTRUDE = 3, FILLET = 4, CHAMFER = 5, MOVE = 6, UNION = 7, SUB = 8, INT = 9, NEWBODY = 10, MEASURE = 11, EXPORT = 12,
			LINE = 13, RECT = 14, CIRCLE = 15, ARC = 16, CLOSE = 17, HOME = 18, FIT = 19, SHADED = 20, CHECK = 21, INFO = 22, EYE = 23, WARN = 24, COMBINE = 25,
			PYRAMID = 26, PRISM = 27, TAPER = 28, TORUS = 29, SPHERE = 30, SHAPES = 31,
			CAM_SETUP = 32, CAM_TOOL = 33, CAM_CLEAR = 34, CAM_CONTOUR = 35, GCODE = 36, PLAY = 37, ARC3 = 38, SPLINE = 39, POINT = 40,
			PR_PLATE = 41, PR_RESIN = 42, PR_SUP = 43, PR_LAYERS = 44, PR_FIL = 45;
	}
	static class Cmd
	{
		public const int NEW = 100, OPEN = 101, SAVE = 102, UNDO = 103, REDO = 104, EXPORT = 105, SK_CANCEL = 106, SK_FINISH = 107, SK_CLOSE = 108, OK = 109, CANCEL = 110,
			DELETE = 111, EDIT_SKETCH = 112, ROLL_HERE = 113, ROLL_END = 114, DEL_ELEMENT = 115, SHAPES = 116, SK_START = 117,
			MODE_DESIGN = 118, MODE_CAM = 119, CAM_SETUP = 120, CAM_TOOL = 121, CAM_CLEAR = 122, CAM_CONTOUR = 123, CAM_SIM = 124, GCODE = 125, CAM_BODY = 126, CAM_DELETE = 127,
			CAM_VALIDATE = 128, CAM_REVERT = 129, CANVAS_IMPORT = 130, CANVAS_DELETE = 131, PR_SETUP = 132, PR_RESIN = 133, PR_SUPPORTS = 134, PR_LAYERS = 135, PR_FILE = 136,
			PR_GENERATE = 137, PR_CLEAR = 138, PR_PRESET = 139, GEN = 140,
			HOME = 200, FIT = 201, TOP = 202, FRONT = 203, RIGHT = 204, EDGES = 205, GRID = 206, SEETHROUGH = 207, SNAP = 208, PROC_MILL = 209, PROC_RESIN = 210, PROC_FDM = 211;
	}
}
