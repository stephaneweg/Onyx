// RemoteWindow.cs -- one Onyx window, as a child of the Onyx Remote window (MDI). By default a
// native child window (its title bar, its close button) showing the Onyx window's content; with
// "Onyx frames": its frame as the app drew it on the Pi (title bar, borders, close box). The
// title bar moves the child inside Onyx Remote (not the Pi's window), the close box closes the
// Onyx app, the rest goes to the app: the pointer in the window's coordinates (rdpd puts it back
// on the Pi's screen), the keys, the focus (the Onyx window is raised and gets the keyboard).
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace OnyxRemote
{
	static class Pix
	{
		// a model's pixels (0x00RRGGBB, w x h) into a bitmap of that size
		public static void Fill (Bitmap b, int[] px, int w, int h)
		{
			if (b == null || w <= 0 || h <= 0 || px.Length < w * h || b.Width < w || b.Height < h) return;
			var d = b.LockBits (new Rectangle (0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppRgb);
			for (int y = 0; y < h; y++) Marshal.Copy (px, y * w, d.Scan0 + y * d.Stride, w);
			b.UnlockBits (d);
		}
		public static Bitmap Make (Bitmap old, int w, int h)
		{
			if (old != null && old.Width == w && old.Height == h) return old;
			old?.Dispose ();
			return w > 0 && h > 0 ? new Bitmap (w, h, PixelFormat.Format32bppRgb) : null;
		}
	}

	class RemoteWindow : Form
	{
		public readonly uint Id;
		readonly Connection conn;
		readonly bool onyxFrames;
		Bitmap content, chromeA, chromeI;
		int w, h, ow, oh, il, it;
		bool frame, native, keys, placed;
		bool dragging; Point dragFrom;
		int buttons;
		public bool GoneOnPi;					// (the Onyx window is gone: really close)

		public RemoteWindow (Connection c, uint id, bool withOnyxFrames, Form parent)
		{
			conn = c; Id = id; onyxFrames = withOnyxFrames;
			MdiParent = parent;
			FormBorderStyle = FormBorderStyle.None;
			StartPosition = FormStartPosition.Manual;
			MaximizeBox = false;
			KeyPreview = true;
			SetStyle (ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.Opaque, true);
			BackColor = Color.Black;
		}

		// From the model (UI thread, under the connection's lock). top: the Pi's line at the top of
		// the MDI area (the menu bar's height: the bar is drawn above the area).
		public void Apply (WinModel m, int top)
		{
			bool sizeChanged = m.W != w || m.H != h || m.OW != ow || m.OH != oh;
			w = m.W; h = m.H; ow = m.OW; oh = m.OH; il = m.IL; it = m.IT;
			bool full = (m.State & WinModel.FULLSCREEN) != 0;
			frame = m.HasFrame && onyxFrames && !full;
			bool wasNative = native;
			native = (m.HasFrame && !onyxFrames) || full;
			if (native && !wasNative) FormBorderStyle = FormBorderStyle.FixedSingle;
			else if (!native && wasNative) FormBorderStyle = FormBorderStyle.None;
			keys = (m.State & WinModel.KEYS) != 0;
			if (Text != m.Title) Text = m.Title;
			bool fresh = sizeChanged || content == null;
			if (fresh)
			{
				content = Pix.Make (content, w, h); chromeA = Pix.Make (chromeA, ow, oh); chromeI = Pix.Make (chromeI, ow, oh);
				ClientSize = new Size (Math.Max (frame ? ow : w, 1), Math.Max (frame ? oh : h, 1));
			}
			if (!placed)						// where the Pi has it, then where it is put
			{
				int bx = native ? (Width - ClientSize.Width) / 2 : 0;
				int by = native ? Height - ClientSize.Height - bx : 0;
				int x = m.X - (frame ? il : 0) - bx, y = m.Y - top - (frame ? it : 0) - by;
				Location = new Point (Math.Max (0, x), Math.Max (0, y));
				placed = true;
			}
			if (m.Dirty || fresh)
			{
				Pix.Fill (content, m.Content, w, h);
				if (frame) { Pix.Fill (chromeA, m.ChromeA, ow, oh); Pix.Fill (chromeI, m.ChromeI, ow, oh); }
				m.Dirty = false;
				Invalidate ();
			}
		}

		protected override void OnPaint (PaintEventArgs e)
		{
			var g = e.Graphics;
			g.CompositingMode = CompositingMode.SourceCopy;
			g.InterpolationMode = InterpolationMode.NearestNeighbor;
			if (frame) { var c = keys ? chromeA : chromeI; if (c != null) g.DrawImageUnscaled (c, 0, 0); }
			if (content != null) g.DrawImageUnscaled (content, frame ? il : 0, frame ? it : 0);
		}

		// ---- the pointer ----
		bool InTitle (Point p) { return frame && p.Y < it; }
		bool InCloseBox (Point p)
		{
			int size = it - 10, x1 = ow - 5, x0 = x1 - size;	// as the kernel's CloseBoxRect
			return frame && p.X >= x0 && p.X <= x1 && p.Y >= 5 && p.Y <= 5 + size;
		}
		public static int Buttons (MouseButtons b) { return ((b & MouseButtons.Left) != 0 ? 1 : 0) | ((b & MouseButtons.Right) != 0 ? 2 : 0) | ((b & MouseButtons.Middle) != 0 ? 4 : 0); }
		void SendPointer (Point p, int b, int wheel)
		{
			conn.Pointer (Id, p.X - (frame ? il : 0), p.Y - (frame ? it : 0), b, wheel);
			buttons = b;
		}
		protected override void OnMouseDown (MouseEventArgs e)
		{
			if (buttons == 0 && InTitle (e.Location))
			{
				if (e.Button == MouseButtons.Left && InCloseBox (e.Location)) conn.CloseWindow (Id);
				else if (e.Button == MouseButtons.Left) { dragging = true; dragFrom = e.Location; }
				Activate ();
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
				if (sym != 0 && !KeyMap.IsModifier (k)) { conn.Key (true, false, sym); return true; }
			}
			return base.ProcessCmdKey (ref msg, keyData);
		}
		protected override void OnKeyDown (KeyEventArgs e)
		{
			if (KeyMap.IsModifier (e.KeyCode)) conn.Key (true, false, KeyMap.Special (e.KeyCode));
			else { uint hk = KeyMap.Held (e.KeyCode); if (hk != 0) conn.Key (true, true, hk); }
			if (e.Alt && e.KeyCode == Keys.F4) return;
			e.Handled = true;
		}
		protected override void OnKeyUp (KeyEventArgs e)
		{
			uint s = KeyMap.Special (e.KeyCode);
			if (s != 0) conn.Key (false, false, s);
			else { uint hk = KeyMap.Held (e.KeyCode); if (hk != 0) conn.Key (false, true, hk); }
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
		protected override void OnFormClosing (FormClosingEventArgs e)
		{
			if (!GoneOnPi && e.CloseReason == CloseReason.UserClosing) { e.Cancel = true; conn.CloseWindow (Id); }	// (its close button: the app closes)
			base.OnFormClosing (e);
		}
		protected override void Dispose (bool disposing)
		{
			if (disposing) { content?.Dispose (); chromeA?.Dispose (); chromeI?.Dispose (); }
			base.Dispose (disposing);
		}
	}
}
