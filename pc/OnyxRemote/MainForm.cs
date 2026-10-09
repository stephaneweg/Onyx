// MainForm.cs -- Onyx Remote: one window holding the Onyx session, shown once connected (the Pi's
// address and the options are asked by ConnectDialog.cs, shown at the start and back after a
// disconnection). At the top a tool bar (Disconnect, Terminal, Desktop, Full screen, Screenshot,
// the updates a second); below, a
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
			SetStyle (ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.Selectable, true);
			TabStop = true;
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
		protected override void OnMouseDown (MouseEventArgs e) { if (!Focused) Focus (); Send (e, 0); }
		protected override void OnMouseUp (MouseEventArgs e) { Send (e, 0); }
		protected override void OnMouseMove (MouseEventArgs e) { Send (e, 0); }

		// ---- the keyboard (2026-10-09): with no Onyx window of its own here (the console's home, the pocket launcher,
		// an app told as a picture...) the keys typed over the Pi's screen still go to the Pi -- its graphics server
		// gives them to the window that has the keyboard there. As RemoteWindow's; F11, Ctrl+Shift+S / W stay
		// Onyx Remote's (MainForm). Without this nothing typed was sent ("the RDP keyboard in console mode").
		public void TakeKeys () { if (Visible && !ContainsFocus) Focus (); }	// (not from one of its windows)
		protected override bool IsInputKey (Keys keyData) { return true; }	// (the arrows, Tab: to the Pi, not the dialog's navigation)
		protected override bool ProcessCmdKey (ref Message msg, Keys keyData)
		{
			const int WM_KEYDOWN = 0x100, WM_SYSKEYDOWN = 0x104;
			var c = main.Conn;
			if (c == null || keyData == Keys.F11 || keyData == (Keys.Control | Keys.Shift | Keys.S) || keyData == (Keys.Control | Keys.Shift | Keys.W))
				return base.ProcessCmdKey (ref msg, keyData);
			if (msg.Msg == WM_KEYDOWN || msg.Msg == WM_SYSKEYDOWN)
			{
				Keys k = keyData & Keys.KeyCode;
				uint sym = KeyMap.Special (k);
				if (sym != 0 && !KeyMap.IsModifier (k)) { c.Key (true, false, sym); return true; }
			}
			return base.ProcessCmdKey (ref msg, keyData);
		}
		protected override void OnKeyDown (KeyEventArgs e)
		{
			var c = main.Conn;
			if (c == null) return;
			if (KeyMap.IsModifier (e.KeyCode)) c.Key (true, false, KeyMap.Special (e.KeyCode));
			else { uint hk = KeyMap.Held (e.KeyCode); if (hk != 0) c.Key (true, true, hk); }
			if (e.Alt && e.KeyCode == Keys.F4) return;
			e.Handled = true;
		}
		protected override void OnKeyUp (KeyEventArgs e)
		{
			var c = main.Conn;
			if (c == null) return;
			uint s = KeyMap.Special (e.KeyCode);
			if (s != 0) c.Key (false, false, s);
			else { uint hk = KeyMap.Held (e.KeyCode); if (hk != 0) c.Key (false, true, hk); }
			e.Handled = true;
		}
		protected override void OnKeyPress (KeyPressEventArgs e)
		{
			var c = main.Conn;
			if (c == null) return;
			char ch = e.KeyChar;
			if (ch == '\r' || ch == '\b' || ch == '\t' || ch == (char) 27) return;	// (sent as keys)
			if (ch < 256) c.Char (ch);
			e.Handled = true;
		}
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
		readonly Settings set;					// (shared with the connection dialog)
		string host = "";					// (the Pi's address, connected)
		Connection opening; Action<string> opened; Rectangle openedNear;	// (a connection being made: Begin)
		public event Action<string> Ended;			// (disconnected: why -- the dialog comes back)
		readonly ToolStripButton desktop = new ToolStripButton ("Desktop") { CheckOnClick = true, ToolTipText = "The Onyx desktop (the wallpaper, the widgets) behind the windows" };
		readonly ToolStripButton go = new ToolStripButton ("Disconnect");
		readonly ToolStripButton console = new ToolStripButton ("Terminal") { ToolTipText = "A telnet console on the Pi (the Onyx shell; telnetd, port 23)" };
		readonly ToolStripButton full = new ToolStripButton ("Full screen") { CheckOnClick = true, ToolTipText = "The Onyx session over the whole screen, without this tool bar (F11 toggles it)" };
		readonly ToolStripSplitButton shot = new ToolStripSplitButton ("Screenshot") { ToolTipText = "Save the Pi's screen as a PNG (Ctrl+Shift+S: straight to Pictures\\Onyx; the arrow: more)" };
		readonly ToolStripLabel status = new ToolStripLabel ("Not connected");
		readonly ToolStrip ts;
		readonly ScrollArea scroller;			// (the scrolling area)
		readonly DeskView desk;					// (the Pi's screen in it)
		bool isFull; Rectangle fullRestore; FormBorderStyle fullBorder; bool fullMaxBox; FormWindowState fullState;	// (before the full screen)
		Toast toast; string shotDir;				// (the screenshots: the confirmation, the Save As dialog's folder)
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
		string link;						// (the connection lost: what is being done; null: connected)

		public MainForm (Settings settings)
		{
			set = settings;
			StartPosition = FormStartPosition.Manual;
			Text = "Onyx Remote";
			if (Program.AppIcon != null) Icon = Program.AppIcon;
			Font = new Font ("Segoe UI", 9f);
			ClientSize = new Size (1040, 830);
			ts = new ToolStrip { GripStyle = ToolStripGripStyle.Hidden, Dock = DockStyle.Top };
			ts.Items.AddRange (new ToolStripItem[] { go, console, new ToolStripSeparator (), desktop, full,
				new ToolStripSeparator (), shot, new ToolStripSeparator (), status });
			desktop.Checked = set.Desktop; full.Checked = set.Full;
			shot.ButtonClick += (s, e) => SaveShot (false, true);
			shot.DropDownItems.AddRange (new ToolStripItem[] {
				new ToolStripMenuItem ("Save screen as...", null, (s, e) => SaveShot (false, true)),
				new ToolStripMenuItem ("Save window as...", null, (s, e) => SaveShot (true, true)) { ToolTipText = "The Onyx window that has the keyboard (else the front one), with its Onyx frame" },
				new ToolStripSeparator (),
				new ToolStripMenuItem ("Quick save screen", null, (s, e) => SaveShot (false, false)) { ShortcutKeyDisplayString = "Ctrl+Shift+S" },
				new ToolStripMenuItem ("Quick save window", null, (s, e) => SaveShot (true, false)) { ShortcutKeyDisplayString = "Ctrl+Shift+W" },
				new ToolStripSeparator (),
				new ToolStripMenuItem ("Open the screenshots folder", null, (s, e) => Screenshot.ShowInExplorer (null)) });
			scroller = new ScrollArea { Dock = DockStyle.Fill, AutoScroll = true, BackColor = Color.Black };
			desk = new DeskView (this) { Location = Point.Empty, Size = new Size (1024, 768), Visible = false };
			scroller.Controls.Add (desk);
			Controls.Add (scroller);
			Controls.Add (ts);
			go.Click += (s, e) => Disconnect ("Disconnected");
			console.Click += (s, e) => Program.OpenTerminal (host);
			full.CheckedChanged += (s, e) => { set.Full = full.Checked; set.Save (); if (Conn != null) FullScreen (full.Checked); };
			desktop.CheckedChanged += (s, e) => { set.Desktop = desktop.Checked; set.Save (); if (Conn != null) { Conn.Desktop (desktop.Checked); Sync (); } };
			Move += (s, e) => Sync ();					// (the overlays follow)
			Resize += (s, e) => Sync ();
			scroller.Scroll += (s, e) => Sync ();
			desk.Move += (s, e) => Sync ();				// (scrolled: by the wheel too)
			scroller.Layout += (s, e) => CenterDesk ();
			desk.Resize += (s, e) => CenterDesk ();
			fullTimer.Tick += (s, e) => FullBarTick ();
			var tick = new Timer { Interval = 1000 };
			tick.Tick += (s, e) => ShowStatus ();
			tick.Start ();
			var made = Handle;				// (made now: the connection's threads post to it before the window shows)
		}

		// The status line, every second: the updates a second (rounds shown), how the rounds flow
		// (pipelined, or lock-step with an older rdpd), the PINGs answered (the server's probes
		// after a loss, its liveness checks), the reconnections; or, the connection lost, what is
		// being done about it.
		// The pointer's shape on the Pi (rdpd's message 11, KAPI_CURSOR_*) shown by the PC's own pointer
		// over the Onyx windows: the hand over a link, the I bar over text, the arrows of a frame's edge...
		Cursor pointer = Cursors.Default;
		static readonly Cursor[] Shapes = { Cursors.Default, Cursors.Hand, Cursors.IBeam, Cursors.SizeAll, Cursors.SizeWE,
			Cursors.SizeNS, Cursors.SizeNWSE, Cursors.SizeNESW, Cursors.Cross, Cursors.Cross, Cursors.WaitCursor, Cursors.No };

		void ShowCursor (int shape)
		{
			pointer = shape >= 0 && shape < Shapes.Length ? Shapes[shape] : Cursors.Default;
			desk.Cursor = pointer;
			foreach (var w in wins.Values) w.Cursor = pointer;
			foreach (var o in overlays.Values) o.Cursor = pointer;
			if (barDrop != null) barDrop.Cursor = pointer;
		}

		void ShowStatus ()
		{
			var c = Conn;
			if (c == null) return;
			double sec = (DateTime.Now - since).TotalSeconds;
			if (link != null) status.Text = link;
			else if (c.KernelAbi < 56) status.Text = "The Onyx kernel is too old (kapi v" + c.KernelAbi + ", rdpd needs v56)";
			else
			{
				string s = string.Format ("{0}: {1} windows, {2:0.0} updates / s, ", host, wins.Count, rounds / Math.Max (sec, 0.001));
				s += c.Pipelined ? c.InFlight + " rounds in flight" : "lock-step (an older rdpd)";
				if (c.Pings > 0) s += ", " + c.Pings + " pings";
				if (c.Damaged > 0) s += ", " + c.Damaged + " damaged messages skipped";
				if (c.Reconnects > 0) s += ", reconnected " + c.Reconnects + (c.Reconnects == 1 ? " time" : " times");
				status.Text = s;
			}
			Text = link != null ? "Onyx Remote (reconnecting)" : "Onyx Remote";
			rounds = 0; since = DateTime.Now;
		}

		// Connect to the Pi the settings name (the dialog's Connect), without holding the dialog: done
		// (null) once the session's window is up -- on the screen that rectangle is on --, done (why)
		// when it could not be. Cancel () gives it up.
		public void Begin (Rectangle near, Action<string> done)
		{
			Cancel ();
			desktop.Checked = set.Desktop; full.Checked = set.Full;
			host = set.Host.Trim ();
			var c = new Connection ();
			opening = c; opened = done; openedNear = near;
			c.RoundDone += session => BeginInvoke ((Action) (() => { if (opening == c) Attach (c); if (Conn != c) return; Sync (); rounds++; c.Ready (session); }));
			c.Closed += why => BeginInvoke ((Action) (() => { if (opening == c) { opening = null; c.Close (); done (why); } else if (Conn == c) Disconnect (why); }));
			c.LinkChanged += what => BeginInvoke ((Action) (() => { if (Conn != c) return; link = what; ShowStatus (); }));
			c.CursorChanged += shape => BeginInvoke ((Action) (() => { if (Conn == c) ShowCursor (shape); }));
			string h = host; int p = set.Port; bool b16 = set.Bits16, back = set.Desktop, fr = set.Frames;
			System.Threading.ThreadPool.QueueUserWorkItem (_ =>
			{
				string err = null;
				try { c.Open (h, p, b16, back, fr); }
				catch (Exception e) { err = e.Message; }
				try
				{
					BeginInvoke ((Action) (() =>
					{
						if (opening != c) { if (Conn != c) c.Close (); return; }	// (given up meanwhile; or shown already)
						if (err != null) { opening = null; done (err); }
						else Attach (c);
					}));
				}
				catch { c.Close (); }				// (the program is leaving)
			});
		}

		public void Cancel ()
		{
			var c = opening;
			opening = null;
			c?.Close ();
		}

		// connected: the session's window, the size of the Pi's screen
		void Attach (Connection c)
		{
			opening = null;
			Conn = c; link = null;
			status.Text = "Connected";
			scroller.AutoScrollPosition = Point.Empty;
			desk.Location = Point.Empty;
			desk.Size = new Size (Math.Max (1, c.ScreenW), Math.Max (1, c.ScreenH));
			desk.Visible = true;
			desk.TakeKeys ();
			var area = Screen.FromRectangle (openedNear).WorkingArea;
			WindowState = FormWindowState.Normal;
			Location = area.Location;
			FitToPi (c.ScreenW, c.ScreenH);
			Location = new Point (area.Left + Math.Max (0, (area.Width - Width) / 2), area.Top + Math.Max (0, (area.Height - Height) / 2));
			Show ();
			Activate ();
			if (full.Checked) FullScreen (true);
			opened?.Invoke (null);
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
			bool lost = link != null;			// (reconnecting: the bar shows it)
			if (onEdge || onBar || fullBar.Pinned || lost) fullAway = DateTime.Now;
			string name = host + (lost ? " (reconnecting...)" : "");
			if (fullBar.Visible && (DateTime.Now - fullAway).TotalMilliseconds > 1000) fullBar.Hide ();
			else if (onEdge || lost || fullBar.Visible) fullBar.ShowOn (scr, name);
		}

		// Back to Onyx Remote: the keys to the Pi at once (the desktop view's, when no Onyx window has them)
		protected override void OnActivated (EventArgs e) { base.OnActivated (e); if (desk != null && Conn != null) desk.TakeKeys (); }

		// F11: the full screen on and off; Ctrl+Shift+S / Ctrl+Shift+W: a quick screenshot of the
		// screen / of the active window (the keys go to the Pi otherwise)
		protected override bool ProcessCmdKey (ref Message msg, Keys keyData)
		{
			if (keyData == Keys.F11) { full.Checked = !full.Checked; return true; }
			if (keyData == (Keys.Control | Keys.Shift | Keys.S)) { SaveShot (false, false); return true; }
			if (keyData == (Keys.Control | Keys.Shift | Keys.W)) { SaveShot (true, false); return true; }
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
							       () => full.Checked = false, () => Disconnect ("Disconnected"), () => SaveShot (false, false));
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

		// A screenshot: the whole Pi screen, or the active Onyx window (the one with the keyboard,
		// else the front one), from the pixels the client has. ask: a Save As dialog (the last
		// folder used, first Pictures\Onyx); else straight to Pictures\Onyx, a short confirmation.
		void SaveShot (bool window, bool ask)
		{
			var c = Conn;
			if (c == null) { status.Text = "Not connected: nothing to capture"; return; }
			Bitmap b = null; string what = "screen";
			lock (c.Lock)
			{
				if (window)
				{
					RemoteWindow rw = null;
					foreach (var w in wins.Values) if (w.ContainsFocus) rw = w;
					if (rw == null && winOrder.Count > 0) rw = winOrder[winOrder.Count - 1];
					if (rw != null && c.Windows.TryGetValue (rw.Id, out WinModel m)) { b = Screenshot.Window (m); what = "\"" + m.Title + "\""; }
				}
				else b = Screenshot.Screen (c);
			}
			if (b == null) { Notify (window ? "No Onyx window to capture" : "Nothing to capture yet", null); return; }
			using (b)
			{
				string path;
				if (ask)
				{
					using (var dlg = new SaveFileDialog { Title = "Save the " + (window ? "window" : "screen") + " as", Filter = "PNG image (*.png)|*.png", DefaultExt = "png", AddExtension = true, OverwritePrompt = true })
					{
						string dir = shotDir ?? Screenshot.Folder;
						try { Directory.CreateDirectory (dir); } catch { }
						dlg.InitialDirectory = dir;
						dlg.FileName = Path.GetFileName (Screenshot.NewPath (dir));
						if (dlg.ShowDialog (this) != DialogResult.OK) return;
						path = dlg.FileName;
					}
				}
				else path = Screenshot.NewPath (Screenshot.Folder);
				try { Screenshot.Save (b, path); }
				catch (Exception e) { Notify ("Screenshot not saved: " + e.Message, null); return; }
				if (ask) shotDir = Path.GetDirectoryName (path);
				Notify ("Saved the " + (window ? "window " + what : "screen") + ": " + (ask ? path : Path.Combine ("Pictures", "Onyx", Path.GetFileName (path))), path);
			}
		}

		// in the status line, and a short toast (the full screen: no tool bar; a quick save from a key)
		void Notify (string message, string path)
		{
			status.Text = message;
			if (toast == null) toast = new Toast (this);
			var area = isFull ? Screen.FromControl (this).Bounds : RectangleToScreen (ClientRectangle);
			toast.ShowOn (area, message, path);
		}

		// Disconnected (the button, the full screen's bar, the connection closed: why): the window
		// hidden, the connection dialog back (Ended).
		void Disconnect (string why)
		{
			if (Conn == null) return;
			FullScreen (false);
			Conn.Close (); Conn = null;
			foreach (var w in wins.Values) { w.GoneOnPi = true; w.Close (); }
			wins.Clear (); winOrder.Clear ();
			foreach (var o in overlays.Values) o.Close ();
			overlays.Clear (); stacked.Clear ();
			barDrop?.Close (); barDrop = null;
			Bar.Present = false;
			desk.Back = null; desk.Visible = false;
			status.Text = why; link = null; Text = "Onyx Remote";
			toast?.Hide ();
			Hide ();
			Ended?.Invoke (why);
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
						w = new RemoteWindow (Conn, id, set.Frames, desk);
						w.Cursor = pointer;
						wins[id] = w;
						w.Apply (m, 0);
						w.Show ();
					}
					else w.Apply (m, 0);
					framed.Add (w);
				}
				foreach (var id in new List<uint> (wins.Keys))
					if (!keep.Contains (id)) { wins[id].GoneOnPi = true; wins[id].Close (); wins.Remove (id); }
				if (Form.ActiveForm == this) desk.TakeKeys ();		// (no Onyx window has the keys here: the desktop view takes them)
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

		// closed (its X): the session ends, and the program with it (unless a terminal is still open)
		protected override void OnFormClosed (FormClosedEventArgs e) { Cancel (); Conn?.Close (); base.OnFormClosed (e); Program.MaybeExit (this); }
	}

	static class Program
	{
		// The program's icon (onyxremote.ico in the .exe: tools/icons/onyxremote_icon.py), for every window
		static Icon appIcon; static bool appIconAsked;
		public static Icon AppIcon { get { if (!appIconAsked) { appIconAsked = true; try { appIcon = Icon.ExtractAssociatedIcon (Application.ExecutablePath); } catch { } } return appIcon; } }
		// The program's icon as a picture, the smallest of the .ico's at least that size (the
		// connection dialog's header; the .ico's pictures are PNGs, which Icon.ToBitmap does not
		// read at the small sizes); null: none
		public static Bitmap AppPicture (int size)
		{
			try
			{
				byte[] d;
				using (var st = typeof (Program).Assembly.GetManifestResourceStream ("onyxremote.ico"))
				using (var ms = new MemoryStream ()) { st.CopyTo (ms); d = ms.ToArray (); }
				int n = BitConverter.ToUInt16 (d, 4), best = -1, bestW = 0;
				for (int i = 0; i < n; i++)
				{
					int w = d[6 + 16 * i] == 0 ? 256 : d[6 + 16 * i];
					if (best < 0 || (bestW < size ? w > bestW : w >= size && w < bestW)) { best = i; bestW = w; }
				}
				int len = BitConverter.ToInt32 (d, 6 + 16 * best + 8), at = BitConverter.ToInt32 (d, 6 + 16 * best + 12);
				using (var ms = new MemoryStream (d, at, len))
				using (var b = new Bitmap (ms))
					return new Bitmap (b);
			}
			catch { try { return AppIcon?.ToBitmap (); } catch { return null; } }
		}

		// A telnet console on the Pi, in its own window: the address typed, port 23 ("host:port" for
		// another). false: no address.
		public static bool OpenTerminal (string address)
		{
			string h = (address ?? "").Trim (); int p = 23;
			int colon = h.LastIndexOf (':');
			if (colon > 0 && int.TryParse (h.Substring (colon + 1), out int pp)) { p = pp; h = h.Substring (0, colon); }
			if (h.Length == 0) return false;
			var f = new TelnetForm (h, p);
			f.FormClosed += (s, e) => MaybeExit (f);
			f.Show ();
			return true;
		}

		// A window closed: the program ends with its last one (the connection dialog, the session,
		// the terminals).
		public static void MaybeExit (Form closed)
		{
			foreach (Form f in Application.OpenForms)
				if (f != closed && f.Visible && (f is ConnectDialog || f is MainForm || f is TelnetForm)) return;
			Application.ExitThread ();
		}

		[STAThread]
		static void Main ()
		{
			Application.EnableVisualStyles ();
			Application.SetCompatibleTextRenderingDefault (false);
			var set = Settings.Load ();
			new ConnectDialog (set, new MainForm (set)).Show ();
			Application.Run ();
		}
	}
}
