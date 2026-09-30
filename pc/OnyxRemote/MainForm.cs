// MainForm.cs -- Onyx Remote: one window holding the Onyx session. At the top a tool bar (the
// Pi's address, the options, Connect / Disconnect, Full screen, the updates a second); below, a
// scrolling area holding the Pi's screen at its size (DeskView: scroll bars when the window is
// smaller): the Onyx menu bar at its top, the Onyx windows as its child windows where they are on
// the Pi, over the Onyx desktop (the wallpaper and the widgets below the windows) when "Desktop"
// is on. The borderless windows above them -- the menu bar's drop-down menus, the
// dock, the notifications, the Wi-Fi menu -- are drawn over the child windows, see-through
// where they are on the Pi (Overlay.cs: layered windows). A minimised window, or one on another
// workspace, is not shown (as on the Pi).
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Windows.Forms;

namespace OnyxRemote
{
	// The Onyx menu bar: the Pi's bar (1024 wide) stretched to the width given -- the empty middle
	// (its widest run of identical columns) widened; the pointer mapped back to the Pi's bar.
	class BarView
	{
		public Bitmap Bmp; public int W, H, BarH = 32, Split = -1;
		public uint Id, Flags; public bool Present;
		int[] px;
		public void Update (WinModel m)
		{
			Id = m.Id; Flags = m.Flags; Present = true;
			if (Bmp == null || W != m.W || H != m.H) { Bmp = Pix.Make (Bmp, m.W, m.H); W = m.W; H = m.H; }
			if (H < BarH) BarH = H;
			Pix.Fill (Bmp, m.Content, W, H);
			px = m.Content;
			FindSplit ();
		}
		void FindSplit ()
		{
			Split = -1;
			int H2 = Math.Min (BarH, H);
			if (px == null || W < 64 || px.Length < W * H2) return;
			bool Same (int a, int b) { for (int y = 0; y < H2; y++) if (px[y * W + a] != px[y * W + b]) return false; return true; }
			int best = 0, bestAt = -1;
			for (int x = W - 2; x > 0; )
			{
				int end = x + 1;
				while (x > 0 && Same (x, x + 1)) x--;
				int run = end - x;
				if (run > best) { best = run; bestAt = x + 1; }
				x--;
			}
			if (best >= 24) Split = bestAt + best / 2;
		}
		// the bar's rows y0..y1 (the bar itself) into a width destW at dy (the middle stretched)
		public void Draw (Graphics g, int destW, int y0, int y1, int dy)
		{
			if (Bmp == null || y1 <= y0) return;
			int gap = Math.Max (0, destW - W);
			if (Split <= 0 || gap == 0) { g.DrawImage (Bmp, new Rectangle (0, dy, W, y1 - y0), new Rectangle (0, y0, W, y1 - y0), GraphicsUnit.Pixel); return; }
			g.DrawImage (Bmp, new Rectangle (0, dy, Split, y1 - y0), new Rectangle (0, y0, Split, y1 - y0), GraphicsUnit.Pixel);
			g.DrawImage (Bmp, new Rectangle (Split + gap, dy, W - Split, y1 - y0), new Rectangle (Split, y0, W - Split, y1 - y0), GraphicsUnit.Pixel);
			g.DrawImage (Bmp, new Rectangle (Split, dy, gap, y1 - y0), new Rectangle (Split, y0, 1, y1 - y0), GraphicsUnit.Pixel);
		}
		// the rows below the bar (while a menu, the volume or the calendar is open: the drop-down
		// over the whole screen almost clear -- a click elsewhere closes it), premultiplied for a
		// layered window, destW wide (the stretched middle's gap: almost clear too) -> their number
		public int DropPixels (ref int[] dst, int destW)
		{
			int dh = H - BarH;
			if (px == null || dh <= 0 || destW <= 0 || px.Length < W * H) return 0;
			if (dst.Length < destW * dh) dst = new int[destW * dh];
			int gap = Math.Max (0, destW - W);
			bool split = Split > 0 && gap > 0, alpha = (Flags & WinModel.ALPHA) != 0, keyed = (Flags & WinModel.TRANSPARENT) != 0;
			for (int y = 0; y < dh; y++)
			{
				int srow = (BarH + y) * W, drow = y * destW;
				for (int x = 0; x < destW; x++)
				{
					int sx = !split ? x : x < Split ? x : x >= Split + gap ? x - gap : -1;
					dst[drow + x] = sx >= 0 && sx < W ? Overlay.Premultiply (px[srow + sx], alpha, keyed) : 0x01000000;
				}
			}
			return dh;
		}
		public int MapX (int x, int destW)
		{
			int gap = Math.Max (0, destW - W);
			if (Split <= 0 || gap == 0) return x;
			return x >= Split + gap ? x - gap : Math.Min (x, Split);
		}
	}

