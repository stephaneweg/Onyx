// SimForm.cs -- PocketTerm Simulator's window: the device's face (Face.cs), a menu, a line that says which
// PC keys press the pad's buttons, the status line. The session is rdpd's (port 3390) through Onyx
// Remote's own Connection.cs; the Pi's screen is composited here (Compositor.cs) after each round.
// The PC's keyboard types into the session as in Onyx Remote (the text as characters with the PC's
// layout, the special keys and the modifiers as keysyms), except the keys the mapping's [pc] section
// gives to a pad button.
// MIT licence (Onyx).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using OnyxRemote;

namespace PocketTermSim
{
	class SimForm : Form
	{
		public readonly FaceView Face = new FaceView ();
		public readonly Device Dev;
		public Connection Conn;
		public readonly Settings Set = Settings.Load ();
		readonly MenuStrip menu = new MenuStrip ();
		readonly Label banner = new Label (), legend = new Label ();
		readonly StatusStrip status = new StatusStrip ();
		readonly ToolStripStatusLabel stLink = new ToolStripStatusLabel ("Not connected"), stSize = new ToolStripStatusLabel (""), stFps = new ToolStripStatusLabel (""),
			stMods = new ToolStripStatusLabel (""), stScale = new ToolStripStatusLabel ("");
		readonly System.Windows.Forms.Timer second = new System.Windows.Forms.Timer { Interval = 1000 };
		ToolStripMenuItem miConnect, miDisconnect, miSmooth, miMouse, miTop, miGame;
		readonly List<ToolStripMenuItem> scaleItems = new List<ToolStripMenuItem> (), layoutItems = new List<ToolStripMenuItem> ();
		Bitmap remote; int[] scr;
		int rounds, fps;
		string host = ""; int port = 3390;
		string linkNote;
		string toast; DateTime toastUntil;		// a short word in the status line (a combination the device keeps)
		public readonly List<string> Notes = new List<string> ();	// (the tests read them)
		readonly HashSet<Keys> pcDown = new HashSet<Keys> ();
		public string MappingPath;
		bool bannerOn;
		public Size AvailOverride;		// (the tests: another monitor's room)
		public Bitmap RemotePicture { get { return remote; } }

		public static readonly float[] Scales = { 1f, 1.5f, 2f, 0f, -1f };	// 0: the device's true size; -1: fitted to the monitor
		static readonly string[] ScaleNames = { "1x  (640 x 480, pixel for pixel)", "1.5x  (960 x 720)", "2x  (1280 x 960)", "True size  (3.5 inches on this monitor)", "Fit the whole device to this monitor" };
		readonly Panel scroller = new Panel { Dock = DockStyle.Fill, AutoScroll = true, BackColor = Color.FromArgb (236, 237, 240) };

