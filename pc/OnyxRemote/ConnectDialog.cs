// ConnectDialog.cs -- Onyx Remote's first window: a small dark dialog asking where the Pi is. The
// address (with the last ones used in its drop-down), "Options" unfolding the port and what the
// session shows (the Onyx desktop, the Onyx frames, 16-bit colours); at the bottom "Full screen",
// Terminal (a telnet console, TelnetForm.cs: the dialog stays) and Connect (Enter). Connecting
// does not hold the dialog: Connect becomes Cancel, what happens is said under the options. Once
// connected it hides and the session's window shows (MainForm.cs); a disconnection brings it back.
// The settings (Settings: %APPDATA%\OnyxRemote.txt) are shared with the session's window.
// MIT licence.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace OnyxRemote
{
	// What is kept from one run to the next: "key=value" lines (recent= once per address, the last
	// used first).
	class Settings
	{
		public string Host = ""; public int Port = 3390;
		public bool Bits16, Desktop, Frames, Full, OptionsOpen;
		public readonly List<string> Recent = new List<string> ();
		const int MaxRecent = 8;
		static readonly string FilePath = Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.ApplicationData), "OnyxRemote.txt");

		public static Settings Load ()
		{
			var s = new Settings ();
			try
			{
				foreach (var line in File.ReadAllLines (FilePath))
				{
					int eq = line.IndexOf ('=');
					if (eq < 0) continue;
					string k = line.Substring (0, eq), v = line.Substring (eq + 1);
					if (k == "host") s.Host = v;
					else if (k == "port") s.Port = ParsePort (v);
					else if (k == "bits16") s.Bits16 = v == "1";
					else if (k == "desktop") s.Desktop = v == "1";
					else if (k == "frames") s.Frames = v == "1";
					else if (k == "fullscreen") s.Full = v == "1";
					else if (k == "options") s.OptionsOpen = v == "1";
					else if (k == "recent" && v.Trim ().Length > 0 && s.Recent.Count < MaxRecent && !s.Recent.Contains (v.Trim ())) s.Recent.Add (v.Trim ());
				}
			}
			catch { }
			if (s.Recent.Count == 0 && s.Host.Trim ().Length > 0) s.Recent.Add (s.Host.Trim ());	// (an older file)
			return s;
		}

		public static int ParsePort (string text)
		{
			int p; return int.TryParse (text, out p) && p > 0 && p <= 65535 ? p : 3390;
		}

		public void Save ()
		{
			var lines = new List<string> { "host=" + Host, "port=" + Port, "bits16=" + (Bits16 ? 1 : 0), "desktop=" + (Desktop ? 1 : 0),
				"frames=" + (Frames ? 1 : 0), "fullscreen=" + (Full ? 1 : 0), "options=" + (OptionsOpen ? 1 : 0) };
			foreach (var r in Recent) lines.Add ("recent=" + r);
			try { File.WriteAllLines (FilePath, lines); } catch { }
		}

		// an address used: first of the recent ones
		public void Remember (string address)
		{
			address = address.Trim ();
			if (address.Length == 0) return;
			Recent.RemoveAll (r => string.Equals (r, address, StringComparison.OrdinalIgnoreCase));
			Recent.Insert (0, address);
			if (Recent.Count > MaxRecent) Recent.RemoveRange (MaxRecent, Recent.Count - MaxRecent);
		}
	}

	// The dialog's colours and shapes
	static class Dark
	{
		public static readonly Color Back = Color.FromArgb (27, 30, 43), Foot = Color.FromArgb (22, 25, 36), Line = Color.FromArgb (44, 48, 68);
		public static readonly Color Field = Color.FromArgb (15, 17, 26), Edge = Color.FromArgb (69, 75, 102), EdgeHot = Color.FromArgb (104, 112, 146);
		public static readonly Color Text = Color.FromArgb (230, 232, 240), Dim = Color.FromArgb (170, 176, 196), Faint = Color.FromArgb (128, 136, 160);
		public static readonly Color Accent = Color.FromArgb (109, 90, 230), AccentHot = Color.FromArgb (128, 110, 240), AccentDown = Color.FromArgb (92, 74, 205);
		public static readonly Color Hot = Color.FromArgb (40, 44, 62), Down = Color.FromArgb (20, 22, 32), Warn = Color.FromArgb (244, 150, 130);

		public static GraphicsPath Round (RectangleF r, float radius)
		{
			var p = new GraphicsPath ();
			float d = 2 * radius;
			p.AddArc (r.X, r.Y, d, d, 180, 90);
			p.AddArc (r.Right - d, r.Y, d, d, 270, 90);
			p.AddArc (r.Right - d, r.Bottom - d, d, d, 0, 90);
			p.AddArc (r.X, r.Bottom - d, d, d, 90, 90);
			p.CloseFigure ();
			return p;
		}

		// a chevron in that square: down, or to the right
		public static void Chevron (Graphics g, RectangleF r, bool down, Color c, float width)
		{
			float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, a = r.Width * 0.22f;
			using (var p = new Pen (c, width) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
			{
				if (down) g.DrawLines (p, new[] { new PointF (cx - a, cy - a / 2), new PointF (cx, cy + a / 2), new PointF (cx + a, cy - a / 2) });
				else g.DrawLines (p, new[] { new PointF (cx - a / 2, cy - a), new PointF (cx + a / 2, cy), new PointF (cx - a / 2, cy + a) });
			}
		}
	}

	// A button: rounded, filled with the accent (Primary), outlined, or bare with a chevron (Arrow:
	// the address' drop-down)
	class DarkButton : Button
	{
		public bool Primary, Arrow;
		bool hot, down;
		public DarkButton () { SetStyle (ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true); }
		protected override void OnMouseEnter (EventArgs e) { hot = true; Invalidate (); base.OnMouseEnter (e); }
		protected override void OnMouseLeave (EventArgs e) { hot = down = false; Invalidate (); base.OnMouseLeave (e); }
		protected override void OnMouseDown (MouseEventArgs e) { down = true; Invalidate (); base.OnMouseDown (e); }
		protected override void OnMouseUp (MouseEventArgs e) { down = false; Invalidate (); base.OnMouseUp (e); }
		protected override void OnEnabledChanged (EventArgs e) { hot = down = false; Invalidate (); base.OnEnabledChanged (e); }
		protected override void OnPaint (PaintEventArgs e)
		{
			var g = e.Graphics;
			float k = DeviceDpi / 96f;
			g.Clear (BackColor);
			g.SmoothingMode = SmoothingMode.AntiAlias;
			var r = new RectangleF (0.5f, 0.5f, Width - 1, Height - 1);
			if (Arrow)
			{
				if (Enabled && (hot || Focused && ShowFocusCues))
					using (var p = Dark.Round (r, 3 * k)) using (var b = new SolidBrush (down ? Dark.Down : Dark.Hot)) g.FillPath (b, p);
				Dark.Chevron (g, new RectangleF ((Width - Height) / 2f, 0, Height, Height), true, Enabled ? Dark.Dim : Dark.Edge, 1.6f * k);
				return;
			}
			Color fill = Primary ? (down ? Dark.AccentDown : hot ? Dark.AccentHot : Dark.Accent) : (down ? Dark.Down : hot ? Dark.Hot : BackColor);
			Color edge = Primary ? fill : hot ? Dark.EdgeHot : Dark.Edge, text = Primary ? Color.White : Dark.Text;
			if (!Enabled) { fill = BackColor; edge = Dark.Line; text = Dark.Edge; }
			using (var p = Dark.Round (r, 4 * k))
			{
				using (var b = new SolidBrush (fill)) g.FillPath (b, p);
				using (var pen = new Pen (edge)) g.DrawPath (pen, p);
			}
			if (Focused && ShowFocusCues)
				using (var p = Dark.Round (new RectangleF (2.5f, 2.5f, Width - 5, Height - 5), 3 * k))
				using (var pen = new Pen (Primary ? Color.White : Dark.Accent)) g.DrawPath (pen, p);
			TextRenderer.DrawText (g, Text, Font, ClientRectangle, text, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
		}
	}

	// A check box: a rounded box filled with the accent when checked, its text, a dimmer hint after
	// it; or (Arrow) a chevron to the right / down: what unfolds the options
	class DarkCheck : CheckBox
	{
		public bool Arrow; public string Hint;
		bool hot;
		public DarkCheck () { SetStyle (ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true); }
		protected override void OnMouseEnter (EventArgs e) { hot = true; Invalidate (); base.OnMouseEnter (e); }
		protected override void OnMouseLeave (EventArgs e) { hot = false; Invalidate (); base.OnMouseLeave (e); }
		protected override void OnPaint (PaintEventArgs e)
		{
			var g = e.Graphics;
			float k = DeviceDpi / 96f;
			g.Clear (BackColor);
			g.SmoothingMode = SmoothingMode.AntiAlias;
			int side = (int) Math.Round (15 * k), top = (Height - side) / 2;
			var box = new RectangleF (0.5f, top + 0.5f, side - 1, side - 1);
			Color text = !Enabled ? Dark.Edge : Arrow && !Checked && !hot ? Dark.Dim : Dark.Text;
			if (Arrow) Dark.Chevron (g, new RectangleF (-1 * k, top, side, side), Checked, text, 1.6f * k);
			else
				using (var p = Dark.Round (box, 3 * k))
				{
					bool on = Checked && Enabled;
					using (var b = new SolidBrush (on ? Dark.Accent : Dark.Field)) g.FillPath (b, p);
					using (var pen = new Pen (on ? Dark.Accent : !Enabled ? Dark.Line : hot ? Dark.EdgeHot : Dark.Edge)) g.DrawPath (pen, p);
					if (Checked)
						using (var pen = new Pen (Enabled ? Color.White : Dark.Edge, 1.7f * k) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
							g.DrawLines (pen, new[] { new PointF (side * 0.26f, top + side * 0.52f), new PointF (side * 0.43f, top + side * 0.69f), new PointF (side * 0.75f, top + side * 0.33f) });
				}
			int x = side + (int) Math.Round ((Arrow ? 3 : 8) * k);
			const TextFormatFlags flags = TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.NoPadding;
			var size = TextRenderer.MeasureText (g, Text, Font, Size.Empty, flags);
			TextRenderer.DrawText (g, Text, Font, new Rectangle (x, 0, size.Width, Height), text, flags);
			if (Hint != null)
				TextRenderer.DrawText (g, Hint, Font, new Rectangle (x + size.Width + (int) Math.Round (8 * k), 0, Width, Height), Enabled ? Dark.Faint : Dark.Edge, flags);
			if (Focused && ShowFocusCues)
				using (var pen = new Pen (Dark.Accent))
				using (var p = Dark.Round (new RectangleF (x - 3 * k, 1.5f, Math.Min (size.Width + 6 * k, Width - x + 3 * k - 1), Height - 3), 3 * k)) g.DrawPath (pen, p);
		}
	}

	// A text field's frame: rounded, the accent around it while what it holds has the focus
	class FieldBox : Panel
	{
		public FieldBox ()
		{
			SetStyle (ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
			ControlAdded += (s, e) => { e.Control.Enter += (s2, e2) => Invalidate (); e.Control.Leave += (s2, e2) => Invalidate (); };
		}
		protected override void OnPaint (PaintEventArgs e)
		{
			var g = e.Graphics;
			g.Clear (BackColor);
			g.SmoothingMode = SmoothingMode.AntiAlias;
			using (var p = Dark.Round (new RectangleF (0.5f, 0.5f, Width - 1, Height - 1), 4 * DeviceDpi / 96f))
			{
				using (var b = new SolidBrush (Dark.Field)) g.FillPath (b, p);
				using (var pen = new Pen (!Enabled ? Dark.Line : ContainsFocus ? Dark.Accent : Dark.Edge)) g.DrawPath (pen, p);
			}
		}
	}

	// the drop-down of the recent addresses
	class DarkMenuColors : ProfessionalColorTable
	{
		public override Color ToolStripDropDownBackground { get { return Dark.Field; } }
		public override Color MenuBorder { get { return Dark.Edge; } }
		public override Color MenuItemBorder { get { return Dark.Accent; } }
		public override Color MenuItemSelected { get { return Dark.Accent; } }
		public override Color ImageMarginGradientBegin { get { return Dark.Field; } }
		public override Color ImageMarginGradientMiddle { get { return Dark.Field; } }
		public override Color ImageMarginGradientEnd { get { return Dark.Field; } }
	}

	class ConnectDialog : Form
	{
		readonly Settings set; readonly MainForm main;
		readonly float k;					// (the screen's scale: 1 at 96 dpi)
		readonly FieldBox hostBox = new FieldBox (), portBox = new FieldBox ();
		readonly TextBox host = new TextBox (), port = new TextBox ();
		readonly DarkButton recent = new DarkButton { Arrow = true, TabStop = false };
		readonly DarkCheck options = new DarkCheck { Arrow = true, Text = "Options" };
		readonly DarkCheck desktop = new DarkCheck { Text = "Show the Onyx desktop" };
		readonly DarkCheck frames = new DarkCheck { Text = "Onyx window frames" };
		readonly DarkCheck bits16 = new DarkCheck { Text = "16-bit colours", Hint = "faster on a slow link" };
		readonly DarkCheck full = new DarkCheck { Text = "Full screen" };
		readonly DarkButton terminal = new DarkButton { Text = "Terminal" };
		readonly DarkButton connect = new DarkButton { Text = "Connect", Primary = true };
		readonly Label status = new Label ();
		readonly ContextMenuStrip menu = new ContextMenuStrip ();
		readonly ToolTip tips = new ToolTip ();
		readonly Font titleFont = new Font ("Segoe UI Semibold", 13f), smallFont = new Font ("Segoe UI", 8.5f);
		readonly Bitmap picture;
		Rectangle optionsBox, foot;				// (painted: the options' panel, the bottom band)
		bool busy;						// (connecting: Connect is Cancel)
		const int W = 340, Margin0 = 18;

		int S (int v) { return (int) Math.Round (v * k); }

		public ConnectDialog (Settings settings, MainForm session)
		{
			set = settings; main = session;
			Text = "Onyx Remote";
			if (Program.AppIcon != null) Icon = Program.AppIcon;
			AutoScaleMode = AutoScaleMode.None;
			FormBorderStyle = FormBorderStyle.FixedSingle; MaximizeBox = false;
			StartPosition = FormStartPosition.CenterScreen;
			DoubleBuffered = true;
			BackColor = Dark.Back; ForeColor = Dark.Text;
			Font = new Font ("Segoe UI", 9.5f);
			k = DeviceDpi / 96f;
			picture = Program.AppPicture (S (40));

			host.BorderStyle = port.BorderStyle = BorderStyle.None;
			host.BackColor = port.BackColor = Dark.Field; host.ForeColor = port.ForeColor = Dark.Text;
			host.Font = new Font ("Segoe UI", 10.5f);
			port.MaxLength = 5;
			port.KeyPress += (s, e) => { if (!char.IsControl (e.KeyChar) && !char.IsDigit (e.KeyChar)) e.Handled = true; };
			host.TextChanged += (s, e) => { if (!busy) Say ("", false); };
			host.KeyDown += (s, e) => { if (e.KeyCode == Keys.Down && recent.Enabled) { e.SuppressKeyPress = true; ShowRecent (); } };
			hostBox.Controls.Add (host); hostBox.Controls.Add (recent);
			hostBox.Click += (s, e) => host.Focus ();
			portBox.Controls.Add (port);
			portBox.Click += (s, e) => port.Focus ();
			recent.BackColor = Dark.Field;
			recent.Click += (s, e) => ShowRecent ();
			menu.Renderer = new ToolStripProfessionalRenderer (new DarkMenuColors ()) { RoundedEdges = false };
			menu.ShowImageMargin = false; menu.Font = host.Font; menu.BackColor = Dark.Field;
			status.AutoEllipsis = true; status.Font = smallFont; status.ForeColor = Dark.Dim;
			desktop.BackColor = frames.BackColor = bits16.BackColor = portBox.BackColor = Dark.Foot;
			full.BackColor = terminal.BackColor = connect.BackColor = Dark.Foot;
			tips.SetToolTip (desktop, "The wallpaper and the widgets behind the Onyx windows");
			tips.SetToolTip (frames, "The windows with the frames Onyx draws, instead of none");
			tips.SetToolTip (full, "The Onyx session over the whole screen (F11 toggles it)");
			tips.SetToolTip (terminal, "A telnet console on the Pi (the Onyx shell; telnetd, port 23)");
			tips.SetToolTip (recent, "The last addresses used");

			host.Text = set.Host; port.Text = set.Port.ToString ();
			desktop.Checked = set.Desktop; frames.Checked = set.Frames; bits16.Checked = set.Bits16; full.Checked = set.Full;
			options.Checked = set.OptionsOpen;
			options.CheckedChanged += (s, e) => { set.OptionsOpen = options.Checked; set.Save (); Arrange (); };
			connect.Click += (s, e) => { if (busy) Cancel (); else Connect (); };
			terminal.Click += (s, e) => Terminal ();
			main.Ended += why => Back (why);

			// (the tab order: the address, the options, what they hold, the bottom row)
			Controls.AddRange (new Control[] { hostBox, options, portBox, desktop, frames, bits16, status, full, terminal, connect });
			AcceptButton = connect;
			Arrange ();
			recent.Enabled = set.Recent.Count > 0;
			Shown += (s, e) => { host.Focus (); host.SelectAll (); };
		}

		// Where everything is: the options' panel only when unfolded, the window as tall as needed.
		void Arrange ()
		{
			int m = S (Margin0), w = S (W) - 2 * m, y = S (98);
			hostBox.SetBounds (m, y, w, S (34));
			int side = hostBox.Height - 2 * S (3);
			recent.SetBounds (hostBox.Width - side - S (3), S (3), side, side);
			host.SetBounds (S (9), (hostBox.Height - host.PreferredHeight) / 2, recent.Left - S (9) - S (4), host.PreferredHeight);
			y += hostBox.Height + S (10);
			options.SetBounds (m, y, S (90), S (24));
			y += options.Height + S (4);
			bool open = options.Checked;
			portBox.Visible = desktop.Visible = frames.Visible = bits16.Visible = open;
			if (open)
			{
				int pad = S (12), x = m + pad, row = S (26);
				optionsBox = new Rectangle (m, y, w, pad + S (28) + S (8) + 3 * row + pad - S (4));
				portBox.SetBounds (m + w - pad - S (72), y + pad, S (72), S (28));
				port.SetBounds (S (8), (portBox.Height - port.PreferredHeight) / 2, portBox.Width - S (16), port.PreferredHeight);
				int cy = y + pad + S (28) + S (8);
				desktop.SetBounds (x, cy, w - 2 * pad, row - S (2)); cy += row;
				frames.SetBounds (x, cy, w - 2 * pad, row - S (2)); cy += row;
				bits16.SetBounds (x, cy, w - 2 * pad, row - S (2));
				y = optionsBox.Bottom + S (8);
			}
			else optionsBox = Rectangle.Empty;
			status.SetBounds (m, y, w, S (34));
			y += status.Height + S (8);
			foot = new Rectangle (0, y, S (W), S (54));
			int bh = S (30), by = y + (foot.Height - bh) / 2 + 1;
			connect.SetBounds (S (W) - m - S (92), by, S (92), bh);
			terminal.SetBounds (connect.Left - S (8) - S (92), by, S (92), bh);
			full.SetBounds (m, by, terminal.Left - m - S (8), bh);
			ClientSize = new Size (S (W), foot.Bottom);
			Invalidate ();
		}

		// the header (the icon, the name), the labels, the options' panel, the bottom band
		protected override void OnPaint (PaintEventArgs e)
		{
			var g = e.Graphics;
			int m = S (Margin0);
			const TextFormatFlags flags = TextFormatFlags.NoPadding | TextFormatFlags.SingleLine;
			int tx = m;
			if (picture != null)
			{
				g.InterpolationMode = InterpolationMode.HighQualityBicubic;
				g.DrawImage (picture, new Rectangle (m, S (18), S (40), S (40)));
				tx = m + S (40) + S (12);
			}
			TextRenderer.DrawText (g, "Onyx Remote", titleFont, new Point (tx, S (15)), Dark.Text, flags);
			TextRenderer.DrawText (g, "Connect to your Raspberry Pi", smallFont, new Point (tx + 1, S (41)), Dark.Faint, flags);
			TextRenderer.DrawText (g, "Address", smallFont, new Point (m, S (78)), Dark.Dim, flags);
			g.SmoothingMode = SmoothingMode.AntiAlias;
			if (!optionsBox.IsEmpty)
			{
				using (var p = Dark.Round (new RectangleF (optionsBox.X + 0.5f, optionsBox.Y + 0.5f, optionsBox.Width - 1, optionsBox.Height - 1), 6 * k))
				{
					using (var b = new SolidBrush (Dark.Foot)) g.FillPath (b, p);
					using (var pen = new Pen (Dark.Line)) g.DrawPath (pen, p);
				}
				TextRenderer.DrawText (g, "Port", Font, new Rectangle (optionsBox.X + S (12), portBox.Top, S (80), portBox.Height), Dark.Dim,
						       flags | TextFormatFlags.VerticalCenter);
			}
			g.SmoothingMode = SmoothingMode.Default;
			using (var b = new SolidBrush (Dark.Foot)) g.FillRectangle (b, foot);
			using (var pen = new Pen (Dark.Line)) g.DrawLine (pen, 0, foot.Top, foot.Right, foot.Top);
		}

		void Say (string what, bool wrong)
		{
			status.ForeColor = wrong ? Dark.Warn : Dark.Dim;
			status.Text = what;
			tips.SetToolTip (status, what.Length > 60 ? what : null);
		}

		// the recent addresses, under the address
		void ShowRecent ()
		{
			menu.Items.Clear ();
			foreach (var r in set.Recent)
			{
				string address = r;
				var it = new ToolStripMenuItem (address) { ForeColor = Dark.Text, AutoSize = false, Size = new Size (hostBox.Width - 2, S (28)) };
				it.Click += (s, e) => { host.Text = address; host.Focus (); host.SelectionStart = host.TextLength; };
				menu.Items.Add (it);
			}
			if (menu.Items.Count > 0) menu.Show (hostBox, new Point (0, hostBox.Height + 1));
		}

		// what the dialog says into the settings; false: no address
		bool Store ()
		{
			set.Host = host.Text.Trim (); set.Port = Settings.ParsePort (port.Text);
			port.Text = set.Port.ToString ();
			set.Desktop = desktop.Checked; set.Frames = frames.Checked; set.Bits16 = bits16.Checked; set.Full = full.Checked;
			if (set.Host.Length == 0)
			{
				Say ("Enter the Pi's address first.", true);
				host.Focus ();
				return false;
			}
			set.Save ();
			return true;
		}

		void Remember ()
		{
			set.Remember (set.Host); set.Save ();
			recent.Enabled = true;
		}

		void Terminal ()
		{
			if (!Store ()) return;
			if (Program.OpenTerminal (set.Host)) Remember ();
		}

		void Busy (bool on)
		{
			busy = on;
			hostBox.Enabled = options.Enabled = portBox.Enabled = desktop.Enabled = frames.Enabled = bits16.Enabled = full.Enabled = terminal.Enabled = !on;
			host.ForeColor = on ? Dark.Faint : Dark.Text;
			connect.Text = on ? "Cancel" : "Connect"; connect.Primary = !on;
			connect.Invalidate (); hostBox.Invalidate ();
		}

		void Connect ()
		{
			if (!Store ()) return;
			Busy (true);
			connect.Focus ();
			Say ("Connecting to " + set.Host + "…", false);
			main.Begin (Bounds, why =>
			{
				if (!busy) return;
				Busy (false);
				if (why == null) { Remember (); Say ("", false); Hide (); }
				else { Say ("Couldn't connect: " + why, true); host.Focus (); host.SelectAll (); }
			});
		}

		void Cancel ()
		{
			main.Cancel ();
			Busy (false);
			Say ("", false);
			host.Focus ();
		}

		// the session ended (why): back, with what its tool bar changed
		void Back (string why)
		{
			desktop.Checked = set.Desktop; full.Checked = set.Full;
			Say (why == "Disconnected" ? "" : why ?? "", why != "Disconnected");
			if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
			Show ();
			Activate ();
			host.Focus (); host.SelectAll ();
		}

		// Esc gives a connection under way up
		protected override bool ProcessCmdKey (ref Message msg, Keys keyData)
		{
			if (keyData == Keys.Escape && busy) { Cancel (); return true; }
			return base.ProcessCmdKey (ref msg, keyData);
		}

		// the title bar dark too (Windows 10 2004 and later; ignored before)
		protected override void OnHandleCreated (EventArgs e)
		{
			base.OnHandleCreated (e);
			try { int on = 1; DwmSetWindowAttribute (Handle, 20, ref on, 4); } catch { }
		}

		protected override void OnFormClosed (FormClosedEventArgs e)
		{
			if (busy) main.Cancel ();
			base.OnFormClosed (e);
			Program.MaybeExit (this);
		}

		[DllImport ("dwmapi.dll")]
		static extern int DwmSetWindowAttribute (IntPtr hwnd, int attribute, ref int value, int size);
	}
}