	// The Pi's screen at its size (a child of the scrolling area: scroll bars when Onyx Remote's
	// window is smaller): the Onyx desktop's picture (the wallpaper, the widgets) and the menu bar
	// at its top, pixel for pixel; the Onyx windows are its child windows, where the Pi has them.
	class DeskView : Control
	{
		readonly MainForm main;
		public Bitmap Back;					// (the desktop's picture + the widgets; null: none)
		public DeskView (MainForm m)
		{
			main = m;
			SetStyle (ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer, true);
			BackColor = Color.FromArgb (16, 18, 28);
		}
		protected override void OnPaint (PaintEventArgs e)
		{
			var g = e.Graphics;
			g.InterpolationMode = InterpolationMode.NearestNeighbor;
			g.PixelOffsetMode = PixelOffsetMode.Half;
			if (Back != null) g.DrawImageUnscaled (Back, 0, 0);
			else g.Clear (BackColor);
			var b = main.Bar;
			if (b.Present && b.Bmp != null) b.Draw (g, b.W, 0, Math.Min (b.BarH, b.H), 0);
		}
		// the pointer: on the menu bar to it, elsewhere to the desktop (without "Desktop", only
		// its moves: the Pi's pointer follows -- the eyes...)
		void Send (MouseEventArgs e, int wheel)
		{
			var c = main.Conn;
			if (c == null) return;
			var b = main.Bar;
			if (b.Present && e.Y < b.BarH) { c.Pointer (b.Id, e.X, e.Y, RemoteWindow.Buttons (MouseButtons), wheel); return; }
			bool all = main.DesktopShown;
			c.Pointer (WinModel.DESKTOP_ID, e.X, e.Y, all ? RemoteWindow.Buttons (MouseButtons) : 0, all ? wheel : 0);
		}
		protected override void OnMouseDown (MouseEventArgs e) { Send (e, 0); }
		protected override void OnMouseUp (MouseEventArgs e) { Send (e, 0); }
		protected override void OnMouseMove (MouseEventArgs e) { Send (e, 0); }
	}

	// The scrolling area: stays where it is scrolled when a child takes the focus (a click on the
	// desktop, a window raised) -- a Panel scrolls the focused control into view, and the desktop
	// view being bigger than the area, that meant back to its top left.
	class ScrollArea : Panel
	{
		protected override Point ScrollToControl (Control activeControl) { return DisplayRectangle.Location; }
	}