		public SimForm (string mappingPath)
		{
			MappingPath = mappingPath;
			Dev = new Device (Mapping.Load (mappingPath));
			Dev.Send = (down, sym) => { var c = Conn; if (c != null) c.Key (down, false, sym); };
			Dev.Changed += () => { Face.Invalidate (); stMods.Text = Dev.ModsText (); };
			Dev.Note += what =>
			{
				Notes.Add (what);
				toast = "Fn: " + what + "  --  handled by the device itself, nothing is sent to Onyx";
				toastUntil = DateTime.Now.AddSeconds (4);
				stLink.Text = toast;
			};
			Face.Dev = Dev;
			Face.Pointer += (x, y, b, w) => { var c = Conn; if (c != null && Set.Mouse) c.Pointer (WinModel.DESKTOP_ID, x, y, b, w); };

			Text = "PocketTerm Simulator";
			FormBorderStyle = FormBorderStyle.FixedSingle; MaximizeBox = false;
			StartPosition = FormStartPosition.Manual;
			KeyPreview = true;
			try { using (var s = typeof (SimForm).Assembly.GetManifestResourceStream ("onyxremote.ico")) if (s != null) Icon = new Icon (s); } catch { }
			BuildMenu ();
			banner.Dock = DockStyle.Top; banner.AutoSize = false; banner.Height = 40; banner.Visible = false;
			banner.BackColor = Color.FromArgb (0xF1, 0xB3, 0x3B); banner.ForeColor = Color.FromArgb (0x1B, 0x1C, 0x1F);
			banner.Padding = new Padding (10, 0, 10, 0); banner.TextAlign = ContentAlignment.MiddleLeft; banner.Font = new Font ("Segoe UI", 9f);
			legend.Dock = DockStyle.Bottom; legend.AutoSize = false; legend.Height = 34; legend.TextAlign = ContentAlignment.MiddleCenter;
			legend.BackColor = Color.FromArgb (236, 237, 240); legend.ForeColor = Color.FromArgb (70, 74, 84); legend.Font = new Font ("Segoe UI", 8.25f);
			status.SizingGrip = false;
			stLink.Spring = true; stLink.TextAlign = ContentAlignment.MiddleLeft;
			status.Items.AddRange (new ToolStripItem[] { stLink, stMods, stSize, stFps, stScale });
			stMods.ForeColor = Color.FromArgb (0x1F, 0x5F, 0xB0);
			scroller.Controls.Add (Face);
			Controls.Add (scroller); Controls.Add (banner); Controls.Add (legend); Controls.Add (status); Controls.Add (menu);
			MainMenuStrip = menu;
			second.Tick += (s, e) => { fps = rounds; rounds = 0; ShowStatus (); };
			second.Start ();
			Relayout (true);
			ShowLegend ();
			ShowStatus ();
		}

		// ---- the menu ----
		ToolStripMenuItem Item (string text, EventHandler click, Keys shortcut = Keys.None)
		{
			var i = new ToolStripMenuItem (text, null, click);
			if (shortcut != Keys.None) { i.ShortcutKeys = shortcut; i.ShowShortcutKeys = true; }
			return i;
		}

		void BuildMenu ()
		{
			var session = new ToolStripMenuItem ("Session");
			miConnect = Item ("Connect...", (s, e) => AskConnect ());
			miDisconnect = Item ("Disconnect", (s, e) => Disconnect (null));
			session.DropDownItems.AddRange (new ToolStripItem[] { miConnect, miDisconnect, new ToolStripSeparator (),
				Item ("Screenshot to PNG", (s, e) => Shot (false), Keys.Control | Keys.Shift | Keys.S),
				Item ("Screenshot with the device", (s, e) => Shot (true), Keys.Control | Keys.Shift | Keys.D),
				Item ("Open the screenshots' folder", (s, e) => { try { Directory.CreateDirectory (ShotFolder); Process.Start ("explorer.exe", "\"" + ShotFolder + "\""); } catch { } }),
				new ToolStripSeparator (), Item ("Exit", (s, e) => Close ()) });
			var view = new ToolStripMenuItem ("View");
			for (int i = 0; i < Scales.Length; i++)
			{
				int n = i;
				var it = Item (ScaleNames[i], (s, e) => { Set.Scale = n; Relayout (false); });
				scaleItems.Add (it); view.DropDownItems.Add (it);
			}
			view.DropDownItems.Add (new ToolStripSeparator ());
			miSmooth = Item ("Smooth scaling (2x: off = sharp pixels)", (s, e) => { Set.Smooth = !Set.Smooth; Relayout (false); });
			view.DropDownItems.Add (miSmooth);
			view.DropDownItems.Add (new ToolStripSeparator ());
			string[] ln = { "The device's shape (it scrolls when taller than the monitor)", "Controls beside the screen (a low, wide window)" };
			for (int i = 0; i < 2; i++)
			{
				int n = i;
				var it = Item (ln[i], (s, e) => { Set.Layout = n; Relayout (false); });
				layoutItems.Add (it); view.DropDownItems.Add (it);
			}
			view.DropDownItems.Add (new ToolStripSeparator ());
			miMouse = Item ("The mouse acts on the screen (the device has no pointer)", (s, e) => { Set.Mouse = !Set.Mouse; Relayout (false); });
			miTop = Item ("Always on top", (s, e) => { Set.Top = !Set.Top; Relayout (false); });
			view.DropDownItems.Add (miMouse); view.DropDownItems.Add (miTop);
			var device = new ToolStripMenuItem ("Device");
			device.DropDownItems.AddRange (new ToolStripItem[] {
				Item ("Release all modifiers", (s, e) => Dev.ReleaseAll ()),
				new ToolStripSeparator (),
				Item ("Edit the button mapping...", (s, e) => { try { Process.Start ("notepad.exe", "\"" + MappingPath + "\""); } catch (Exception x) { MessageBox.Show (this, x.Message, Text); } }),
				Item ("Reload the mapping", (s, e) => ReloadMapping ()),
				(miGame = Item ("Game keys (the pad as the emulators' keyboard)", (s, e) => { Dev.GameKeys = !Dev.GameKeys; miGame.Checked = Dev.GameKeys; Invalidate (true); }, Keys.F11)) });
			var help = new ToolStripMenuItem ("Help");
			help.DropDownItems.AddRange (new ToolStripItem[] {
				Item ("What each control sends...", (s, e) => ShowText ("What each control sends", SendsTable ())),
				Item ("How to get a 640 x 480 screen...", (s, e) => ShowText ("A 640 x 480 screen on the Pi", HowTo640)),
				Item ("About", (s, e) => MessageBox.Show (this, "PocketTerm Simulator\n\nThe Waveshare PocketTerm35's face on a PC, to try Onyx's pocket interface before the device is there: the Onyx session through rdpd (port 3390) under the glass, the device's controls below.\n\nPart of Onyx -- MIT licence.", Text)) });
			menu.Items.AddRange (new ToolStripItem[] { session, view, device, help });
		}

