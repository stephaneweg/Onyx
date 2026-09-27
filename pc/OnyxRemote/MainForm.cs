// MainForm.cs -- Onyx Remote: one window (MDI) holding the Onyx session. At the top a tool bar
// (the Pi's address, the options, Connect / Disconnect, the updates a second), then the Onyx
// menu bar across the window's width (its menus on the left, the status and the clock on the
// right; its drop-down menus drawn over the windows); below, the Onyx windows as child windows,
// placed as on the Pi (the area starts below the menu bar), over the Onyx desktop (the
// wallpaper and the bubbles) when "Desktop" is on.
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
		public uint Id; public bool Present;
		int[] px;
		public void Update (WinModel m)
		{
			Id = m.Id; Present = true;
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
		// the bar's rows y0..y1 into a width destW at dy (the middle stretched on the bar's own rows,
		// the colour key under a drop-down menu)
		public void Draw (Graphics g, int destW, int y0, int y1, int dy)
		{
			if (Bmp == null || y1 <= y0) return;
			int gap = Math.Max (0, destW - W);
			if (Split <= 0 || gap == 0) { g.DrawImage (Bmp, new Rectangle (0, dy, W, y1 - y0), new Rectangle (0, y0, W, y1 - y0), GraphicsUnit.Pixel); return; }
			g.DrawImage (Bmp, new Rectangle (0, dy, Split, y1 - y0), new Rectangle (0, y0, Split, y1 - y0), GraphicsUnit.Pixel);
			g.DrawImage (Bmp, new Rectangle (Split + gap, dy, W - Split, y1 - y0), new Rectangle (Split, y0, W - Split, y1 - y0), GraphicsUnit.Pixel);
			int by1 = Math.Min (y1, BarH);
			if (by1 > y0) g.DrawImage (Bmp, new Rectangle (Split, dy, gap, by1 - y0), new Rectangle (Split, y0, 1, by1 - y0), GraphicsUnit.Pixel);
			if (y1 > BarH) using (var key = new SolidBrush (Color.FromArgb (255, 0, 255))) g.FillRectangle (key, Split, dy + Math.Max (0, BarH - y0), gap, y1 - Math.Max (y0, BarH));
		}
		public int MapX (int x, int destW)
		{
			int gap = Math.Max (0, destW - W);
			if (Split <= 0 || gap == 0) return x;
			return x >= Split + gap ? x - gap : Math.Min (x, Split);
		}
	}

	// the bar's strip at the top of the window
	class BarPanel : Panel
	{
		readonly MainForm main;
		public BarPanel (MainForm m)
		{
			main = m;
			SetStyle (ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer, true);
			BackColor = Color.FromArgb (24, 26, 40);
		}
		protected override void OnPaint (PaintEventArgs e)
		{
			var b = main.Bar;
			if (!b.Present || b.Bmp == null) { base.OnPaint (e); return; }
			e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
			e.Graphics.PixelOffsetMode = PixelOffsetMode.Half;
			b.Draw (e.Graphics, Width, 0, Math.Min (b.BarH, b.H), 0);
		}
		void Send (MouseEventArgs e, int wheel)
		{
			var b = main.Bar;
			if (!b.Present || main.Conn == null) return;
			main.Conn.Pointer (b.Id, b.MapX (e.X, Width), e.Y, RemoteWindow.Buttons (MouseButtons), wheel);
		}
		protected override void OnMouseDown (MouseEventArgs e) { Send (e, 0); }
		protected override void OnMouseUp (MouseEventArgs e) { Send (e, 0); }
		protected override void OnMouseMove (MouseEventArgs e) { Send (e, 0); }
		protected override void OnMouseWheel (MouseEventArgs e) { Send (e, e.Delta > 0 ? 1 : -1); }
	}

	// the bar's drop-down menus: drawn over the child windows (an owned, colour-keyed window)
	class DropLayer : Form
	{
		readonly MainForm main;
		public DropLayer (MainForm m)
		{
			main = m;
			FormBorderStyle = FormBorderStyle.None; ShowInTaskbar = false; StartPosition = FormStartPosition.Manual;
			BackColor = Color.FromArgb (255, 0, 255); TransparencyKey = BackColor;
			SetStyle (ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer, true);
		}
		protected override bool ShowWithoutActivation { get { return true; } }
		protected override void OnPaint (PaintEventArgs e)
		{
			var b = main.Bar;
			e.Graphics.Clear (BackColor);
			if (b.Bmp == null || b.H <= b.BarH) return;
			e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
			e.Graphics.PixelOffsetMode = PixelOffsetMode.Half;
			b.Draw (e.Graphics, Width, b.BarH, b.H, 0);
		}
		void Send (MouseEventArgs e, int wheel)
		{
			var b = main.Bar;
			if (!b.Present || main.Conn == null) return;
			main.Conn.Pointer (b.Id, b.MapX (e.X, Width), e.Y + b.BarH, RemoteWindow.Buttons (MouseButtons), wheel);
		}
		protected override void OnMouseDown (MouseEventArgs e) { Send (e, 0); }
		protected override void OnMouseUp (MouseEventArgs e) { Send (e, 0); }
		protected override void OnMouseMove (MouseEventArgs e) { Send (e, 0); }
	}

	class MainForm : Form
	{
		readonly ToolStripTextBox host = new ToolStripTextBox (), port = new ToolStripTextBox ();
		readonly ToolStripButton bits16 = new ToolStripButton ("16-bit colours") { CheckOnClick = true };
		readonly ToolStripButton desktop = new ToolStripButton ("Desktop") { CheckOnClick = true };
		readonly ToolStripButton frames = new ToolStripButton ("Onyx frames") { CheckOnClick = true };
		readonly ToolStripButton go = new ToolStripButton ("Connect");
		readonly ToolStripButton console = new ToolStripButton ("Console") { ToolTipText = "A telnet console on the Pi (the Onyx shell; telnetd, port 23)" };
		readonly ToolStripLabel status = new ToolStripLabel ("Not connected");
		readonly BarPanel barPanel;
		readonly DropLayer drop;
		readonly MdiClient mdi;
		public Connection Conn;
		public readonly BarView Bar = new BarView ();
		readonly Dictionary<uint, RemoteWindow> wins = new Dictionary<uint, RemoteWindow> ();
		Bitmap deskBmp, backBmp; readonly Dictionary<uint, Bitmap> bubbles = new Dictionary<uint, Bitmap> ();
		int rounds; DateTime since = DateTime.Now;
		static readonly string SettingsPath = Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.ApplicationData), "OnyxRemote.txt");

		public MainForm ()
		{
			Text = "Onyx Remote";
			IsMdiContainer = true;
			Font = new Font ("Segoe UI", 9f);
			ClientSize = new Size (1040, 830);
			foreach (Control c in Controls) if (c is MdiClient mc) mdi = mc;
			mdi.BackColor = Color.FromArgb (32, 64, 96);
			var ts = new ToolStrip { GripStyle = ToolStripGripStyle.Hidden, Dock = DockStyle.Top };
			host.Width = 130; port.Width = 45; port.Text = "3390";
			ts.Items.AddRange (new ToolStripItem[] { new ToolStripLabel ("Onyx:"), host, port, new ToolStripSeparator (), go, console,
				new ToolStripSeparator (), bits16, desktop, frames, new ToolStripSeparator (), status });
			barPanel = new BarPanel (this) { Dock = DockStyle.Top, Height = 32, Visible = false };
			Controls.Add (barPanel);
			Controls.Add (ts);
			drop = new DropLayer (this);
			go.Click += (s, e) => { if (Conn == null) Connect (); else Disconnect ("Disconnected"); };
			console.Click += (s, e) => OpenConsole ();
			desktop.CheckedChanged += (s, e) => { if (Conn != null) { Conn.Desktop (desktop.Checked); Sync (); } };
			host.KeyDown += (s, e) => { if (e.KeyCode == Keys.Enter && Conn == null) { e.SuppressKeyPress = true; Connect (); } };
			MdiChildActivate += (s, e) => { if (ActiveMdiChild is RemoteWindow rw && Conn != null) Conn.Raise (rw.Id); };
			// the desktop's pointer: the MDI area's background
			mdi.MouseDown += (s, e) => DeskPointer (e, 0);
			mdi.MouseUp += (s, e) => DeskPointer (e, 0);
			mdi.MouseMove += (s, e) => DeskPointer (e, 0);
			Move += (s, e) => PlaceDrop ();
			Resize += (s, e) => { PlaceDrop (); barPanel.Invalidate (); Sync (); };	// (Sync: the desktop's picture to the new size)
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

		void DeskPointer (MouseEventArgs e, int wheel)
		{
			if (Conn == null || !desktop.Checked) return;
			Conn.Pointer (WinModel.DESKTOP_ID, e.X, e.Y + Bar.BarH, RemoteWindow.Buttons (MouseButtons), wheel);
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
			host.Enabled = port.Enabled = bits16.Enabled = frames.Enabled = false;
		}

		void SaveSettings ()
		{
			int p; if (!int.TryParse (port.Text, out p) || p <= 0 || p > 65535) p = 3390;
			try
			{
				File.WriteAllLines (SettingsPath, new[] { "host=" + host.Text, "port=" + p,
					"bits16=" + (bits16.Checked ? 1 : 0), "desktop=" + (desktop.Checked ? 1 : 0), "frames=" + (frames.Checked ? 1 : 0) });
			}
			catch { }
		}

		void Disconnect (string why)
		{
			Conn?.Close (); Conn = null;
			foreach (var w in wins.Values) { w.GoneOnPi = true; w.Close (); }
			wins.Clear ();
			Bar.Present = false; barPanel.Visible = false; drop.Hide ();
			mdi.BackgroundImage = null;
			go.Text = "Connect";
			host.Enabled = port.Enabled = bits16.Enabled = frames.Enabled = true;
			status.Text = why;
		}

		void PlaceDrop ()
		{
			if (!drop.Visible) return;
			Point p = mdi.PointToScreen (Point.Empty);
			drop.Bounds = new Rectangle (p.X, p.Y, mdi.ClientSize.Width, Math.Max (1, Bar.H - Bar.BarH));
		}

		// The model -> the child windows, the bar, the desktop (the UI thread).
		void Sync ()
		{
			if (Conn == null) return;
			lock (Conn.Lock)
			{
				var keep = new HashSet<uint> ();
				bool barSeen = false, deskDirty = false;
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
					bool borderless = (m.Flags & WinModel.BORDERLESS) != 0 && (m.State & WinModel.FULLSCREEN) == 0;
					if ((m.Flags & WinModel.TOPMOST) != 0 && m.Y == 0 && borderless)	// the menu bar
					{
						barSeen = true;
						if (m.Dirty || !Bar.Present) { Bar.Update (m); m.Dirty = false; barPanel.Invalidate (); drop.Invalidate (); }
						continue;
					}
					if ((m.Flags & WinModel.BACKMOST) != 0) continue;	// (in the desktop's picture)
					if (borderless)					// a bubble, a widget: on the desktop's picture
					{
						if (m.Alpha > 0) bubbleOrder.Add (m);
						if (m.Dirty || !bubbles.ContainsKey (id))
						{
							bubbles.TryGetValue (id, out Bitmap bb);
							bubbles[id] = Pix.Make (bb, m.W, m.H); Pix.Fill (bubbles[id], m.Content, m.W, m.H); m.Dirty = false; deskDirty = true;
						}
						continue;
					}
					keep.Add (id);
					if (!wins.TryGetValue (id, out RemoteWindow w))
					{
						w = new RemoteWindow (Conn, id, frames.Checked, this);
						wins[id] = w;
						w.Apply (m, Bar.BarH);
						w.Show ();
					}
					else w.Apply (m, Bar.BarH);
				}
				foreach (var id in new List<uint> (wins.Keys))
					if (!keep.Contains (id)) { wins[id].GoneOnPi = true; wins[id].Close (); wins.Remove (id); }
				foreach (var id in new List<uint> (bubbles.Keys))
					if (!Conn.Windows.ContainsKey (id)) { bubbles[id]?.Dispose (); bubbles.Remove (id); deskDirty = true; }
				// the bar, its drop-down menus
				if (barSeen != barPanel.Visible) { barPanel.Visible = barSeen; if (!barSeen) Bar.Present = false; }
				if (barSeen && barPanel.Height != Bar.BarH) barPanel.Height = Bar.BarH;
				bool open = barSeen && Bar.H > Bar.BarH;
				if (open && !drop.Visible) { drop.Show (this); PlaceDrop (); }
				else if (!open && drop.Visible) drop.Hide ();
				if (open) PlaceDrop ();
				// the desktop: its picture (below the bar) + the bubbles, as the MDI area's background
				// (the MDI area tiles its background image: the picture is made at least the area's
				// size, the part beyond the Pi's screen a plain colour -- else the desktop repeated)
				int bw = Math.Max (deskBmp?.Width ?? 1, mdi.ClientSize.Width), bh = Math.Max (Math.Max (1, (deskBmp?.Height ?? 1) - Bar.BarH), mdi.ClientSize.Height);
				bool resized = backBmp == null || backBmp.Width != bw || backBmp.Height != bh;
				if (desktop.Checked && deskBmp != null && (deskDirty || resized || mdi.BackgroundImage == null))
				{
					Bitmap old = null;
					if (resized) { old = backBmp; backBmp = new Bitmap (bw, bh, System.Drawing.Imaging.PixelFormat.Format32bppRgb); }
					using (var g = Graphics.FromImage (backBmp))
					{
						g.Clear (Color.FromArgb (16, 18, 28));
						g.DrawImageUnscaled (deskBmp, 0, -Bar.BarH);
						foreach (var m in bubbleOrder)
						{
							if (!bubbles.TryGetValue (m.Id, out Bitmap bb) || bb == null) continue;
							using (var ia = new ImageAttributes ())
							{
								if ((m.Flags & WinModel.TRANSPARENT) != 0) ia.SetColorKey (Color.FromArgb (255, 0, 255), Color.FromArgb (255, 0, 255));
								if (m.Alpha < 255) ia.SetColorMatrix (new ColorMatrix { Matrix33 = m.Alpha / 255f });
								g.DrawImage (bb, new Rectangle (m.X, m.Y - Bar.BarH, m.W, m.H), 0, 0, m.W, m.H, GraphicsUnit.Pixel, ia);
							}
						}
					}
					mdi.BackgroundImageLayout = ImageLayout.None;
					mdi.BackgroundImage = backBmp;
					old?.Dispose ();
					mdi.Invalidate ();
				}
				else if (!desktop.Checked && mdi.BackgroundImage != null) mdi.BackgroundImage = null;
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