	class MainForm : Form
	{
		readonly ToolStripTextBox host = new ToolStripTextBox (), port = new ToolStripTextBox ();
		readonly ToolStripButton bits16 = new ToolStripButton ("16-bit colours") { CheckOnClick = true };
		readonly ToolStripButton desktop = new ToolStripButton ("Desktop") { CheckOnClick = true };
		readonly ToolStripButton frames = new ToolStripButton ("Onyx frames") { CheckOnClick = true };
		readonly ToolStripButton go = new ToolStripButton ("Connect");
		readonly ToolStripButton console = new ToolStripButton ("Console") { ToolTipText = "A telnet console on the Pi (the Onyx shell; telnetd, port 23)" };
		readonly ToolStripButton full = new ToolStripButton ("Full screen") { CheckOnClick = true, ToolTipText = "The Onyx session over the whole screen, without this tool bar (F11 toggles it)" };
		readonly ToolStripLabel status = new ToolStripLabel ("Not connected");
		readonly ToolStrip ts;
		readonly ScrollArea scroller;			// (the scrolling area)
		readonly DeskView desk;					// (the Pi's screen in it)
		bool isFull; Rectangle fullRestore; FormBorderStyle fullBorder; bool fullMaxBox; FormWindowState fullState;	// (before the full screen)
		FullBar fullBar; readonly Timer fullTimer = new Timer { Interval = 100 }; DateTime fullAway;	// (the full screen's bar)
		public Connection Conn;
		public readonly BarView Bar = new BarView ();
		public bool DesktopShown { get { return desktop.Checked; } }
		readonly Dictionary<uint, RemoteWindow> wins = new Dictionary<uint, RemoteWindow> ();
		List<RemoteWindow> winOrder = new List<RemoteWindow> ();	// (their order, bottom to top)
		Bitmap deskBmp, backBmp; readonly Dictionary<uint, Bitmap> bubbles = new Dictionary<uint, Bitmap> ();
		readonly Dictionary<uint, Overlay> overlays = new Dictionary<uint, Overlay> ();	// (over the child windows)
		Overlay barDrop; int[] dropPx = new int[0];			// (the bar's drop-downs)
		List<Overlay> stacked = new List<Overlay> ();					// (their order, bottom to top)
		int rounds; DateTime since = DateTime.Now;
		static readonly string SettingsPath = Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.ApplicationData), "OnyxRemote.txt");

		public MainForm ()
		{
			Text = "Onyx Remote";
			Font = new Font ("Segoe UI", 9f);
			ClientSize = new Size (1040, 830);
			ts = new ToolStrip { GripStyle = ToolStripGripStyle.Hidden, Dock = DockStyle.Top };
			host.Width = 130; port.Width = 45; port.Text = "3390";
			ts.Items.AddRange (new ToolStripItem[] { new ToolStripLabel ("Onyx:"), host, port, new ToolStripSeparator (), go, console,
				new ToolStripSeparator (), bits16, desktop, frames, full, new ToolStripSeparator (), status });
			scroller = new ScrollArea { Dock = DockStyle.Fill, AutoScroll = true, BackColor = Color.Black };
			desk = new DeskView (this) { Location = Point.Empty, Size = new Size (1024, 768), Visible = false };
			scroller.Controls.Add (desk);
			Controls.Add (scroller);
			Controls.Add (ts);
			go.Click += (s, e) => { if (Conn == null) Connect (); else Disconnect ("Disconnected"); };
			console.Click += (s, e) => OpenConsole ();
			full.CheckedChanged += (s, e) => { SaveSettings (); if (Conn != null) FullScreen (full.Checked); };
			desktop.CheckedChanged += (s, e) => { if (Conn != null) { Conn.Desktop (desktop.Checked); Sync (); } };
			host.KeyDown += (s, e) => { if (e.KeyCode == Keys.Enter && Conn == null) { e.SuppressKeyPress = true; Connect (); } };
			Move += (s, e) => Sync ();					// (the overlays follow)
			Resize += (s, e) => Sync ();
			scroller.Scroll += (s, e) => Sync ();
			desk.Move += (s, e) => Sync ();				// (scrolled: by the wheel too)
			scroller.Layout += (s, e) => CenterDesk ();
			desk.Resize += (s, e) => CenterDesk ();
			fullTimer.Tick += (s, e) => FullBarTick ();
			try
			{
				foreach (var line in File.ReadAllLines (SettingsPath))
				{
					int eq = line.IndexOf ('=');
					if (eq < 0) continue;
					string k = line.Substring (0, eq), v = line.Substring (eq + 1);
					if (k == "host") host.Text = v;
					else if (k == "port") port.Text = v;
					else if (k == "bits16") bits16.Checked = v == "1";
					else if (k == "desktop") desktop.Checked = v == "1";
					else if (k == "frames") frames.Checked = v == "1";
					else if (k == "fullscreen") full.Checked = v == "1";
				}
			}
			catch { }
			var tick = new Timer { Interval = 1000 };
			tick.Tick += (s, e) =>
			{
				if (Conn == null) return;
				double sec = (DateTime.Now - since).TotalSeconds;
				if (Conn.KernelAbi < 56) status.Text = "The Onyx kernel is too old (kapi v" + Conn.KernelAbi + ", rdpd needs v56)";
				else status.Text = string.Format ("{0}: {1} windows, {2:0.0} updates / s", host.Text, wins.Count, rounds / Math.Max (sec, 0.001));
				rounds = 0; since = DateTime.Now;
			};
			tick.Start ();
		}

		// a telnet console on the Pi: its own window (the address typed, port 23 -- "host:port" for another)
		void OpenConsole ()
		{
			string h = host.Text.Trim (); int p = 23;
			int colon = h.LastIndexOf (':');
			if (colon > 0 && int.TryParse (h.Substring (colon + 1), out int pp)) { p = pp; h = h.Substring (0, colon); }
			if (h.Length == 0) { status.Text = "Type the Pi's address first"; return; }
			SaveSettings ();
			new TelnetForm (h, p).Show ();
		}

		void Connect ()
		{
			int p; if (!int.TryParse (port.Text, out p) || p <= 0 || p > 65535) p = 3390;
			SaveSettings ();
			var c = new Connection ();
			status.Text = "Connecting...";
			Refresh ();
			try { c.Open (host.Text.Trim (), p, bits16.Checked, desktop.Checked, frames.Checked); }
			catch (Exception e) { status.Text = "Cannot connect: " + e.Message; return; }
			Conn = c;
			c.RoundDone += () => BeginInvoke ((Action) (() => { if (Conn != c) return; Sync (); rounds++; c.Ready (); }));
			c.Closed += why => BeginInvoke ((Action) (() => { if (Conn == c) Disconnect (why); }));
			go.Text = "Disconnect";
			scroller.AutoScrollPosition = Point.Empty;
			desk.Location = Point.Empty;
			desk.Size = new Size (Math.Max (1, c.ScreenW), Math.Max (1, c.ScreenH));
			desk.Visible = true;
			FitToPi (c.ScreenW, c.ScreenH);
			if (full.Checked) FullScreen (true);
			host.Enabled = port.Enabled = bits16.Enabled = frames.Enabled = false;
		}

		// The Pi's screen in the middle of the area when the area is bigger (black around it: the full
		// screen on a bigger monitor, a window made bigger); else at the scroll bars' place.
		void CenterDesk ()
		{
			var c = scroller.ClientSize;
			int x = desk.Width < c.Width ? (c.Width - desk.Width) / 2 : scroller.AutoScrollPosition.X;
			int y = desk.Height < c.Height ? (c.Height - desk.Height) / 2 : scroller.AutoScrollPosition.Y;
			if (desk.Left != x || desk.Top != y) desk.Location = new Point (x, y);
		}

		// The full screen's bar (FullBar, as mstsc's): shown when the pointer touches the top edge of
		// the screen, hidden ~1 s after it left the bar (unless pinned).
		void FullBarTick ()
		{
			if (!isFull || fullBar == null) return;
			if (WindowState == FormWindowState.Minimized || Form.ActiveForm == null)
			{
				if (fullBar.Visible) fullBar.Hide ();	// (minimised, or another app in front: none of
				return;					// ours active -- the bar itself or an overlay is fine)
			}
			var scr = Screen.FromControl (this).Bounds;
			Point p = Cursor.Position;
			bool onEdge = p.Y <= scr.Top && p.X >= scr.Left && p.X < scr.Right;
			bool onBar = fullBar.Visible && fullBar.Bounds.Contains (p);
			if (onEdge || onBar || fullBar.Pinned) fullAway = DateTime.Now;
			if (onEdge && !fullBar.Visible) fullBar.ShowOn (scr, host.Text.Trim ());
			else if (fullBar.Visible && (DateTime.Now - fullAway).TotalMilliseconds > 1000) fullBar.Hide ();
		}

		// F11: the full screen on and off (the keys go to the Pi otherwise)
		protected override bool ProcessCmdKey (ref Message msg, Keys keyData)
		{
			if (keyData == Keys.F11) { full.Checked = !full.Checked; if (Conn == null) FullScreen (false); return true; }
			return base.ProcessCmdKey (ref msg, keyData);
		}

		// The full screen: no frame, no tool bar, the whole monitor (the taskbar covered) -- the
		// Pi's screen pixel for pixel (one the monitor's size fills it exactly; a bigger one
		// scrolls). Off: the window as it was.
		void FullScreen (bool on)
		{
			if (on == isFull) return;
			if (on)
			{
				fullBorder = FormBorderStyle; fullMaxBox = MaximizeBox; fullState = WindowState;
				fullRestore = WindowState == FormWindowState.Normal ? Bounds : RestoreBounds;
				ts.Visible = false;
				WindowState = FormWindowState.Normal;
				FormBorderStyle = FormBorderStyle.None;
				Bounds = Screen.FromControl (this).Bounds;
				isFull = true;
				if (fullBar == null)
					fullBar = new FullBar (this, () => WindowState = FormWindowState.Minimized,
							       () => full.Checked = false, () => Disconnect ("Disconnected"));
				fullAway = DateTime.Now;
				fullTimer.Start ();
				Activate ();
			}
			else
			{
				isFull = false;
				fullTimer.Stop ();
				fullBar?.Hide ();
				FormBorderStyle = fullBorder; MaximizeBox = fullMaxBox;
				Bounds = fullRestore;
				WindowState = fullState;
				ts.Visible = true;
			}
			Sync ();
		}

		// Connected: the window the size of the Pi's screen (the tool bar above it) as far as the
		// PC's screen allows -- smaller, the scroll bars show the rest. It stays resizable.
		void FitToPi (int w, int h)
		{
			if (w <= 0 || h <= 0 || isFull) return;
			var area = Screen.FromControl (this).WorkingArea;
			if (WindowState != FormWindowState.Normal) WindowState = FormWindowState.Normal;
			Size frame = Size - ClientSize;
			int maxW = area.Width - frame.Width, maxH = area.Height - frame.Height;
			int cw = w, ch = ts.Height + h;
			if (ch > maxH) cw += SystemInformation.VerticalScrollBarWidth;		// (room for the scroll bars)
			if (cw > maxW) ch += SystemInformation.HorizontalScrollBarHeight;
			ClientSize = new Size (Math.Min (cw, maxW), Math.Min (ch, maxH));
			if (Right > area.Right) Left = Math.Max (area.Left, area.Right - Width);
			if (Bottom > area.Bottom) Top = Math.Max (area.Top, area.Bottom - Height);
		}

		void SaveSettings ()
		{
			int p; if (!int.TryParse (port.Text, out p) || p <= 0 || p > 65535) p = 3390;
			try
			{
				File.WriteAllLines (SettingsPath, new[] { "host=" + host.Text, "port=" + p,
					"bits16=" + (bits16.Checked ? 1 : 0), "desktop=" + (desktop.Checked ? 1 : 0), "frames=" + (frames.Checked ? 1 : 0),
					"fullscreen=" + (full.Checked ? 1 : 0) });
			}
			catch { }
		}

		void Disconnect (string why)
		{
			FullScreen (false);				// (the tool bar back: Connect again)
			Conn?.Close (); Conn = null;
			foreach (var w in wins.Values) { w.GoneOnPi = true; w.Close (); }
			wins.Clear (); winOrder.Clear ();
			foreach (var o in overlays.Values) o.Close ();
			overlays.Clear (); stacked.Clear ();
			barDrop?.Close (); barDrop = null;
			Bar.Present = false;
			desk.Back = null; desk.Visible = false;
			go.Text = "Connect";
			host.Enabled = port.Enabled = bits16.Enabled = frames.Enabled = true;
			status.Text = why;
		}

		// An overlay where the Pi has its window (x, y on the Pi's screen, w x h), cut to the part of
		// the desktop view that shows (scrolled, or the window smaller than it). top: added to the
		// pointer's y (the bar's drop-down: its pixels start below the bar).
		void PlaceOverlay (Overlay ov, int x, int y, int w, int h, int alpha, int top = 0)
		{
			if (WindowState == FormWindowState.Minimized || !desk.Visible) { ov.Place (0, 0, 0, 0, 0, 0, 0); return; }
			Rectangle area = Rectangle.Intersect (scroller.RectangleToScreen (scroller.ClientRectangle), desk.RectangleToScreen (desk.ClientRectangle));
			Point o = desk.PointToScreen (new Point (x, y));
			var r = Rectangle.Intersect (new Rectangle (o.X, o.Y, w, h), area);
			int sx = r.X - o.X, sy = r.Y - o.Y;
			ov.Map = p => new Point (p.X + sx, p.Y + sy + top);
			ov.Place (r.X, r.Y, sx, sy, r.Width, r.Height, alpha);
		}

		// The model -> the child windows, the bar, the overlays, the desktop (the UI thread).
		void Sync ()
		{
			if (Conn == null) return;
			lock (Conn.Lock)
			{
				if (Conn.ScreenW > 0 && Conn.ScreenH > 0 && (desk.Width != Conn.ScreenW || desk.Height != Conn.ScreenH))
					desk.Size = new Size (Conn.ScreenW, Conn.ScreenH);	// (the Pi's resolution changed)
				var keep = new HashSet<uint> ();
				var keepOver = new HashSet<uint> ();
				var keepBubble = new HashSet<uint> ();
				var order = new List<Overlay> ();			// the overlays, bottom to top
				var framed = new List<RemoteWindow> ();			// the child windows, bottom to top
				bool barSeen = false, barDirty = false, deskDirty = false, framedBelow = false;
				var bubbleOrder = new List<WinModel> ();
				foreach (uint id in Conn.ZOrder)			// (bottom to top: new ones open in that order)
				{
					if (!Conn.Windows.TryGetValue (id, out WinModel m)) continue;
					if (m.W <= 0 || m.H <= 0) continue;
					if (id == WinModel.DESKTOP_ID)
					{
						if (desktop.Checked && (m.Dirty || deskBmp == null)) { deskBmp = Pix.Make (deskBmp, m.W, m.H); Pix.Fill (deskBmp, m.Content, m.W, m.H); m.Dirty = false; deskDirty = true; }
						continue;
					}
					if (m.Hidden) continue;					// (minimised, on another workspace)
					bool borderless = (m.Flags & WinModel.BORDERLESS) != 0 && (m.State & WinModel.FULLSCREEN) == 0;
					if ((m.Flags & WinModel.TOPMOST) != 0 && m.Y == 0 && borderless)	// the menu bar
					{
						barSeen = true;
						if (m.Dirty || !Bar.Present) { Bar.Update (m); m.Dirty = false; barDirty = true; }
						if (Bar.H > Bar.BarH)				// open: its drop-down over the windows
						{
							if (barDrop == null) { barDrop = new Overlay (Conn, id, this); barDirty = true; }
							order.Add (barDrop);
						}
						continue;
					}
					if ((m.Flags & WinModel.BACKMOST) != 0) continue;	// (in the desktop's picture)
					if (borderless)
					{
						// over the child windows: pinned on top (the dock, the notifications), above a
						// framed window (a popup: the Wi-Fi menu...), or anywhere without the desktop's
						// picture; else a widget on the desktop's picture, below them
						if ((m.Flags & WinModel.TOPMOST) != 0 || framedBelow || !desktop.Checked)
						{
							if (!overlays.TryGetValue (id, out Overlay ov)) { ov = new Overlay (Conn, id, this); overlays[id] = ov; m.Dirty = true; }
							if (m.Dirty) { ov.SetPixels (m.Content, m.W, m.H, (m.Flags & WinModel.ALPHA) != 0, (m.Flags & WinModel.TRANSPARENT) != 0); m.Dirty = false; }
							PlaceOverlay (ov, m.X, m.Y, m.W, m.H, m.Alpha);
							keepOver.Add (id); order.Add (ov);
							continue;
						}
						keepBubble.Add (id);
						if (m.Alpha > 0) bubbleOrder.Add (m);
						if (m.Dirty || !bubbles.ContainsKey (id))
						{
							bubbles.TryGetValue (id, out Bitmap bb);
							bubbles[id] = Pix.Make (bb, m.W, m.H, PixelFormat.Format32bppArgb);
							Pix.FillArgb (bubbles[id], m.Content, m.W, m.H, (m.Flags & WinModel.ALPHA) != 0, (m.Flags & WinModel.TRANSPARENT) != 0);
							m.Dirty = false; deskDirty = true;
						}
						continue;
					}
					framedBelow = true;
					keep.Add (id);
					if (!wins.TryGetValue (id, out RemoteWindow w))
					{
						w = new RemoteWindow (Conn, id, frames.Checked, desk);
						wins[id] = w;
						w.Apply (m, 0);
						w.Show ();
					}
					else w.Apply (m, 0);
					framed.Add (w);
				}
				foreach (var id in new List<uint> (wins.Keys))
					if (!keep.Contains (id)) { wins[id].GoneOnPi = true; wins[id].Close (); wins.Remove (id); }
				foreach (var id in new List<uint> (overlays.Keys))
					if (!keepOver.Contains (id)) { overlays[id].Close (); overlays.Remove (id); }
				foreach (var id in new List<uint> (bubbles.Keys))
					if (!keepBubble.Contains (id)) { bubbles[id]?.Dispose (); bubbles.Remove (id); deskDirty = true; }
				// the child windows stacked as on the Pi (when their order changed)
				bool sameWins = framed.Count == winOrder.Count;
				for (int i = 0; sameWins && i < framed.Count; i++) sameWins = framed[i] == winOrder[i];
				if (!sameWins) { foreach (var w in framed) w.BringToFront (); winOrder = framed; }
				// the bar (drawn by the desktop view), its drop-down
				if (!barSeen && Bar.Present) { Bar.Present = false; barDirty = true; }
				if (barDirty) desk.Invalidate (new Rectangle (0, 0, desk.Width, Bar.BarH));
				if (barDrop != null && !order.Contains (barDrop)) { barDrop.Close (); barDrop = null; }
				if (barDrop != null)
				{
					if (barDirty)
					{
						int dh = Bar.DropPixels (ref dropPx, Bar.W);
						barDrop.SetPremultiplied (dropPx, Bar.W, dh);
					}
					PlaceOverlay (barDrop, 0, Bar.BarH, Bar.W, Bar.H - Bar.BarH, 255, Bar.BarH);
				}
				// the overlays stacked as on the Pi (when their order changed)
				bool same = order.Count == stacked.Count;
				for (int i = 0; same && i < order.Count; i++) same = order[i] == stacked[i];
				if (!same)
				{
					for (int i = order.Count - 2; i >= 0; i--) order[i].Below (order[i + 1]);
					stacked = order;
				}
				// the desktop: its picture + the widgets, drawn by the desktop view (the bar over it)
				if (desktop.Checked && deskBmp != null && (deskDirty || desk.Back == null))
				{
					if (backBmp == null || backBmp.Width != deskBmp.Width || backBmp.Height != deskBmp.Height)
					{
						backBmp?.Dispose ();
						backBmp = new Bitmap (deskBmp.Width, deskBmp.Height, PixelFormat.Format32bppRgb);
					}
					using (var g = Graphics.FromImage (backBmp))
					{
						g.DrawImageUnscaled (deskBmp, 0, 0);
						foreach (var m in bubbleOrder)
						{
							if (!bubbles.TryGetValue (m.Id, out Bitmap bb) || bb == null) continue;
							using (var ia = new ImageAttributes ())
							{
								if (m.Alpha < 255) ia.SetColorMatrix (new ColorMatrix { Matrix33 = m.Alpha / 255f });
								g.DrawImage (bb, new Rectangle (m.X, m.Y, m.W, m.H), 0, 0, m.W, m.H, GraphicsUnit.Pixel, ia);
							}
						}
					}
					desk.Back = backBmp;
					desk.Invalidate ();
				}
				else if (!desktop.Checked && desk.Back != null) { desk.Back = null; desk.Invalidate (); }
			}
		}

		protected override void OnFormClosed (FormClosedEventArgs e) { Conn?.Close (); base.OnFormClosed (e); }
	}

	static class Program
	{
		[STAThread]
		static void Main ()
		{
			Application.EnableVisualStyles ();
			Application.SetCompatibleTextRenderingDefault (false);
			Application.Run (new MainForm ());
		}
	}
}