		public const string HowTo640 =
			"The device's screen is 640 x 480. rdpd's protocol tells the client the Pi's screen size but has no message to change it, so it is set on the Pi:\r\n\r\n" +
			"  - in SD:/cmdline.txt (the card's first partition), put  width=640 height=480  in the line, then restart the Pi;\r\n" +
			"  - the Control Panel's Display applet changes the size at once, but its list starts at 1024 x 768 today.\r\n\r\n" +
			"The pocket interface itself is the system's mode: Control Panel > Mode > Pocket (SD:/etc/system.ini, shell=pocket).\r\n\r\n" +
			"Meanwhile a screen of another size is shown whole, scaled into the glass (not pixel for pixel).";

		void ShowText (string title, string text)
		{
			var f = new Form { Text = title, StartPosition = FormStartPosition.CenterParent, ClientSize = new Size (760, 560), MinimizeBox = false, ShowInTaskbar = false, TopMost = TopMost };
			var t = new TextBox { Multiline = true, ReadOnly = true, Dock = DockStyle.Fill, ScrollBars = ScrollBars.Vertical, Font = new Font ("Consolas", 9.5f), Text = text, BackColor = Color.White, WordWrap = true };
			f.Controls.Add (t);
			f.Shown += (s, e) => t.Select (0, 0);
			f.ShowDialog (this);
		}

