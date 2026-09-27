// GameForm.cs -- a game being played: its window (the picture scaled with its proportions, black
// bars around), the keys, and its own thread running the machine -- paced by the sound queue
// (when the game makes sound) or by the clock, a picture skipped now and then to catch up.
using System;
using System.Diagnostics;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace NintendoEMU
{
	// the picture: drawn with StretchDIBits (fast, no copy into a Bitmap)
	class Screen : Control
	{
		[DllImport ("gdi32.dll")] static extern int StretchDIBits (IntPtr hdc, int x, int y, int w, int h, int sx, int sy, int sw, int sh,
			IntPtr bits, ref BITMAPINFOHEADER bmi, uint usage, uint rop);
		[DllImport ("gdi32.dll")] static extern int SetStretchBltMode (IntPtr hdc, int mode);
		[StructLayout (LayoutKind.Sequential)] struct BITMAPINFOHEADER
		{
			public int biSize, biWidth, biHeight; public short biPlanes, biBitCount; public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant;
		}

		public IntPtr Pixels; public int PicW, PicH;	// (the game form's buffer)
		public double Aspect = 4.0 / 3.0;
		public bool Smooth;
		public string Overlay;				// the speed, "Paused"...

		public Screen ()
		{
			SetStyle (ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.Opaque | ControlStyles.Selectable, true);
			BackColor = Color.Black;
		}
		protected override bool IsInputKey (Keys k) => true;		// the arrows, Enter... are the game's

		public Rectangle PictureRect ()
		{
			int w = ClientSize.Width, h = ClientSize.Height;
			int pw = w, ph = (int) (w / Aspect);
			if (ph > h) { ph = h; pw = (int) (h * Aspect); }
			return new Rectangle ((w - pw) / 2, (h - ph) / 2, pw, ph);
		}

		protected override void OnPaint (PaintEventArgs e)
		{
			var g = e.Graphics;
			var r = PictureRect ();
			// the bars
			if (r.Left > 0) { g.FillRectangle (Brushes.Black, 0, 0, r.Left, ClientSize.Height); g.FillRectangle (Brushes.Black, r.Right, 0, ClientSize.Width - r.Right, ClientSize.Height); }
			if (r.Top > 0) { g.FillRectangle (Brushes.Black, 0, 0, ClientSize.Width, r.Top); g.FillRectangle (Brushes.Black, 0, r.Bottom, ClientSize.Width, ClientSize.Height - r.Bottom); }
			if (Pixels != IntPtr.Zero && PicW > 0 && PicH > 0)
			{
				IntPtr hdc = g.GetHdc ();
				SetStretchBltMode (hdc, Smooth ? 4 : 3);		// HALFTONE : COLORONCOLOR
				var bi = new BITMAPINFOHEADER { biSize = 40, biWidth = PicW, biHeight = -PicH, biPlanes = 1, biBitCount = 32 };
				StretchDIBits (hdc, r.X, r.Y, r.Width, r.Height, 0, 0, PicW, PicH, Pixels, ref bi, 0, 0x00CC0020);
				g.ReleaseHdc (hdc);
			}
			else g.FillRectangle (Brushes.Black, r);
			if (!string.IsNullOrEmpty (Overlay))
			{
				using (var f = new Font ("Consolas", 11f))
				{
					var sz = g.MeasureString (Overlay, f);
					g.FillRectangle (Brushes.Black, r.X, r.Bottom - sz.Height - 4, sz.Width + 8, sz.Height + 4);
					g.DrawString (Overlay, f, Brushes.Yellow, r.X + 4, r.Bottom - sz.Height - 2);
				}
			}
		}
	}

	class GameForm : Form
	{
		[DllImport ("winmm.dll")] static extern uint timeBeginPeriod (uint ms);
		[DllImport ("winmm.dll")] static extern uint timeEndPeriod (uint ms);

		const int RATE = 44100;
		public static GameForm Current;			// (one game at a time: one sound output)

		readonly Game game;
		IntPtr h;
		readonly Screen screen = new Screen ();
		readonly MenuStrip menu = new MenuStrip ();
		Thread thread;
		volatile bool running, paused, soundOn, stats, uiPending;
		volatile int scale3d;
		readonly byte[] keys = new byte[256];
		bool keysChanged;
		readonly int[] pixels;
		GCHandle pin;
		bool full; FormWindowState oldState; Rectangle oldBounds;
		string speedText = "";
		int baseH;
		ToolStripMenuItem miPause, miSound, miStats, miSmooth;

		public static void Play (Game g, Form owner)
		{
			if (Current != null)
			{
				if (MessageBox.Show (owner, "Stop " + Current.game.Name + " and play " + g.Name + "?", "NintendoEMU", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
				Current.Close ();
			}
			if (!g.Exists) { MessageBox.Show (owner, "The file is not there any more:\n" + g.Path, "NintendoEMU", MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }
			IntPtr h = Native.Open (g.Path, RATE, out string err);
			if (h == IntPtr.Zero) { MessageBox.Show (owner, err.Length > 0 ? err : "Cannot start this game.", "NintendoEMU", MessageBoxButtons.OK, MessageBoxIcon.Warning); return; }
			var f = new GameForm (g, h);
			Current = f;
			f.Show ();
			if (Settings.GetBool ("FullScreen", false)) f.SetFull (true);
		}

		GameForm (Game g, IntPtr handle)
		{
			game = g; h = handle;
			pixels = new int[2560 * 2304];			// the biggest picture: a GameCube frame at 4 x
			pin = GCHandle.Alloc (pixels, GCHandleType.Pinned);
			soundOn = Settings.GetBool ("Sound", true);
			scale3d = Settings.GetInt ("Scale3D", 2);
			Native.ne_set_scale (h, scale3d);

			string title = Native.Title (h);
			Text = g.Name + " - " + g.SystemName + " - NintendoEMU";
			BackColor = Color.Black;
			KeyPreview = true;
			screen.Dock = DockStyle.Fill;
			screen.Aspect = Native.ne_aspect1000 (h) / 1000.0;
			screen.Smooth = Settings.GetBool ("Smooth", false);
			screen.Pixels = pin.AddrOfPinnedObject ();
			Controls.Add (screen);
			BuildMenu ();
			Controls.Add (menu);
			MainMenuStrip = menu;

			switch (g.System)
			{
			case Sys.GB: case Sys.GBC: baseH = 144; break;
			case Sys.GBA: baseH = 160; break;
			case Sys.SNES: baseH = 224; break;
			default: baseH = 240; break;
			}
			SetZoom (Settings.GetInt ("Zoom", 3));
			StartPosition = FormStartPosition.CenterScreen;

			screen.KeyDown += (s, e) => Key (e, true);
			screen.KeyUp += (s, e) => Key (e, false);
			Deactivate += (s, e) => { lock (keys) { Array.Clear (keys, 0, 256); keysChanged = true; } };
			Shown += (s, e) => { screen.Focus (); Start (); };
			try { Icon = Icon.ExtractAssociatedIcon (Application.ExecutablePath); } catch { }
		}

		void BuildMenu ()
		{
			var game = new ToolStripMenuItem ("&Game");
			miPause = new ToolStripMenuItem ("&Pause", null, (s, e) => TogglePause ()) { ShortcutKeyDisplayString = "P" };
			game.DropDownItems.Add (miPause);
			game.DropDownItems.Add (new ToolStripMenuItem ("&Reset", null, (s, e) => DoReset ()) { ShortcutKeyDisplayString = "Ctrl+R" });
			game.DropDownItems.Add (new ToolStripSeparator ());
			game.DropDownItems.Add (new ToolStripMenuItem ("&Close", null, (s, e) => Close ()) { ShortcutKeyDisplayString = "Ctrl+W" });
			var view = new ToolStripMenuItem ("&View");
			view.DropDownItems.Add (new ToolStripMenuItem ("&Full Screen", null, (s, e) => SetFull (!full)) { ShortcutKeyDisplayString = "F11" });
			for (int z = 1; z <= 5; z++) { int zz = z; view.DropDownItems.Add (new ToolStripMenuItem ("Size " + z + "x", null, (s, e) => { SetZoom (zz); Settings.Set ("Zoom", zz); })); }
			view.DropDownItems.Add (new ToolStripSeparator ());
			miSmooth = new ToolStripMenuItem ("Smooth Picture", null, (s, e) => { screen.Smooth = !screen.Smooth; miSmooth.Checked = screen.Smooth; Settings.Set ("Smooth", screen.Smooth); screen.Invalidate (); }) { Checked = screen.Smooth };
			view.DropDownItems.Add (miSmooth);
			if (this.game.System == Sys.N64 || this.game.System == Sys.GC)
			{
				var res = new ToolStripMenuItem ("3D Resolution");
				for (int s3 = 1; s3 <= 4; s3++)
				{
					int k = s3;
					var it = new ToolStripMenuItem (k + "x the console's", null, null) { Checked = k == scale3d };
					it.Click += (s, e) => { scale3d = k; Settings.Set ("Scale3D", k); foreach (ToolStripMenuItem o in res.DropDownItems) o.Checked = o == it; };
					res.DropDownItems.Add (it);
				}
				view.DropDownItems.Add (res);
			}
			miStats = new ToolStripMenuItem ("Show &Speed", null, (s, e) => { stats = !stats; miStats.Checked = stats; if (!stats) screen.Overlay = paused ? "Paused" : null; screen.Invalidate (); }) { ShortcutKeyDisplayString = "F12" };
			view.DropDownItems.Add (miStats);
			var sound = new ToolStripMenuItem ("&Sound");
			miSound = new ToolStripMenuItem ("Sound On", null, (s, e) => { soundOn = !soundOn; miSound.Checked = soundOn; Settings.Set ("Sound", soundOn); }) { Checked = soundOn };
			sound.DropDownItems.Add (miSound);
			var help = new ToolStripMenuItem ("&Help");
			help.DropDownItems.Add (new ToolStripMenuItem ("&Controls...", null, (s, e) => MessageBox.Show (this, Controls_ (this.game.System), "Controls - " + this.game.SystemName)));
			menu.Items.AddRange (new ToolStripItem[] { game, view, sound, help });
		}

		public static string Controls_ (Sys s)
		{
			switch (s)
			{
			case Sys.GB: case Sys.GBC: case Sys.NES:
				return "Arrows: the D-pad\nX: A    Z: B\nEnter: Start    Backspace: Select\n\nGamepad: the D-pad (or the left stick), B / Y = A, A / X = B, Start, Back = Select";
			case Sys.GBA:
				return "Arrows: the D-pad\nX: A    Z: B    A: L    S: R\nEnter: Start    Backspace: Select\n\nGamepad: the D-pad (or the left stick), B / Y = A, A / X = B, the shoulders = L / R, Start, Back = Select";
			case Sys.SNES:
				return "Arrows: the D-pad\nX: A    Z: B    S: X    A: Y\nQ: L    W: R\nEnter: Start    Backspace: Select\n\nGamepad: the buttons by their place (right = A, bottom = B, top = X, left = Y), the shoulders = L / R, Start, Back = Select";
			case Sys.N64:
				return "Arrows: the stick\nX: A    C: B    Z: Z\nEnter: Start    Q: L    W: R\nI J K L: the C buttons    T F G H: the D-pad\n\nGamepad: the left stick, A = A, X = B, the triggers = Z, the shoulders = L / R, Start, the right stick = the C buttons, the D-pad";
			case Sys.GC:
				return "Arrows: the stick\nX: A    C: B    S: X    A: Y    Z: Z\nEnter: Start    Q: L    W: R\nI J K L: the C stick    T F G H: the D-pad\n\nGamepad: the sticks, A = A, X = B, B = X, Y = Y, the shoulders = Z, the triggers = L / R, Start, the D-pad";
			}
			return "";
		}

		void SetZoom (int z)
		{
			if (full) SetFull (false);
			if (WindowState != FormWindowState.Normal) WindowState = FormWindowState.Normal;
			int hh = baseH * z; int ww = (int) (hh * screen.Aspect);
			var wa = System.Windows.Forms.Screen.FromControl (this).WorkingArea;
			if (hh + menu.Height + 60 > wa.Height) { hh = wa.Height - menu.Height - 60; ww = (int) (hh * screen.Aspect); }
			ClientSize = new Size (ww, hh + menu.Height);
		}

		public void SetFull (bool on)
		{
			if (on == full) return;
			full = on;
			if (on)
			{
				oldState = WindowState; oldBounds = Bounds;
				menu.Visible = false;
				FormBorderStyle = FormBorderStyle.None;
				WindowState = FormWindowState.Normal;
				Bounds = System.Windows.Forms.Screen.FromControl (this).Bounds;
				Cursor.Hide ();
			}
			else
			{
				FormBorderStyle = FormBorderStyle.Sizable;
				Bounds = oldBounds; WindowState = oldState;
				menu.Visible = true;
				Cursor.Show ();
			}
			screen.Focus ();
		}

		void TogglePause ()
		{
			paused = !paused; miPause.Checked = paused;
			screen.Overlay = paused ? "Paused" : stats ? speedText : null;
			screen.Invalidate ();
		}
		void DoReset ()
		{
			if (game.System != Sys.GC) { lock (this) Native.ne_reset (h); return; }
			// the GameCube: the game opened again
			Stop ();
			Native.ne_close (h);
			h = Native.Open (game.Path, RATE, out string err);
			if (h == IntPtr.Zero) { MessageBox.Show (this, err, "NintendoEMU"); Close (); return; }
			Native.ne_set_scale (h, scale3d);
			Start ();
		}

		void Key (KeyEventArgs e, bool down)
		{
			if (down)
			{
				switch (e.KeyCode)
				{
				case Keys.F11: SetFull (!full); e.Handled = true; return;
				case Keys.Escape: if (full) SetFull (false); e.Handled = true; return;
				case Keys.F12: miStats.PerformClick (); e.Handled = true; return;
				case Keys.P: TogglePause (); e.Handled = true; return;
				}
				if (e.Alt && e.KeyCode == Keys.Enter) { SetFull (!full); e.Handled = true; e.SuppressKeyPress = true; return; }
				if (e.Control && e.KeyCode == Keys.R) { DoReset (); e.Handled = true; return; }
				if (e.Control && e.KeyCode == Keys.W) { Close (); e.Handled = true; return; }
			}
			int k = (int) e.KeyCode & 0xFF;
			lock (keys) { keys[k] = (byte) (down ? 1 : 0); keysChanged = true; }
			e.Handled = true; e.SuppressKeyPress = true;
		}

		void Start ()
		{
			running = true;
			thread = new Thread (Run) { IsBackground = true, Name = "game", Priority = ThreadPriority.AboveNormal };
			thread.Start ();
		}
		void Stop ()
		{
			running = false;
			thread?.Join (3000);
			thread = null;
		}

		// the game's thread
		void Run ()
		{
			timeBeginPeriod (1);
			bool audioOk = Native.ne_audio_open (RATE) == 1;
			var pcm = new short[8192];
			var kc = new byte[256];
			var sw = Stopwatch.StartNew ();
			double frameMs = 1000000.0 / Native.ne_fps1000 (h), next = 0, statT = 0;
			int skipped = 0, frames = 0, shown = 0; bool hadAudio = false;
			try
			{
				while (running)
				{
					if (paused) { Thread.Sleep (10); next = sw.Elapsed.TotalMilliseconds; continue; }
					lock (keys) { if (keysChanged) { Buffer.BlockCopy (keys, 0, kc, 0, 256); keysChanged = false; } }
					Native.ne_set_keys (h, kc);
					Native.ne_set_scale (h, scale3d);
					double now = sw.Elapsed.TotalMilliseconds;
					bool draw;
					if (audioOk && soundOn && hadAudio)
					{
						// paced by the sound: ~70 ms queued at most; a starving queue skips pictures
						int q = Native.ne_audio_queued ();
						if (q > RATE * 7 / 100) { Thread.Sleep (1); continue; }
						draw = q > RATE * 2 / 100 || skipped >= 3;
						next = now;
					}
					else
					{
						if (now < next) { if (next - now > 1.5) Thread.Sleep (1); else Thread.Yield (); continue; }
						if (now - next > 250) next = now;		// far behind (a pause, a slow game): start again from now
						draw = now - next < frameMs || skipped >= 3;
						next += frameMs;
					}
					int r;
					lock (this) r = Native.ne_run_frame (h, draw ? 1 : 0);
					if (r == 0)
					{
						string msg = Native.Halted (h) ?? "";
						BeginInvoke ((Action) (() => MessageBox.Show (this, "The game stopped." + (msg.Length > 0 ? "\n" + msg : ""), "NintendoEMU")));
						break;
					}
					frames++;
					skipped = draw ? 0 : skipped + 1;
					int k, got = 0;
					while ((k = Native.ne_audio (h, pcm, 4096)) > 0)
					{
						got += k;
						if (audioOk && soundOn) Native.ne_audio_write (pcm, k);
					}
					hadAudio = got > 0;
					if (draw)
					{
						shown++;
						if (!uiPending) { uiPending = true; try { BeginInvoke ((Action) ShowFrame); } catch { uiPending = false; } }
					}
					if (now - statT >= 1000)
					{
						double s = (now - statT) / 1000.0;
						speedText = string.Format ("{0:0.0} fps ({1:0} shown), {2:0}% speed{3}{4}", frames / s, shown / s,
							frames / s * frameMs / 10.0, Native.ne_is_3d (h) != 0 ? ", 3D" : "",
							audioOk && soundOn && hadAudio ? string.Format (", sound {0} ms", Native.ne_audio_queued () * 1000 / RATE) : "");
						frames = 0; shown = 0; statT = now;
					}
				}
			}
			catch (ObjectDisposedException) { }
			catch (InvalidOperationException) { }
			finally
			{
				Native.ne_audio_close ();
				timeEndPeriod (1);
			}
		}

		// the window's thread: the last picture
		void ShowFrame ()
		{
			uiPending = false;
			if (h == IntPtr.Zero) return;
			uint serial = Native.ne_video (h, screen.Pixels, pixels.Length, out int w, out int hh);
			if (serial == 0) return;
			screen.PicW = w; screen.PicH = hh;
			if (!paused) screen.Overlay = stats ? speedText : null;
			screen.Invalidate ();
		}

		protected override void OnFormClosing (FormClosingEventArgs e)
		{
			Stop ();
			if (h != IntPtr.Zero) { Native.ne_close (h); h = IntPtr.Zero; }	// (writes the save)
			if (full) Cursor.Show ();
			if (pin.IsAllocated) pin.Free ();
			if (Current == this) Current = null;
			base.OnFormClosing (e);
		}
	}
}
