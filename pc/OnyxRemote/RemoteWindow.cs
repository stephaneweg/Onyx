// RemoteWindow.cs -- one Onyx window on the PC. By default a native Windows window (its title
// bar, its close button) showing the Onyx window's content; with "Onyx window frames": its frame
// as the app drew it on the Pi (title
// bar, borders, close box) and its content. The title bar moves this PC window (not the Pi's),
// the close box closes the Onyx app, the rest goes to the app: the pointer in the window's
// coordinates (rdpd puts it back on the Pi's screen), the keys, the focus (the Onyx window is
// raised and gets the keyboard).
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace OnyxRemote
{
	// A window without a frame (the menu bar, a bubble...) drawn inside the desktop's window,
	// where it is on the Pi's screen, when the desktop is shown.
	class Overlay
	{
		public uint Id; public Bitmap Bmp; public int X, Y, W, H; public bool Key; public float Alpha = 1f;
	}

	class RemoteWindow : Form
	{
		public List<Overlay> Overlays = new List<Overlay> ();	// (the desktop's window only)
		public readonly uint Id;
		readonly Connection conn;
		Bitmap content, chromeA, chromeI;
		int w, h, ow, oh, il, it;
		bool frame, keys, placed;
		uint flags;
		bool dragging; Point dragFrom;
		bool docked;
		readonly bool desk;

		// ---- the application bar (the Onyx menu bar at the top of the PC's screen) ----
		[StructLayout (LayoutKind.Sequential)]
		struct RECT { public int left, top, right, bottom; }
		[StructLayout (LayoutKind.Sequential)]
		struct APPBARDATA { public int cbSize; public IntPtr hWnd; public uint uCallbackMessage; public uint uEdge; public RECT rc; public IntPtr lParam; }
		[DllImport ("shell32.dll")] static extern IntPtr SHAppBarMessage (uint msg, ref APPBARDATA data);
		const uint ABM_NEW = 0, ABM_REMOVE = 1, ABM_QUERYPOS = 2, ABM_SETPOS = 3, ABE_TOP = 1;

		void DockBar (Point origin, int height)
		{
			var d = new APPBARDATA { cbSize = Marshal.SizeOf (typeof (APPBARDATA)), hWnd = Handle, uEdge = ABE_TOP };
			SHAppBarMessage (ABM_NEW, ref d);
			Rectangle scr = Screen.FromPoint (origin).Bounds;
			d.rc = new RECT { left = scr.Left, top = scr.Top, right = scr.Right, bottom = scr.Top + height };
			SHAppBarMessage (ABM_QUERYPOS, ref d);
			d.rc.bottom = d.rc.top + height;
			SHAppBarMessage (ABM_SETPOS, ref d);
			docked = true;
		}
		void Undock ()
		{
			if (!docked) return;
			var d = new APPBARDATA { cbSize = Marshal.SizeOf (typeof (APPBARDATA)), hWnd = Handle };
			SHAppBarMessage (ABM_REMOVE, ref d);
			docked = false;
		}
		protected override void OnHandleDestroyed (EventArgs e) { Undock (); base.OnHandleDestroyed (e); }
		int buttons;

		readonly bool onyxFrames;			// draw the Onyx frame (else a native one)
		bool native;					// a framed Onyx window shown in a native frame

		public RemoteWindow (Connection c, uint id, bool withOnyxFrames)
		{
			conn = c; Id = id; onyxFrames = withOnyxFrames;
			desk = id == WinModel.DESKTOP_ID;
			// the desktop: a normal PC window (its title, movable), else borderless: the Onyx
			// frame is part of what is drawn
			FormBorderStyle = desk ? FormBorderStyle.FixedSingle : FormBorderStyle.None;
			MaximizeBox = false;
			StartPosition = desk ? FormStartPosition.CenterScreen : FormStartPosition.Manual;
			KeyPreview = true;
			SetStyle (ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.Opaque, true);
			BackColor = Color.Black;
		}

		protected override bool ShowWithoutActivation { get { return !desk && (flags & (WinModel.SYSTEM | WinModel.BORDERLESS)) != 0; } }

		// From the model (UI thread, under the connection's lock).
		public void Apply (WinModel m, Point origin)
		{
			bool sizeChanged = m.W != w || m.H != h || m.OW != ow || m.OH != oh;
			w = m.W; h = m.H; ow = m.OW; oh = m.OH; il = m.IL; it = m.IT; flags = m.Flags;
			frame = m.HasFrame && onyxFrames; keys = (m.State & WinModel.KEYS) != 0;
			bool wasNative = native;
			native = !desk && m.HasFrame && !onyxFrames;
			if (native && !wasNative) { FormBorderStyle = FormBorderStyle.FixedSingle; MaximizeBox = false; }
			else if (!native && wasNative) FormBorderStyle = FormBorderStyle.None;
			if (Text != m.Title) Text = m.Title;
			TopMost = (flags & WinModel.TOPMOST) != 0;
			ShowInTaskbar = desk || (flags & (WinModel.SYSTEM | WinModel.BORDERLESS)) == 0;
			if ((flags & WinModel.TRANSPARENT) != 0) TransparencyKey = Color.FromArgb (255, 0, 255);
			Opacity = m.Alpha >= 255 ? 1.0 : m.Alpha / 255.0;
			int fw = frame ? ow : w, fh = frame ? oh : h;
			bool fresh = sizeChanged || content == null;
			if (fresh)
			{
				Swap (ref content, w, h); Swap (ref chromeA, ow, oh); Swap (ref chromeI, ow, oh);
				ClientSize = new Size (Math.Max (fw, 1), Math.Max (fh, 1));
			}
			// the Pi's place at first; then a framed window stays where it is put on the PC,
			// a borderless one (menu bar, bubbles) follows the Pi
			Point at = new Point (origin.X + m.X - (frame ? il : 0), origin.Y + m.Y - (frame ? it : 0));
			if (desk) placed = true;					// (centred on the PC's screen)
			if (native)							// its client area where the Pi has it
			{
				int bx = (Width - ClientSize.Width) / 2;
				at = new Point (origin.X + m.X - bx, origin.Y + m.Y - (Height - ClientSize.Height - bx));
			}
			if (!placed || !(frame || native)) { if (Location != at) Location = at; placed = true; }
			// the Onyx menu bar (topmost, borderless, at the top): docked at the top of the PC's
			// screen as an application bar -- Windows keeps that strip for it
			if (!docked && (flags & WinModel.TOPMOST) != 0 && !frame && m.Y == 0 && IsHandleCreated) DockBar (origin, h);
			// a window with a title bar is never under the menu bar (the work area starts below it)
			if ((frame || native || desk) && (flags & WinModel.TOPMOST) == 0 && !dragging)
			{
				int top = Screen.FromPoint (Location).WorkingArea.Top;
				if (Top < top) Top = top;
			}
			if (m.Dirty || fresh)					// (fresh: its pixels, even if already seen)
			{
				Fill (content, m.Content, w, h);
				if (frame) { Fill (chromeA, m.ChromeA, ow, oh); Fill (chromeI, m.ChromeI, ow, oh); }
				m.Dirty = false;
				Invalidate ();
			}
		}

		static void Swap (ref Bitmap b, int w, int h)
		{
			b?.Dispose (); b = null;
			if (w > 0 && h > 0) b = new Bitmap (w, h, PixelFormat.Format32bppRgb);
		}

		static void Fill (Bitmap b, int[] px, int w, int h)
		{
			if (b == null || px.Length < w * h) return;
			var d = b.LockBits (new Rectangle (0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppRgb);
			for (int y = 0; y < h; y++) Marshal.Copy (px, y * w, d.Scan0 + y * d.Stride, w);
			b.UnlockBits (d);
		}

		protected override void OnPaint (PaintEventArgs e)
		{
			var g = e.Graphics;
			g.CompositingMode = CompositingMode.SourceCopy;
			g.InterpolationMode = InterpolationMode.NearestNeighbor;
			if (frame) { var c = keys ? chromeA : chromeI; if (c != null) g.DrawImageUnscaled (c, 0, 0); }
			if (content != null) g.DrawImageUnscaled (content, frame ? il : 0, frame ? it : 0);
			foreach (var o in Overlays)				// (bottom to top)
			{
				if (o.Bmp == null) continue;
				if (!o.Key && o.Alpha >= 1f) { g.DrawImageUnscaled (o.Bmp, o.X, o.Y); continue; }
				using (var ia = new ImageAttributes ())
				{
					if (o.Key) ia.SetColorKey (Color.FromArgb (255, 0, 255), Color.FromArgb (255, 0, 255));
					if (o.Alpha < 1f) ia.SetColorMatrix (new ColorMatrix { Matrix33 = o.Alpha });
					g.CompositingMode = CompositingMode.SourceOver;
					g.DrawImage (o.Bmp, new Rectangle (o.X, o.Y, o.W, o.H), 0, 0, o.W, o.H, GraphicsUnit.Pixel, ia);
					g.CompositingMode = CompositingMode.SourceCopy;
				}
			}
		}

		// (the desktop's overlays) a model's pixels into a bitmap of its size
		public static void FillOverlay (Overlay o, WinModel m)
		{
			if (o.Bmp == null || o.W != m.W || o.H != m.H) { o.Bmp?.Dispose (); o.Bmp = new Bitmap (m.W, m.H, PixelFormat.Format32bppRgb); o.W = m.W; o.H = m.H; }
			Fill (o.Bmp, m.Content, m.W, m.H);
		}

		// ---- the pointer ----
		bool InTitle (Point p) { return frame && p.Y < it; }
		bool InCloseBox (Point p)
		{
			int size = it - 10, x1 = ow - 5, x0 = x1 - size;	// as the kernel's CloseBoxRect
			return frame && p.X >= x0 && p.X <= x1 && p.Y >= 5 && p.Y <= 5 + size;
		}
		static int Buttons (MouseButtons b) { return ((b & MouseButtons.Left) != 0 ? 1 : 0) | ((b & MouseButtons.Right) != 0 ? 2 : 0) | ((b & MouseButtons.Middle) != 0 ? 4 : 0); }
		void SendPointer (Point p, int b, int wheel)
		{
			int x = p.X - (frame ? il : 0), y = p.Y - (frame ? it : 0);
			conn.Pointer (Id, x, y, b, wheel);
			buttons = b;
		}

		protected override void OnMouseDown (MouseEventArgs e)
		{
			if (buttons == 0 && InTitle (e.Location))
			{
				if (e.Button == MouseButtons.Left && InCloseBox (e.Location)) conn.CloseWindow (Id);
				else if (e.Button == MouseButtons.Left) { dragging = true; dragFrom = e.Location; }
				return;
			}
			SendPointer (e.Location, Buttons (MouseButtons), 0);
		}
		protected override void OnMouseMove (MouseEventArgs e)
		{
			if (dragging) { Location = new Point (Location.X + e.X - dragFrom.X, Location.Y + e.Y - dragFrom.Y); return; }
			if (buttons != 0 || !InTitle (e.Location)) SendPointer (e.Location, Buttons (MouseButtons), 0);
		}
		protected override void OnMouseUp (MouseEventArgs e)
		{
			if (dragging) { dragging = false; return; }
			SendPointer (e.Location, Buttons (MouseButtons), 0);
		}
		protected override void OnMouseWheel (MouseEventArgs e) { SendPointer (e.Location, Buttons (MouseButtons), e.Delta > 0 ? 1 : -1); }

		// ---- the keyboard ----
		protected override bool ProcessCmdKey (ref Message msg, Keys keyData)
		{
			const int WM_KEYDOWN = 0x100, WM_SYSKEYDOWN = 0x104;
			if (msg.Msg == WM_KEYDOWN || msg.Msg == WM_SYSKEYDOWN)
			{
				Keys k = keyData & Keys.KeyCode;
				uint sym = KeyMap.Special (k);
				if (sym != 0 && !KeyMap.IsModifier (k)) { conn.Key (true, false, sym); return true; }	// (no dialog-key handling)
			}
			return base.ProcessCmdKey (ref msg, keyData);
		}
		protected override void OnKeyDown (KeyEventArgs e)
		{
			if (KeyMap.IsModifier (e.KeyCode)) conn.Key (true, false, KeyMap.Special (e.KeyCode));
			else { uint h = KeyMap.Held (e.KeyCode); if (h != 0) conn.Key (true, true, h); }
			if (e.Alt && e.KeyCode == Keys.F4) return;
			e.Handled = true;
		}
		protected override void OnKeyUp (KeyEventArgs e)
		{
			uint s = KeyMap.Special (e.KeyCode);
			if (s != 0) conn.Key (false, false, s);
			else { uint h = KeyMap.Held (e.KeyCode); if (h != 0) conn.Key (false, true, h); }
			e.Handled = true;
		}
		protected override void OnKeyPress (KeyPressEventArgs e)
		{
			char c = e.KeyChar;
			if (c == '\r' || c == '\b' || c == '\t' || c == (char) 27) return;	// (sent as keys)
			if (c < 256) conn.Char (c);
			e.Handled = true;
		}

		// ---- focus, closing ----
		protected override void OnActivated (EventArgs e) { base.OnActivated (e); conn.Raise (Id); }
		public bool GoneOnPi;
		public event Action DesktopClosed;					// (the Onyx window is gone: really close)
		protected override void OnFormClosing (FormClosingEventArgs e)
		{
			if (!GoneOnPi && desk && e.CloseReason == CloseReason.UserClosing) { e.Cancel = true; DesktopClosed?.Invoke (); }	// (unticks the option)
			else if (!GoneOnPi && e.CloseReason == CloseReason.UserClosing) { e.Cancel = true; conn.CloseWindow (Id); }	// (Alt+F4: the app closes)
			base.OnFormClosing (e);
		}
		protected override void Dispose (bool disposing)
		{
			if (disposing) { content?.Dispose (); chromeA?.Dispose (); chromeI?.Dispose (); foreach (var o in Overlays) o.Bmp?.Dispose (); }
			base.Dispose (disposing);
		}
	}
}
