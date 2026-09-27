// MainForm.cs -- the connection window: the Pi's address, the options (16-bit pixels, the
// desktop shown or not), Connect / Disconnect, the rate of updates. Once connected, every Onyx
// window opens as a window of its own (RemoteWindow).
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace OnyxRemote
{
	class MainForm : Form
	{
		readonly TextBox host = new TextBox ();
		readonly NumericUpDown port = new NumericUpDown ();
		readonly CheckBox bits16 = new CheckBox (), desktop = new CheckBox (), frames = new CheckBox ();
		readonly Button go = new Button ();
		readonly Label status = new Label ();
		Connection conn;
		readonly Dictionary<uint, RemoteWindow> wins = new Dictionary<uint, RemoteWindow> ();
		readonly Dictionary<uint, Overlay> overlayCache = new Dictionary<uint, Overlay> ();
		int rounds; DateTime since = DateTime.Now;
		static readonly string SettingsPath = Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.ApplicationData), "OnyxRemote.txt");

		public MainForm ()
		{
			Text = "Onyx Remote";
			FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = false;
			ClientSize = new Size (360, 190);
			Font = new Font ("Segoe UI", 9f);
			var l1 = new Label { Text = "Onyx (IP or name):", Location = new Point (12, 15), AutoSize = true };
			host.SetBounds (130, 12, 140, 23);
			port.SetBounds (280, 12, 68, 23); port.Minimum = 1; port.Maximum = 65535; port.Value = 3390;
			bits16.Text = "16-bit colours (faster)"; bits16.SetBounds (12, 45, 200, 22);
			desktop.Text = "Show the Onyx desktop"; desktop.SetBounds (12, 68, 200, 22);
			frames.Text = "Onyx window frames"; frames.SetBounds (12, 91, 200, 22);
			go.Text = "Connect"; go.SetBounds (268, 45, 80, 28);
			status.SetBounds (12, 120, 336, 60);
			Controls.AddRange (new Control[] { l1, host, port, bits16, desktop, frames, go, status });
			AcceptButton = go;
			go.Click += (s, e) => { if (conn == null) Connect (); else Disconnect ("disconnected"); };
			desktop.CheckedChanged += (s, e) => { if (conn != null) { conn.Desktop (desktop.Checked); Sync (); } };
			try
			{
				foreach (var line in File.ReadAllLines (SettingsPath))
				{
					int eq = line.IndexOf ('=');
					if (eq < 0) continue;
					string k = line.Substring (0, eq), v = line.Substring (eq + 1);
					if (k == "host") host.Text = v;
					else if (k == "port" && int.TryParse (v, out int p) && p > 0 && p < 65536) port.Value = p;
					else if (k == "bits16") bits16.Checked = v == "1";
					else if (k == "desktop") desktop.Checked = v == "1";
					else if (k == "frames") frames.Checked = v == "1";
				}
			}
			catch { }
			var tick = new Timer { Interval = 1000 };
			tick.Tick += (s, e) =>
			{
				if (conn == null) return;
				double sec = (DateTime.Now - since).TotalSeconds;
				if (conn.KernelAbi < 56)
					status.Text = string.Format ("The Onyx kernel on the Pi is too old (kapi v{0}, rdpd needs v56): copy the new kernel8-rpi4.img to the SD card.", conn.KernelAbi);
				else
				{
					int listed; lock (conn.Lock) listed = conn.Windows.Count;
					status.Text = string.Format ("Connected to {0} ({1} x {2}, kapi v{5}): {6} windows from the Pi, {3} shown, {4:0.0} updates / s",
						host.Text, conn.ScreenW, conn.ScreenH, wins.Count, rounds / Math.Max (sec, 0.001), conn.KernelAbi, listed);
				}
				rounds = 0; since = DateTime.Now;
			};
			tick.Start ();
		}

		void Connect ()
		{
			try
			{
				File.WriteAllLines (SettingsPath, new[] { "host=" + host.Text, "port=" + port.Value,
					"bits16=" + (bits16.Checked ? 1 : 0), "desktop=" + (desktop.Checked ? 1 : 0), "frames=" + (frames.Checked ? 1 : 0) });
			}
			catch { }
			var c = new Connection ();
			status.Text = "Connecting...";
			Refresh ();
			try { c.Open (host.Text.Trim (), (int) port.Value, bits16.Checked, desktop.Checked, frames.Checked); }
			catch (Exception e) { status.Text = "Cannot connect: " + e.Message; return; }
			conn = c;
			c.RoundDone += () => BeginInvoke ((Action) (() => { if (conn != c) return; Sync (); rounds++; c.Ready (); }));
			c.Closed += why => BeginInvoke ((Action) (() => { if (conn == c) Disconnect (why); }));
			go.Text = "Disconnect";
			host.Enabled = port.Enabled = bits16.Enabled = frames.Enabled = false;
		}

		void Disconnect (string why)
		{
			conn?.Close (); conn = null;
			foreach (var w in wins.Values) { w.GoneOnPi = true; w.Close (); }
			wins.Clear ();
			go.Text = "Connect";
			host.Enabled = port.Enabled = bits16.Enabled = frames.Enabled = true;
			status.Text = why;
		}

		// The model -> the PC windows (the UI thread).
		void Sync ()
		{
			if (conn == null) return;
			Point origin = Screen.PrimaryScreen.Bounds.Location;	// (the Pi's screen = the PC's, from its top left)
			lock (conn.Lock)
			{
				var keep = new HashSet<uint> ();
				// with the desktop shown, the windows without a frame (the menu bar, the bubbles)
				// are drawn inside its window, not over the PC's screen
				bool deskShown = desktop.Checked && conn.Windows.ContainsKey (WinModel.DESKTOP_ID);
				RemoteWindow deskWin = null;
				var overlays = new List<Overlay> ();
				bool overlaysChanged = false;
				foreach (uint id in conn.ZOrder)			// (bottom to top: new ones open in that order)
				{
					if (!conn.Windows.TryGetValue (id, out WinModel m)) continue;
					bool isDesk = id == WinModel.DESKTOP_ID;
					if (deskShown && !isDesk && (m.Flags & WinModel.BORDERLESS) != 0 && (m.Flags & WinModel.BACKMOST) == 0
					    && (m.State & WinModel.FULLSCREEN) == 0 && m.W > 0 && m.H > 0)
					{
						if (!overlayCache.TryGetValue (id, out Overlay o)) { o = new Overlay { Id = id }; overlayCache[id] = o; m.Dirty = true; }
						if (m.Dirty || o.W != m.W || o.H != m.H || o.X != m.X || o.Y != m.Y) overlaysChanged = true;
						if (m.Dirty || o.W != m.W || o.H != m.H) { RemoteWindow.FillOverlay (o, m); m.Dirty = false; }
						o.X = m.X; o.Y = m.Y; o.Key = (m.Flags & WinModel.TRANSPARENT) != 0; o.Alpha = m.Alpha / 255f;
						if (m.Alpha > 0) overlays.Add (o);
						continue;
					}
					if ((m.Flags & WinModel.BACKMOST) != 0 && !isDesk) continue;	// (drawn in the desktop's view)
					if (isDesk && !desktop.Checked) continue;
					if (m.Alpha == 0 || m.W <= 0 || m.H <= 0) continue;
					keep.Add (id);
					if (!wins.TryGetValue (id, out RemoteWindow w))
					{
						w = new RemoteWindow (conn, id, frames.Checked);
						w.DesktopClosed += () => desktop.Checked = false;
						wins[id] = w;
						w.Apply (m, origin);
						w.Show ();
						w.Apply (m, origin);				// (again, its handle made: the menu bar docks)
					}
					else w.Apply (m, origin);
					if (isDesk) deskWin = w;
				}
				if (deskWin != null)
				{
					bool same = deskWin.Overlays.Count == overlays.Count;
					for (int i = 0; same && i < overlays.Count; i++) same = deskWin.Overlays[i] == overlays[i];
					if (!same || overlaysChanged) { deskWin.Overlays = overlays; deskWin.Invalidate (); }
				}
				foreach (var id in new List<uint> (overlayCache.Keys))
					if (!deskShown || !conn.Windows.ContainsKey (id)) { overlayCache[id].Bmp?.Dispose (); overlayCache.Remove (id); }
				foreach (var id in new List<uint> (wins.Keys))
					if (!keep.Contains (id)) { wins[id].GoneOnPi = true; wins[id].Close (); wins.Remove (id); }
			}
		}

		protected override void OnFormClosed (FormClosedEventArgs e) { conn?.Close (); base.OnFormClosed (e); }
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