		// The table of what every control sends (Help; `--sends` prints it).
		public string SendsTable ()
		{
			var sb = new StringBuilder ();
			sb.AppendLine ("rdpd has no gamepad message: every control sends rdpd's KEY message (type 3: flags 1 = down / 0 = up, an X11 keysym).");
			sb.AppendLine ("The mapping: " + MappingPath);
			sb.AppendLine ();
			sb.AppendLine ("The pad's buttons                       pressed -> released                                        PC key");
			foreach (string b in Mapping.PadButtons)
			{
				Chord c;
				if (!Dev.Map.Buttons.TryGetValue (b, out c)) c = new Chord ();
				string pc = Dev.Map.PcKeyOf (b);
				sb.AppendLine (string.Format ("  {0,-7} = {1,-14} {2,-74} {3}", b, c.Text, Wire (c), pc));
			}
			sb.AppendLine ();
			sb.AppendLine ("The thumb keyboard");
			sb.AppendLine ("  a character key      KEY down, KEY up: the keysym is the character itself (Latin-1), Shift / Caps applied here: a A 1 ! [ {");
			sb.AppendLine ("  Tab 0xFF09   Enter 0xFF0D   Backspace 0xFF08   Space 0x0020   (no arrow keys: the D-pad is the arrows)");
			sb.AppendLine ("  Shift 0xFFE1   Ctrl 0xFFE3   Alt 0xFFE9 (left) 0xFFEA (right)   CMD 0xFFEB (Super)");
			sb.AppendLine ("  a modifier: one click = down, released after the next key; two clicks = locked; a third = up.");
			sb.AppendLine ("  Caps and Fn send nothing themselves. Held with the mouse, a key repeats its down (400 ms, then every 60 ms).");
			sb.AppendLine ();
			sb.AppendLine ("The Fn layer (Fn + key; the blue legends of the maker's diagram)");
			var fnNames = new List<string> ();
			foreach (var row in Dev.Rows) foreach (var k in row) fnNames.Add (k.Name);
			fnNames.Add ("Select"); fnNames.Add ("Start");
			foreach (string name in fnNames)
			{
				Chord c = Dev.FnOf (name);
				if (c != null) sb.AppendLine (string.Format ("  Fn + {0,-10} = {1,-22} {2}", name, c.Text, c.Local != null ? "(nothing: the device does it itself)" : Wire (c)));
			}
			if (Dev.Map.Problems.Count > 0) { sb.AppendLine (); sb.AppendLine ("Not understood in the mapping file:"); foreach (string p in Dev.Map.Problems) sb.AppendLine ("  " + p); }
			return sb.ToString ();
		}
		static string Wire (Chord c)
		{
			if (c.Empty) return "(nothing)";
			var d = new List<string> (); var u = new List<string> ();
			foreach (uint m in c.Mods) d.Add (Mapping.SymName (m) + " down");
			if (c.Key != 0) { d.Add (Mapping.SymName (c.Key) + " down"); u.Add (Mapping.SymName (c.Key) + " up"); }
			for (int i = c.Mods.Count - 1; i >= 0; i--) u.Add (Mapping.SymName (c.Mods[i]) + " up");
			return string.Join (", ", d.ToArray ()) + " -> " + string.Join (", ", u.ToArray ());
		}

		void ReloadMapping ()
		{
			Dev.ReleaseAll ();
			Dev.Map = Mapping.Load (MappingPath);
			ShowLegend ();
			Face.Invalidate ();
			if (Dev.Map.Problems.Count > 0) MessageBox.Show (this, "Not understood in " + MappingPath + ":\n\n" + string.Join ("\n", Dev.Map.Problems.ToArray ()), Text);
		}

		void ShowLegend ()
		{
			var s = new List<string> { "PC keys: typed into Onyx", "arrows = D-pad", "Enter = A", "Esc = B" };
			foreach (string b in new[] { "X", "Y", "L", "R", "Select", "Start" })
			{
				string k = Dev.Map.PcKeyOf (b);
				if (k.Length > 0) s.Add (k + " = " + b);
			}
			legend.Text = string.Join ("  ·  ", s.ToArray ());
		}

		// ---- the layout ----
		float fitScale = 1;
		public float ScaleNow
		{
			get
			{
				float s = Scales[Math.Max (0, Math.Min (Scales.Length - 1, Set.Scale))];
				return s > 0 ? s : s == 0 ? DeviceDpi / FaceLayout.PpiDevice : fitScale;
			}
		}

