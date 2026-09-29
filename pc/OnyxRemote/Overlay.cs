// Overlay.cs -- an Onyx window drawn over the child windows: a top-level window of its own,
// layered (UpdateLayeredWindow: each pixel's transparency heeded as on the Pi, the clicks going
// through the see-through ones), owned by Onyx Remote's window and placed over its MDI area
// where the Pi has it. For the borderless windows above the framed ones: the menu bar's drop-down
// menus (the bar grows over the whole screen while a menu is open, almost clear: a click
// elsewhere closes the menu), the dock, the notifications, the Wi-Fi menu... It never takes the
// focus (the keys stay with the child window, sent to the Pi as ever); the pointer goes to the
// Onyx window (Map: a point of the overlay -> the window's).
using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace OnyxRemote
{
	class Overlay : Form
	{
		public readonly uint Id;
		public Func<Point, Point> Map;
		readonly Connection conn;
		IntPtr memDC, dib, oldBmp, bits;
		int dibW, dibH;
		int[] pm = new int[0];				// (the premultiplied pixels, reused)
		bool fresh;					// new pixels since the last update
		int lx, ly, lsx, lsy, lw, lh, la;		// (the last update's place)

		public Overlay (Connection c, uint id, Form owner)
		{
			conn = c; Id = id;
			FormBorderStyle = FormBorderStyle.None; ShowInTaskbar = false; StartPosition = FormStartPosition.Manual;
			ControlBox = false; MinimizeBox = false; MaximizeBox = false; Text = "";
			Owner = owner;
		}

		protected override CreateParams CreateParams
		{
			get { var cp = base.CreateParams; cp.ExStyle |= WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE; return cp; }
		}
		protected override bool ShowWithoutActivation { get { return true; } }
		protected override void WndProc (ref Message m)
		{
			if (m.Msg == WM_MOUSEACTIVATE) { m.Result = (IntPtr) MA_NOACTIVATE; return; }
			base.WndProc (ref m);
		}

		// ---- the pixels ----
		// An Onyx pixel (0xTTRRGGBB) premultiplied, for a layered window: T the transparency of a
		// see-through window (WIN_FLAG_ALPHA: 0 opaque .. 255 clear, the colour itself not darkened),
		// magenta the clear one of a colour-keyed window (WIN_FLAG_TRANSPARENT); else opaque.
		public static int Premultiply (int c, bool alpha, bool keyed)
		{
			int a = alpha ? 255 - ((c >> 24) & 0xFF) : keyed && (c & 0xFFFFFF) == 0xFF00FF ? 0 : 255;
			if (a == 255) return c | unchecked ((int) 0xFF000000);
			if (a == 0) return 0;
			int r = ((c >> 16) & 0xFF) * a / 255, g = ((c >> 8) & 0xFF) * a / 255, b = (c & 0xFF) * a / 255;
			return a << 24 | r << 16 | g << 8 | b;
		}

		// The window's pixels (w x h, as the model has them).
		public void SetPixels (int[] px, int w, int h, bool alpha, bool keyed)
		{
			if (w <= 0 || h <= 0 || px.Length < w * h) return;
			if (pm.Length < w * h) pm = new int[w * h];
			for (int i = 0; i < w * h; i++) pm[i] = Premultiply (px[i], alpha, keyed);
			SetPremultiplied (pm, w, h);
		}

		// Pixels already premultiplied (w x h).
		public void SetPremultiplied (int[] px, int w, int h)
		{
			if (w <= 0 || h <= 0 || px.Length < w * h) return;
			if (dib == IntPtr.Zero || w != dibW || h != dibH) MakeDib (w, h);
			if (bits != IntPtr.Zero) Marshal.Copy (px, 0, bits, w * h);
			fresh = true;
		}

		// Shown at the screen's (x, y): the part (sx, sy, w, h) of its pixels, the window's own
		// alpha over them (0..255: 0 hides it).
		public void Place (int x, int y, int sx, int sy, int w, int h, int alpha)
		{
			if (dib == IntPtr.Zero || w <= 0 || h <= 0 || alpha <= 0 || sx < 0 || sy < 0 || sx + w > dibW || sy + h > dibH)
			{
				if (Visible) Hide ();
				return;
			}
			if (Visible && !fresh && x == lx && y == ly && sx == lsx && sy == lsy && w == lw && h == lh && alpha == la) return;
			if (!Visible) { Bounds = new Rectangle (x, y, w, h); Show (); }
			fresh = false; lx = x; ly = y; lsx = sx; lsy = sy; lw = w; lh = h; la = alpha;
			IntPtr screen = GetDC (IntPtr.Zero);
			var dst = new POINT { X = x, Y = y }; var size = new SIZE { CX = w, CY = h }; var src = new POINT { X = sx, Y = sy };
			var blend = new BLENDFUNCTION { BlendOp = AC_SRC_OVER, BlendFlags = 0, SourceConstantAlpha = (byte) Math.Min (alpha, 255), AlphaFormat = AC_SRC_ALPHA };
			UpdateLayeredWindow (Handle, screen, ref dst, ref size, memDC, ref src, 0, ref blend, ULW_ALPHA);
			ReleaseDC (IntPtr.Zero, screen);
		}

		// Stacked just below another overlay (the Pi's order).
		public void Below (Overlay above)
		{
			if (above != null && IsHandleCreated && above.IsHandleCreated)
				SetWindowPos (Handle, above.Handle, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
		}

		void MakeDib (int w, int h)
		{
			FreeDib ();
			var bi = new BITMAPINFO ();
			bi.bmiHeader.biSize = Marshal.SizeOf (typeof (BITMAPINFOHEADER));
			bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;	// (top-down)
			bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
			IntPtr screen = GetDC (IntPtr.Zero);
			memDC = CreateCompatibleDC (screen);
			ReleaseDC (IntPtr.Zero, screen);
			dib = CreateDIBSection (memDC, ref bi, 0, out bits, IntPtr.Zero, 0);
			if (dib == IntPtr.Zero) { DeleteDC (memDC); memDC = IntPtr.Zero; bits = IntPtr.Zero; return; }
			oldBmp = SelectObject (memDC, dib);
			dibW = w; dibH = h;
		}

		void FreeDib ()
		{
			if (memDC != IntPtr.Zero) { SelectObject (memDC, oldBmp); DeleteDC (memDC); memDC = IntPtr.Zero; }
			if (dib != IntPtr.Zero) { DeleteObject (dib); dib = IntPtr.Zero; }
			bits = IntPtr.Zero; dibW = dibH = 0;
		}

		protected override void Dispose (bool disposing) { FreeDib (); base.Dispose (disposing); }

		// ---- the pointer: to the Onyx window ----
		void Send (MouseEventArgs e, int wheel)
		{
			if (Map == null) return;
			Point p = Map (e.Location);
			conn.Pointer (Id, p.X, p.Y, RemoteWindow.Buttons (MouseButtons), wheel);
		}
		protected override void OnMouseDown (MouseEventArgs e) { Send (e, 0); }
		protected override void OnMouseUp (MouseEventArgs e) { Send (e, 0); }
		protected override void OnMouseMove (MouseEventArgs e) { Send (e, 0); }
		protected override void OnMouseWheel (MouseEventArgs e) { Send (e, e.Delta > 0 ? 1 : -1); }

		// ---- Win32 ----
		const int WS_EX_LAYERED = 0x80000, WS_EX_TOOLWINDOW = 0x80, WS_EX_NOACTIVATE = 0x08000000;
		const int WM_MOUSEACTIVATE = 0x21, MA_NOACTIVATE = 3, ULW_ALPHA = 2;
		const byte AC_SRC_OVER = 0, AC_SRC_ALPHA = 1;
		const uint SWP_NOSIZE = 1, SWP_NOMOVE = 2, SWP_NOACTIVATE = 0x10, SWP_NOOWNERZORDER = 0x200;
		[StructLayout (LayoutKind.Sequential)] struct POINT { public int X, Y; }
		[StructLayout (LayoutKind.Sequential)] struct SIZE { public int CX, CY; }
		[StructLayout (LayoutKind.Sequential, Pack = 1)] struct BLENDFUNCTION { public byte BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; }
		[StructLayout (LayoutKind.Sequential)]
		struct BITMAPINFOHEADER
		{
			public int biSize, biWidth, biHeight; public short biPlanes, biBitCount;
			public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant;
		}
		[StructLayout (LayoutKind.Sequential)] struct BITMAPINFO { public BITMAPINFOHEADER bmiHeader; public int bmiColors; }
		[DllImport ("user32.dll", SetLastError = true)]
		static extern bool UpdateLayeredWindow (IntPtr hwnd, IntPtr hdcDst, ref POINT pptDst, ref SIZE psize, IntPtr hdcSrc, ref POINT pptSrc, int crKey, ref BLENDFUNCTION pblend, int dwFlags);
		[DllImport ("user32.dll")] static extern bool SetWindowPos (IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y, int cx, int cy, uint uFlags);
		[DllImport ("user32.dll")] static extern IntPtr GetDC (IntPtr hWnd);
		[DllImport ("user32.dll")] static extern int ReleaseDC (IntPtr hWnd, IntPtr hDC);
		[DllImport ("gdi32.dll")] static extern IntPtr CreateCompatibleDC (IntPtr hDC);
		[DllImport ("gdi32.dll")] static extern bool DeleteDC (IntPtr hdc);
		[DllImport ("gdi32.dll")] static extern IntPtr SelectObject (IntPtr hDC, IntPtr hObject);
		[DllImport ("gdi32.dll")] static extern bool DeleteObject (IntPtr hObject);
		[DllImport ("gdi32.dll")] static extern IntPtr CreateDIBSection (IntPtr hdc, ref BITMAPINFO pbmi, uint iUsage, out IntPtr ppvBits, IntPtr hSection, uint dwOffset);
	}
}
