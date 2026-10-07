// Screenshot.cs -- Onyx Remote's captures, saved as PNG: the whole Pi screen (composited here from
// the pixels the client already has, as the Pi composites it: the desktop when "Desktop" is on,
// the windows bottom to top with their frames and their transparency, the menu bar, the dock...),
// or one Onyx window (its Onyx frame included, the rounded corners see-through). The default
// folder is the user's Pictures\Onyx, the name Onyx-YYYYMMDD-HHMMSS.png. Toast: the short
// confirmation of a quick save (a click on it shows the file in Explorer).
// MIT licence (Onyx).
using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace OnyxRemote
{
	static class Screenshot
	{
		public static string Folder { get { return Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.MyPictures), "Onyx"); } }

		// a fresh file name in that folder (Onyx-YYYYMMDD-HHMMSS.png; -2, -3... in the same second)
		public static string NewPath (string folder, string suffix = "")
		{
			string stem = "Onyx-" + DateTime.Now.ToString ("yyyyMMdd-HHmmss") + suffix;
			string p = Path.Combine (folder, stem + ".png");
			for (int i = 2; File.Exists (p); i++) p = Path.Combine (folder, stem + "-" + i + ".png");
			return p;
		}

		// src (0xAARRGGBB, A 255 opaque) over dst (opaque 0x..RRGGBB) -> opaque
		static int Over (int dst, int src)
		{
			int a = (src >> 24) & 0xFF;
			if (a == 255) return src;
			if (a == 0) return dst;
			int r = (((src >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * (255 - a)) / 255;
			int g = (((src >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * (255 - a)) / 255;
			int b = ((src & 0xFF) * a + (dst & 0xFF) * (255 - a)) / 255;
			return unchecked ((int) 0xFF000000) | r << 16 | g << 8 | b;
		}

		// src (straight alpha) over dst (straight alpha) -> straight alpha (a window over its frame)
		static int OverArgb (int dst, int src)
		{
			int sa = (src >> 24) & 0xFF, da = (dst >> 24) & 0xFF;
			if (sa == 255 || da == 0) return src;
			if (sa == 0) return dst;
			int oa = sa + da * (255 - sa) / 255;
			int Mix (int sh) { return ((((src >> sh) & 0xFF) * sa + ((dst >> sh) & 0xFF) * da * (255 - sa) / 255) / oa) & 0xFF; }
			return oa << 24 | Mix (16) << 16 | Mix (8) << 8 | Mix (0);
		}

		// an Onyx pixel (0xTTRRGGBB) -> 0xAARRGGBB: T the transparency of an ALPHA window (and of a
		// frame: its corners), magenta the clear colour of a colour-keyed one; else opaque
		static int Argb (int c, bool alpha, bool keyed, int windowAlpha = 255)
		{
			int a = alpha ? 255 - ((c >> 24) & 0xFF) : keyed && (c & 0xFFFFFF) == 0xFF00FF ? 0 : 255;
			if (windowAlpha < 255) a = a * windowAlpha / 255;
			return a << 24 | (c & 0xFFFFFF);
		}

		// px (w x h) at (x, y) over the screen (sw x sh)
		static void Blit (int[] scr, int sw, int sh, int[] px, int x, int y, int w, int h, bool alpha, bool keyed, int windowAlpha)
		{
			if (px == null || w <= 0 || h <= 0 || px.Length < w * h || windowAlpha <= 0) return;
			int x0 = Math.Max (0, x), y0 = Math.Max (0, y), x1 = Math.Min (sw, x + w), y1 = Math.Min (sh, y + h);
			for (int yy = y0; yy < y1; yy++)
			{
				int s = (yy - y) * w - x, d = yy * sw;
				for (int xx = x0; xx < x1; xx++) scr[d + xx] = Over (scr[d + xx], Argb (px[s + xx], alpha, keyed, windowAlpha));
			}
		}

		// The whole Pi screen, from the model (the caller holds the connection's lock).
		public static Bitmap Screen (Connection c)
		{
			int sw = c.ScreenW, sh = c.ScreenH;
			if (sw <= 0 || sh <= 0) return null;
			var scr = new int[sw * sh];
			int back = unchecked ((int) 0xFF10121C);		// (the desktop view's colour without "Desktop")
			for (int i = 0; i < scr.Length; i++) scr[i] = back;
			bool desk = c.Windows.TryGetValue (WinModel.DESKTOP_ID, out WinModel d) && d.W > 0 && d.H > 0 && d.Content.Length >= d.W * d.H;
			if (desk) Blit (scr, sw, sh, d.Content, 0, 0, d.W, d.H, false, false, 255);
			foreach (uint id in c.ZOrder)				// bottom to top
			{
				if (id == WinModel.DESKTOP_ID || !c.Windows.TryGetValue (id, out WinModel m)) continue;
				if (m.W <= 0 || m.H <= 0 || m.Hidden) continue;
				if ((m.Flags & WinModel.BACKMOST) != 0 && desk) continue;	// (in the desktop's picture)
				bool full = (m.State & WinModel.FULLSCREEN) != 0;
				if (m.HasFrame && !full)
				{
					var chrome = (m.State & WinModel.KEYS) != 0 ? m.ChromeA : m.ChromeI;
					Blit (scr, sw, sh, chrome, m.X - m.IL, m.Y - m.IT, m.OW, m.OH, true, false, m.Alpha);
				}
				Blit (scr, sw, sh, m.Content, m.X, m.Y, m.W, m.H, (m.Flags & WinModel.ALPHA) != 0, (m.Flags & WinModel.TRANSPARENT) != 0, m.Alpha);
			}
			var bmp = new Bitmap (sw, sh, PixelFormat.Format32bppRgb);
			Pix.Fill (bmp, scr, sw, sh);
			return bmp;
		}

		// One Onyx window: with its Onyx frame (as the Pi draws it: the rounded corners see-through)
		// when it has one, else its content (the caller holds the connection's lock).
		public static Bitmap Window (WinModel m)
		{
			if (m == null || m.W <= 0 || m.H <= 0 || m.Content.Length < m.W * m.H) return null;
			bool alpha = (m.Flags & WinModel.ALPHA) != 0, keyed = (m.Flags & WinModel.TRANSPARENT) != 0;
			bool framed = m.HasFrame && (m.State & WinModel.FULLSCREEN) == 0;
			var chrome = (m.State & WinModel.KEYS) != 0 ? m.ChromeA : m.ChromeI;
			if (framed && chrome.Length < m.OW * m.OH) framed = false;
			int w = framed ? m.OW : m.W, h = framed ? m.OH : m.H, ox = framed ? m.IL : 0, oy = framed ? m.IT : 0;
			var px = new int[w * h];
			if (framed) for (int i = 0; i < w * h; i++) px[i] = Argb (chrome[i], true, false);
			for (int y = 0; y < m.H; y++)
			{
				int ty = y + oy;
				if (ty < 0 || ty >= h) continue;
				for (int x = 0; x < m.W; x++)
				{
					int tx = x + ox;
					if (tx < 0 || tx >= w) continue;
					px[ty * w + tx] = OverArgb (px[ty * w + tx], Argb (m.Content[y * m.W + x], alpha, keyed));
				}
			}
			var bmp = new Bitmap (w, h, PixelFormat.Format32bppArgb);
			var bd = bmp.LockBits (new Rectangle (0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
			for (int y = 0; y < h; y++) Marshal.Copy (px, y * w, bd.Scan0 + y * bd.Stride, w);
			bmp.UnlockBits (bd);
			return bmp;
		}

		public static void Save (Bitmap b, string path)
		{
			string dir = Path.GetDirectoryName (path);
			if (!string.IsNullOrEmpty (dir)) Directory.CreateDirectory (dir);
			b.Save (path, ImageFormat.Png);
		}

		// Explorer, the file selected
		public static void ShowInExplorer (string path)
		{
			try
			{
				if (File.Exists (path)) Process.Start ("explorer.exe", "/select,\"" + path + "\"");
				else { Directory.CreateDirectory (Folder); Process.Start ("explorer.exe", "\"" + Folder + "\""); }
			}
			catch { }
		}
	}

	// The short confirmation of a quick save: a small dark box at the top of the screen, in its
	// middle (below the full screen's bar), for ~2.5 s; it never takes the focus; a click on it
	// shows the file in Explorer.
	class Toast : Form
	{
		readonly Label text;
		readonly Timer timer = new Timer { Interval = 2500 };
		string path;

		public Toast (Form owner)
		{
			FormBorderStyle = FormBorderStyle.None; ShowInTaskbar = false; StartPosition = FormStartPosition.Manual;
			ControlBox = false; MinimizeBox = false; MaximizeBox = false; Text = "";
			TopMost = true; Owner = owner;
			BackColor = FullBarRenderer.Bottom;
			Padding = new Padding (12, 8, 12, 8);
			text = new Label { AutoSize = true, ForeColor = Color.White, Font = new Font ("Segoe UI", 9.5f), Location = new Point (12, 8), Cursor = Cursors.Hand };
			Controls.Add (text);
			Cursor = Cursors.Hand;
			text.Click += (s, e) => Clicked ();
			Click += (s, e) => Clicked ();
			timer.Tick += (s, e) => { timer.Stop (); Hide (); };
		}

		void Clicked () { timer.Stop (); Hide (); if (path != null) Screenshot.ShowInExplorer (path); }

		public void ShowOn (Rectangle area, string message, string file)
		{
			path = file;
			text.Text = message;
			ClientSize = new Size (text.PreferredWidth + 24, text.PreferredHeight + 16);
			Location = new Point (area.Left + (area.Width - Width) / 2, area.Top + 44);
			if (!Visible) Show ();
			timer.Stop (); timer.Start ();
		}

		protected override CreateParams CreateParams
		{
			get { var cp = base.CreateParams; cp.ExStyle |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE; return cp; }
		}
		protected override bool ShowWithoutActivation { get { return true; } }
		protected override void WndProc (ref Message m)
		{
			if (m.Msg == WM_MOUSEACTIVATE) { m.Result = (IntPtr) MA_NOACTIVATE; return; }
			base.WndProc (ref m);
		}
		const int WS_EX_TOOLWINDOW = 0x80, WS_EX_NOACTIVATE = 0x08000000;
		const int WM_MOUSEACTIVATE = 0x21, MA_NOACTIVATE = 3;
	}
}