		// The window takes the face's size; taller (or wider) than the monitor's working area, it stops there
		// and the face scrolls inside it.
		public void Relayout (bool first)
		{
			for (int i = 0; i < scaleItems.Count; i++) scaleItems[i].Checked = i == Set.Scale;
			for (int i = 0; i < layoutItems.Count; i++) layoutItems[i].Checked = i == Set.Layout;
			miSmooth.Checked = Set.Smooth; miMouse.Checked = Set.Mouse; miTop.Checked = Set.Top;
			TopMost = Set.Top;
			Face.Smooth = Set.Smooth;
			Rectangle work = System.Windows.Forms.Screen.FromControl (this).WorkingArea;
			int bars = menu.Height + legend.Height + status.Height + (bannerOn ? banner.Height : 0);
			Size room = AvailOverride.IsEmpty ? new Size (work.Width - (Width - ClientSize.Width), work.Height - (Height - ClientSize.Height) - bars) : AvailOverride;
			Size one = FaceLayout.Make (Dev, 1, Set.Layout == 1).Client;	// (fitted: both grow with the scale)
			fitScale = Math.Max (0.3f, Math.Min (3f, Math.Min ((room.Height - 2f * FaceLayout.Margin) / (one.Height - 2f * FaceLayout.Margin), (room.Width - 2f * FaceLayout.Margin) / (one.Width - 2f * FaceLayout.Margin))));
			Face.L = FaceLayout.Make (Dev, ScaleNow, Set.Layout == 1);
			Size face = Face.L.Client;
			bool vbar = face.Height > room.Height, hbar = face.Width + (vbar ? SystemInformation.VerticalScrollBarWidth : 0) > room.Width;
			Face.Bounds = new Rectangle (scroller.AutoScrollPosition, face);
			ClientSize = new Size (Math.Min (room.Width, face.Width + (vbar ? SystemInformation.VerticalScrollBarWidth : 0)),
					       Math.Min (room.Height, face.Height + (hbar ? SystemInformation.HorizontalScrollBarHeight : 0)) + bars);
			Scrolls = vbar || hbar;
			if (first) Location = new Point (work.X + Math.Max (0, (work.Width - Width) / 2), work.Y + Math.Max (0, (work.Height - Height) / 2));
			else Location = new Point (Math.Max (work.X - 8, Math.Min (Left, work.Right - Width + 8)), Math.Max (work.Y, Math.Min (Top, work.Bottom - Height)));
			stScale.Text = (Set.Scale == 3 ? "true size" : ScaleNow.ToString ("0.##") + "x") + (Face.L.Beside ? ", beside" : "") + (Scrolls ? ", scrolls" : "");
			Face.Invalidate ();
			Set.Save ();
		}
		public bool Scrolls;

		// the wheel: the Pi's when the pointer is on its screen (Face sends it), else the view scrolls
		protected override void OnMouseWheel (MouseEventArgs e)
		{
			Point p = Face.PointToClient (PointToScreen (e.Location));
			if (Set.Mouse && Conn != null && Face.L.Screen.Contains (p)) { Face.Wheel (p, Math.Sign (e.Delta)); return; }
			if (Scrolls) scroller.AutoScrollPosition = new Point (-scroller.AutoScrollPosition.X, -scroller.AutoScrollPosition.Y - Math.Sign (e.Delta) * 90);
		}

		void ShowStatus ()
		{
			var c = Conn;
			miDisconnect.Enabled = c != null;
			if (toast != null && DateTime.Now >= toastUntil) toast = null;
			if (c == null) { stLink.Text = toast ?? linkNote ?? "Not connected  (Session > Connect...)"; stSize.Text = ""; stFps.Text = ""; SetBanner (null); return; }
			stLink.Text = toast ?? linkNote ?? ("Connected to " + host + (port != 3390 ? ":" + port : "") + (c.Reconnects > 0 ? "  (" + c.Reconnects + " reconnection" + (c.Reconnects > 1 ? "s" : "") + ")" : ""));
			int w = c.ScreenW, h = c.ScreenH;
			stSize.Text = w + " x " + h;
			stFps.Text = fps + " fps";
			bool ok = w == 640 && h == 480;
			stSize.ForeColor = ok ? SystemColors.ControlText : Color.FromArgb (0xB0, 0x30, 0x20);
			SetBanner (ok || w == 0 ? null : "The Pi's screen is " + w + " x " + h + ", not 640 x 480: shown scaled to fit. On the Pi: width=640 height=480 in SD:/cmdline.txt, then restart (Help menu).");
		}

		void SetBanner (string text)
		{
			if ((text != null) == bannerOn && (text == null || banner.Text == text)) return;
			bannerOn = text != null;
			banner.Text = text ?? "";
			banner.Visible = text != null;
			Relayout (false);
		}

		// ---- the session ----
		public void AskConnect ()
		{
			using (var d = new ConnectBox (Set))
			{
				if (d.ShowDialog (this) != DialogResult.OK) return;
				Connect (d.Host, d.Port);
			}
		}

