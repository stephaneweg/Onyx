// RemoteWindow.cs -- one Onyx window, a child window of Onyx Remote's desktop view (the Pi's screen
// at its size, in a scrolling area), where the Pi has it. By default a
// native child window (its title bar, its close button) showing the Onyx window's content; with
// "Onyx frames": its frame as the app drew it on the Pi (title bar, borders, the title buttons,
// the rounded corners). The title bar moves the child inside Onyx Remote (not the Pi's window),
// the close button closes the Onyx app, the other title buttons (the window menu, minimise,
// maximise) are pressed on the Pi; the rest goes to the app: the pointer in the window's
// coordinates (rdpd puts it back on the Pi's screen), the keys, the focus (the Onyx window is
// raised and gets the keyboard). Moved on the Pi (dragged there, maximised), it follows; dragged here, the
// Pi's window is put at the same place (rdpd's MOVE, protocol 2: an older rdpd leaves it where it is).
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
		// a model's pixels (0xTTRRGGBB, w x h; the top byte ignored) into a bitmap of that size
		public static void Fill (Bitmap b, int[] px, int w, int h)
		{
			if (b == null || w <= 0 || h <= 0 || px.Length < w * h || b.Width < w || b.Height < h) return;
			var d = b.LockBits (new Rectangle (0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppRgb);
			for (int y = 0; y < h; y++) Marshal.Copy (px, y * w, d.Scan0 + y * d.Stride, w);
			b.UnlockBits (d);
		}
		public static Bitmap Make (Bitmap old, int w, int h, PixelFormat f = PixelFormat.Format32bppRgb)
		{
			if (old != null && old.Width == w && old.Height == h && old.PixelFormat == f) return old;
			old?.Dispose ();
			return w > 0 && h > 0 ? new Bitmap (w, h, f) : null;
		}
		// ... with each pixel's transparency as the bitmap's alpha (a Format32bppArgb one: a
		// WIN_FLAG_ALPHA window's top byte, a WIN_FLAG_TRANSPARENT one's magenta; else opaque)
		public static void FillArgb (Bitmap b, int[] px, int w, int h, bool alpha, bool keyed)
		{
			if (b == null || w <= 0 || h <= 0 || px.Length < w * h || b.Width < w || b.Height < h) return;
			var row = new int[w];
			var d = b.LockBits (new Rectangle (0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
			for (int y = 0; y < h; y++)
			{
				for (int x = 0; x < w; x++)
				{
					int c = px[y * w + x];
					int a = alpha ? 255 - ((c >> 24) & 0xFF) : keyed && (c & 0xFFFFFF) == 0xFF00FF ? 0 : 255;
					row[x] = a << 24 | (c & 0xFFFFFF);
				}
				Marshal.Copy (row, 0, d.Scan0 + y * d.Stride, w);
			}
			b.UnlockBits (d);
		}
	}

	class RemoteWindow : Form
	{
		public readonly uint Id;
		readonly Connection conn;
		readonly bool onyxFrames;
		Bitmap content, chromeA, chromeI;
		int w, h, ow, oh, il, it, piX, piY, viewTop;
		bool frame, native, keys, placed;
		bool dragging; Point dragFrom;
		int buttons;
		public bool GoneOnPi;					// (the Onyx window is gone: really close)

		public RemoteWindow (Connection c, uint id, bool withOnyxFrames, Control desk)
		{
			conn = c; Id = id; onyxFrames = withOnyxFrames;
			TopLevel = false;					// (a child window of the desktop view)
			desk.Controls.Add (this);
			FormBorderStyle = FormBorderStyle.None;
			StartPosition = FormStartPosition.Manual;
			MaximizeBox = false;
			KeyPreview = true;
			SetStyle (ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.Opaque, true);
			BackColor = Color.Black;
		}

		// From the model (UI thread, under the connection's lock). top: the Pi's line at the top of
		// the desktop view (0: the view is the whole Pi screen).
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
				var was = Region;
				Region = frame ? Rounded (ow, oh, KAPI_FRAME_RADIUS) : null;
				was?.Dispose ();
			}
			if (placed && (m.X != piX || m.Y != piY)) placed = false;	// (moved on the Pi: follows)
			piX = m.X; piY = m.Y; viewTop = top;
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

		// the frame's rounded shape (its corners' outside see-through on the Pi)
		static Region Rounded (int w, int h, int r)
		{
			var p = new GraphicsPath ();
			int d = 2 * r;
			p.AddArc (0, 0, d, d, 180, 90); p.AddArc (w - d, 0, d, d, 270, 90);
			p.AddArc (w - d, h - d, d, d, 0, 90); p.AddArc (0, h - d, d, d, 90, 90);
			p.CloseFigure ();
			var g = new Region (p);
			p.Dispose ();
			return g;
		}

		// ---- the pointer ----
		// the title buttons as the kernel places them (kapi_abi.h KAPI_FRAME_*)
		const int KAPI_FRAME_RADIUS = 8, BTN_W = 22, BTN_H = 19, BTN_Y = 5, BTN_EDGE = 6, BTN_STEP = 25;
		const int FRAME_MENU = 0, FRAME_CLOSE = 1, FRAME_MAXIMISE = 2, FRAME_MINIMISE = 3;
		bool InTitle (Point p) { return frame && p.Y < it; }
		int TitleButton (Point p)				// -> FRAME_*, -1: none
		{
			if (!frame || p.Y < BTN_Y || p.Y >= BTN_Y + BTN_H) return -1;
			for (int b = 0; b < 4; b++)
			{
				int bx = b == FRAME_MENU ? BTN_EDGE : ow - BTN_EDGE - BTN_W - (b == FRAME_CLOSE ? 0 : b == FRAME_MAXIMISE ? 1 : 2) * BTN_STEP;
				if (p.X >= bx && p.X < bx + BTN_W) return b;
			}
			return -1;
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
				int b = e.Button == MouseButtons.Left ? TitleButton (e.Location) : -1;
				if (b == FRAME_CLOSE) conn.CloseWindow (Id);
				else if (b >= 0) { SendPointer (e.Location, Buttons (MouseButtons), 0); return; }	// (pressed on the Pi)
				else if (e.Button == MouseButtons.Left) { dragging = true; dragFrom = e.Location; }
				ToFront ();
				return;
			}
			ToFront ();
			SendPointer (e.Location, Buttons (MouseButtons), 0);
		}
		protected override void OnMouseMove (MouseEventArgs e)
		{
			if (dragging) { Location = new Point (Location.X + e.X - dragFrom.X, Location.Y + e.Y - dragFrom.Y); return; }
			if (buttons != 0 || !InTitle (e.Location)) SendPointer (e.Location, Buttons (MouseButtons), 0);
			else conn.Pointer (Id, e.X - il, e.Y - it, 0, 0);	// (over the title bar: the pointer moves on the Pi too)
		}
		protected override void OnMouseUp (MouseEventArgs e)
		{
			if (dragging) { dragging = false; SendPlace (); return; }
			SendPointer (e.Location, Buttons (MouseButtons), 0);
		}
		protected override void OnMouseWheel (MouseEventArgs e) { SendPointer (e.Location, Buttons (MouseButtons), e.Delta > 0 ? 1 : -1); }

		// Dragged here (by the Onyx frame's title bar, or the native one's: ResizeEnd): the Pi's window put at
		// the same place -- the inverse of Apply's placing; what the Pi then reports is that place, so the
		// window stays (and if the Pi moved it elsewhere -- kept on its screen --, it follows that).
		void SendPlace ()
		{
			int bx = native ? (Width - ClientSize.Width) / 2 : 0;
			int by = native ? Height - ClientSize.Height - bx : 0;
			int x = Location.X + bx + (frame ? il : 0), y = Location.Y + by + viewTop + (frame ? it : 0);
			if (x == piX && y == piY) return;
			if (conn.Move (Id, x, y)) { piX = x; piY = y; }
		}
		protected override void OnResizeEnd (EventArgs e) { base.OnResizeEnd (e); if (native) SendPlace (); }

		// ---- the keyboard ----
		protected override bool ProcessCmdKey (ref Message msg, Keys keyData)
		{
			const int WM_KEYDOWN = 0x100, WM_SYSKEYDOWN = 0x104;
			if (keyData == Keys.F11) return base.ProcessCmdKey (ref msg, keyData);	// (Onyx Remote's: the full screen)
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
		// clicked (or given the keys): in front here, raised on the Pi -- it gets the keyboard there
		public void ToFront ()
		{
			if (!ContainsFocus) Focus ();
			BringToFront ();
			conn.Raise (Id);
		}
		protected override void OnEnter (EventArgs e) { base.OnEnter (e); BringToFront (); conn.Raise (Id); }
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