		public void Connect (string h, int p)
		{
			Disconnect (null);
			host = h; port = p;
			linkNote = "Connecting to " + h + "...";
			Face.Idle = linkNote; Face.Invalidate ();
			ShowStatus ();
			var c = new Connection ();
			c.RoundDone += session => Post (() => Round (c, session));
			c.LinkChanged += note => Post (() => { if (Conn == c) { linkNote = note; ShowStatus (); } });
			c.Closed += why => Post (() => { if (Conn == c) Disconnect (why); });
			new Thread (() =>
			{
				try { c.Open (h, p, false, true, true); }
				catch (Exception e)
				{
					Post (() => { linkNote = null; Face.Idle = "Not connected"; Face.Invalidate (); ShowStatus (); if (!Quiet) MessageBox.Show (this, "No connection to " + h + ":" + p + "\n\n" + e.Message + "\n\nIs rdpd running on the Pi (port 3390)?", Text); else stLink.Text = "No connection: " + e.Message; });
					return;
				}
				Post (() =>
				{
					Conn = c; linkNote = null;
					Set.Remember (h, p);
					Face.Idle = "Waiting for the screen...";
					ShowStatus ();
				});
			}) { IsBackground = true, Name = "connect" }.Start ();
		}

		public bool Quiet;			// (the tests: no message box)

		void Post (Action a) { try { if (IsHandleCreated && !IsDisposed) BeginInvoke (a); } catch { } }

		void Round (Connection c, int session)
		{
			if (Conn != c) return;
			lock (c.Lock) Compositor.Screen (c, ref remote, ref scr);
			Face.Remote = remote;
			rounds++;
			Face.Invalidate (Rectangle.Inflate (Face.L.Screen, 1, 1));
			c.Ready (session);			// (shown: the next round)
			if (fps == 0 && rounds == 1) ShowStatus ();
		}

		public void Disconnect (string why)
		{
			var c = Conn;
			Conn = null;
			if (c != null) c.Close ();
			Dev.ReleaseAll (true);
			pcDown.Clear ();
			Face.Remote = null; Face.Idle = why ?? "Not connected";
			linkNote = why;
			Face.Invalidate ();
			ShowStatus ();
		}

		protected override void OnFormClosed (FormClosedEventArgs e)
		{
			var c = Conn; Conn = null;
			if (c != null) { Dev.ReleaseAll (); Thread.Sleep (60); c.Close (); }
			Set.Save ();
			base.OnFormClosed (e);
		}

		// ---- the screenshots ----
		public static string ShotFolder { get { return Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.MyPictures), "Onyx"); } }

		// the device drawn into a picture (also the tests')
		public Bitmap DeviceBitmap ()
		{
			var b = new Bitmap (Face.L.Client.Width, Face.L.Client.Height, PixelFormat.Format32bppRgb);
			using (var g = Graphics.FromImage (b)) Face.Draw (g);
			return b;
		}

		void Shot (bool device)
		{
			if (!device && remote == null) { stLink.Text = "No screen to save: not connected"; return; }
			try
			{
				Directory.CreateDirectory (ShotFolder);
				string stem = "PocketTerm-" + DateTime.Now.ToString ("yyyyMMdd-HHmmss") + (device ? "-device" : ""), p = Path.Combine (ShotFolder, stem + ".png");
				for (int i = 2; File.Exists (p); i++) p = Path.Combine (ShotFolder, stem + "-" + i + ".png");
				if (device) using (var b = DeviceBitmap ()) b.Save (p, ImageFormat.Png);
				else remote.Save (p, ImageFormat.Png);
				linkNote = null;
				stLink.Text = "Saved " + p;
			}
			catch (Exception e) { MessageBox.Show (this, "The screenshot was not saved:\n\n" + e.Message, Text); }
		}

		// ---- the PC's keyboard ----
		// A key down: true when it was used (a pad button's key, a special key).
		public bool PcKeyDown (Keys k)
		{
			string button;
			if (Dev.Map.Pc.TryGetValue (k, out button))
			{
				if (pcDown.Add (k)) Dev.Press (button); else Dev.Repeat (button);
				return true;
			}
			var c = Conn;
			uint sym = KeyMap.Special (k);
			if (KeyMap.IsModifier (k)) { if (c != null && pcDown.Add (k)) c.Key (true, false, sym); return false; }
			if (sym != 0)
			{
				if (c != null) c.Key (true, false, sym);
				FlashFor (sym);
				return true;
			}
			uint hk = KeyMap.Held (k);
			if (hk != 0 && c != null && pcDown.Add (k)) c.Key (true, true, hk);	// (its text comes as a character)
			return false;
		}
		public void PcKeyUp (Keys k)
		{
			string button;
			if (Dev.Map.Pc.TryGetValue (k, out button)) { if (pcDown.Remove (k)) Dev.Release (button); return; }
			pcDown.Remove (k);
			var c = Conn;
			if (c == null) return;
			uint s = KeyMap.Special (k);
			if (s != 0) c.Key (false, false, s);
			else { uint hk = KeyMap.Held (k); if (hk != 0) c.Key (false, true, hk); }
		}
		public void PcChar (char ch)
		{
			var c = Conn;
			if (c == null || ch == '\r' || ch == '\b' || ch == '\t' || ch == (char) 27) return;	// (sent as keys)
			if (ch < 256) c.Char (ch);
		}

		// the control of the face a PC key stands for, lit a moment
		void FlashFor (uint sym)
		{
			foreach (string b in Mapping.PadButtons)
			{
				Chord c;
				if (Dev.Map.Buttons.TryGetValue (b, out c) && c.Mods.Count == 0 && c.Key == sym) { Face.Flash (b); return; }
			}
			foreach (var kd in Dev.Keys.Values) if (kd.Mod == Mod.None && kd.Sym == sym) { Face.Flash (kd.Id); return; }
		}

		protected override bool ProcessCmdKey (ref Message msg, Keys keyData)
		{
			const int WM_KEYDOWN = 0x100, WM_SYSKEYDOWN = 0x104;
			if (keyData == (Keys.Control | Keys.Shift | Keys.S) || keyData == (Keys.Control | Keys.Shift | Keys.D) || keyData == (Keys.Alt | Keys.F4))
				return base.ProcessCmdKey (ref msg, keyData);
			if (msg.Msg == WM_KEYDOWN || msg.Msg == WM_SYSKEYDOWN)
			{
				Keys k = keyData & Keys.KeyCode;
				if (PcKeyDown (k)) return true;
				if (KeyMap.IsModifier (k)) return true;		// (Alt alone must not open this window's menu)
			}
			return false;						// (no menu shortcut is taken from the session)
		}
		protected override void OnKeyUp (KeyEventArgs e) { PcKeyUp (e.KeyCode); e.Handled = true; }
		protected override void OnKeyPress (KeyPressEventArgs e) { PcChar (e.KeyChar); e.Handled = true; }
		protected override void OnDeactivate (EventArgs e)
		{
			foreach (Keys k in new List<Keys> (pcDown)) PcKeyUp (k);	// (a key released in another window)
			pcDown.Clear ();
			base.OnDeactivate (e);
		}
		protected override void WndProc (ref Message m)
		{
			const int WM_SYSCOMMAND = 0x112, SC_KEYMENU = 0xF100, WM_SYSKEYUP = 0x105, WM_SYSCHAR = 0x106;
			if (m.Msg == WM_SYSCOMMAND && ((int) m.WParam & 0xFFF0) == SC_KEYMENU) return;		// (Alt, F10: the Pi's)
			if (m.Msg == WM_SYSKEYUP) { PcKeyUp ((Keys) (int) m.WParam & Keys.KeyCode); return; }
			if (m.Msg == WM_SYSCHAR) { PcChar ((char) (int) m.WParam); return; }			// (Alt + a letter)
			base.WndProc (ref m);
		}
	}

	// What is kept from one run to the next: %APPDATA%\PocketTermSim.txt.
	class Settings
	{
		public string Host = ""; public int Port = 3390;
		public List<string> Recent = new List<string> ();
		public int Scale, Layout; public bool Smooth, Mouse = true, Top;
		public static bool ReadOnly;			// (the tests: nothing written)
		static string File_ { get { return Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.ApplicationData), "PocketTermSim.txt"); } }

		public static Settings Load ()
		{
			var s = new Settings ();
			try
			{
				foreach (string line in File.ReadAllLines (File_))
				{
					int eq = line.IndexOf ('=');
					if (eq < 0) continue;
					string k = line.Substring (0, eq), v = line.Substring (eq + 1);
					int n; int.TryParse (v, out n);
					switch (k)
					{
					case "host": s.Host = v; break;
					case "port": if (n > 0 && n < 65536) s.Port = n; break;
					case "recent": if (v.Length > 0) s.Recent.AddRange (v.Split ('|')); break;
					case "scale": s.Scale = Math.Max (0, Math.Min (4, n)); break;
					case "layout": s.Layout = Math.Max (0, Math.Min (1, n)); break;
					case "smooth": s.Smooth = n != 0; break;
					case "mouse": s.Mouse = n != 0; break;
					case "top": s.Top = n != 0; break;
					}
				}
			}
			catch { }
			return s;
		}

		public void Remember (string host, int port)
		{
			Host = host; Port = port;
			string e = port == 3390 ? host : host + ":" + port;
			Recent.RemoveAll (x => x.Equals (e, StringComparison.OrdinalIgnoreCase));
			Recent.Insert (0, e);
			if (Recent.Count > 8) Recent.RemoveRange (8, Recent.Count - 8);
			Save ();
		}

		public void Save ()
		{
			if (ReadOnly) return;
			try
			{
				File.WriteAllLines (File_, new[] { "host=" + Host, "port=" + Port, "recent=" + string.Join ("|", Recent.ToArray ()), "scale=" + Scale, "layout=" + Layout,
					"smooth=" + (Smooth ? 1 : 0), "mouse=" + (Mouse ? 1 : 0), "top=" + (Top ? 1 : 0) });
			}
			catch { }
		}
	}

	// The connection dialog: the Pi's address (the last ones in its list), the port.
	class ConnectBox : Form
	{
		readonly ComboBox host = new ComboBox ();
		readonly TextBox port = new TextBox ();
		public string Host; public int Port;

		public ConnectBox (Settings set)
		{
			Text = "Connect to Onyx"; FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = MinimizeBox = false; ShowInTaskbar = false;
			StartPosition = FormStartPosition.CenterParent; ClientSize = new Size (380, 150); Font = new Font ("Segoe UI", 9f);
			Controls.Add (new Label { Text = "The Pi's address (rdpd):", Location = new Point (14, 14), AutoSize = true });
			host.Location = new Point (16, 36); host.Width = 250; host.Text = set.Host;
			foreach (string r in set.Recent) host.Items.Add (r);
			Controls.Add (new Label { Text = "Port:", Location = new Point (278, 14), AutoSize = true });
			port.Location = new Point (280, 36); port.Width = 84; port.Text = set.Port.ToString ();
			Controls.Add (new Label { Text = "No password, no encryption: a trusted network only.", Location = new Point (14, 70), AutoSize = true, ForeColor = SystemColors.GrayText });
			var ok = new Button { Text = "Connect", Location = new Point (194, 106), Size = new Size (82, 28), DialogResult = DialogResult.None };
			var cancel = new Button { Text = "Cancel", Location = new Point (282, 106), Size = new Size (82, 28), DialogResult = DialogResult.Cancel };
			ok.Click += (s, e) =>
			{
				string h = host.Text.Trim ();
				int p;
				int colon = h.LastIndexOf (':');
				if (colon > 0 && int.TryParse (h.Substring (colon + 1), out p)) { h = h.Substring (0, colon); port.Text = p.ToString (); }
				if (h.Length == 0) { host.Focus (); return; }
				if (!int.TryParse (port.Text, out p) || p <= 0 || p > 65535) { port.Focus (); return; }
				Host = h; Port = p; DialogResult = DialogResult.OK;
			};
			Controls.AddRange (new Control[] { host, port, ok, cancel });
			AcceptButton = ok; CancelButton = cancel;
		}
	}
}
