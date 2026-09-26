// Runner.cs -- a BASIC program's window on Windows. The VM runs on a worker thread inside
// obcore.dll (ob_run); it calls back here to open / resize the window, show its picture (a
// 0x00RRGGBB page, drawn scaled into a bitmap) and manage the controls (real WinForms
// controls); keys, the mouse and control events go back to it (ob_key, ob_mouse, ob_event).
// FULLSCREEN does nothing on the PC. A menu (opened with the mouse only: Alt and F10 stay the
// program's keys) stops / restarts the program, fills the screen, sets the zoom.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace OnyxBasic
{
	// The picture: takes every key (arrows included) and scales the page, proportions kept.
	class Screen : Control
	{
		public Screen ()
		{
			SetStyle (ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
				  ControlStyles.Selectable | ControlStyles.ResizeRedraw, true);
			BackColor = Color.Black;
			TabStop = true;
		}
		protected override bool IsInputKey (Keys k) => true;
		protected override void OnMouseDown (MouseEventArgs e) { Focus (); base.OnMouseDown (e); }
	}

	class Runner : Form
	{
		// ---- the program --------------------------------------------------------------------------------
		readonly string path, cwd, errFile, args;
		Thread vm; volatile bool running;
		int result; int errLine; string errMsg = "";
		ApplicationContext ctx;

		public static int Run (string path, string cwd, string errFile, string args)
		{
			var r = new Runner (path, cwd, errFile, args);
			r.ctx = new ApplicationContext ();
			r.Start ();
			Application.Run (r.ctx);
			return r.result;
		}

		Runner (string path, string cwd, string errFile, string args)
		{
			this.path = path; this.cwd = cwd; this.errFile = errFile; this.args = args ?? "";
			Text = Path.GetFileName (path) + " - Onyx BASIC";
			AutoScaleMode = AutoScaleMode.None;
			BackColor = Color.Black;
			KeyPreview = false;
			StartPosition = FormStartPosition.CenterScreen;
			try { Icon = Icon.ExtractAssociatedIcon (Application.ExecutablePath); } catch { }
			screen = new Screen { Dock = DockStyle.Fill };
			Controls.Add (screen);
			Controls.Add (MakeMenu ());
			screen.Paint += OnPaintScreen;
			screen.KeyDown += OnKeyDown; screen.KeyUp += OnKeyUp; screen.KeyPress += OnKeyPress;
			screen.MouseMove += OnMouse; screen.MouseDown += OnMouse; screen.MouseUp += OnMouse;
			FormClosing += OnClosing;
			var h = Handle;					// (the window exists for Invoke before it is shown)
		}

		// ---- the menu ---------------------------------------------------------------------------------------------
		MenuStrip menu;
		MenuStrip MakeMenu ()
		{
			menu = new MenuStrip { Dock = DockStyle.Top };
			var prog = new ToolStripMenuItem ("&Program");
			prog.DropDownItems.Add (new ToolStripMenuItem ("&Stop", null, (s, e) => Native.ob_stop ()) { ShortcutKeyDisplayString = "Ctrl+Break" });
			prog.DropDownItems.Add (new ToolStripMenuItem ("&Restart", null, (s, e) => Restart ()));
			prog.DropDownItems.Add (new ToolStripSeparator ());
			prog.DropDownItems.Add (new ToolStripMenuItem ("&Close", null, (s, e) => Close ()));
			var view = new ToolStripMenuItem ("&View");
			view.DropDownItems.Add (new ToolStripMenuItem ("&Full screen", null, (s, e) => ToggleFullWindow ()) { ShortcutKeyDisplayString = "Alt+Enter" });
			view.DropDownItems.Add (new ToolStripSeparator ());
			for (int z = 1; z <= 4; z++)
			{
				int zz = z;
				view.DropDownItems.Add (new ToolStripMenuItem ("Zoom &" + z + "x", null, (s, e) => SetZoom (zz)));
			}
			menu.Items.Add (prog); menu.Items.Add (view);
			return menu;
		}
		// Alt and F10 would open the menu: they are the program's keys (F10 = INKEY$'s CHR$(0) + "D").
		protected override bool ProcessCmdKey (ref Message m, Keys keyData)
		{
			if (keyData == (Keys.Enter | Keys.Alt)) { ToggleFullWindow (); return true; }
			if ((keyData & Keys.KeyCode) == Keys.F10 && screen.Focused) { OnKeyDown (screen, new KeyEventArgs (keyData)); return true; }
			if ((keyData & Keys.KeyCode) == Keys.Menu) return true;
			return base.ProcessCmdKey (ref m, keyData);
		}
		protected override void WndProc (ref Message m)
		{
			const int WM_SYSKEYUP = 0x105, WM_SYSCOMMAND = 0x112, SC_KEYMENU = 0xF100;
			if (m.Msg == WM_SYSCOMMAND && ((int) m.WParam & 0xFFF0) == SC_KEYMENU && m.LParam == IntPtr.Zero) return;	// (Alt / F10 alone)
			if (m.Msg == WM_SYSKEYUP && (int) m.WParam == 0x79) { OnKeyUp (screen, new KeyEventArgs (Keys.F10)); return; }
			base.WndProc (ref m);
		}
		void SetZoom (int z)
		{
			if (FormBorderStyle == FormBorderStyle.None) ToggleFullWindow ();
			WindowState = FormWindowState.Normal;
			zoom = z;
			if (pw > 0) SizeFor (pw * psx, ph * psy);
		}
		bool restart;
		void Restart ()
		{
			if (running) { restart = true; Native.ob_stop (); }
			else Start ();
		}

		void Start ()
		{
			byte[] src;
			try { src = File.ReadAllBytes (path); }
			catch (Exception e) { MessageBox.Show ("Cannot read " + path + "\n" + e.Message, "Onyx BASIC"); result = 1; ctx.ExitThread (); return; }
			int len = src.Length;
			Array.Resize (ref src, src.Length + 1);			// (a source is also 0-terminated)
			string sd = Settings.SdFolder;
			string dir = cwd ?? Path.GetDirectoryName (path);
			var cb = MakeCallbacks ();
			running = true;
			vm = new Thread (() =>
			{
				var msg = new byte[256];
				int r = Native.ob_run (src, len, sd, dir, Latin1.Z (args), Latin1.Z (Path.GetFileName (path)), ref cb, out int line, msg, msg.Length);
				running = false;
				errLine = line; errMsg = Latin1.FromZ (msg);
				try { BeginInvoke ((Action) (() => Finished (r))); } catch { }
			}, 16 << 20);
			vm.IsBackground = true;
			vm.Start ();
		}

		void Finished (int r)
		{
			result = r;
			if (restart)						// Program > Restart: a fresh screen, run again
			{
				restart = false;
				foreach (var c in ctl.Values) { screen.Controls.Remove (c); c.Dispose (); }
				ctl.Clear (); kindOf.Clear ();
				fixedSize = false; FormBorderStyle = FormBorderStyle.Sizable; MaximizeBox = true;
				lock (picLock) { pw = ph = 0; }
				screen.Invalidate ();
				Start ();
				return;
			}
			if (r != 0)
			{
				string kind = r == 2 ? "Syntax error" : "Error";
				if (errFile != null)
				{
					try { File.WriteAllText (errFile, errLine + "\n" + errMsg); } catch { }
				}
				else MessageBox.Show (Visible ? this : null, kind + " in line " + errLine + ": " + errMsg, Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
			}
			notifyIcon?.Dispose ();
			Hide ();
			ctx.ExitThread ();
		}

		bool closing;
		void OnClosing (object s, FormClosingEventArgs e)
		{
			if (!running) return;
			e.Cancel = true;					// the program ends first (Finished closes)
			Native.ob_stop ();
			if (closing) Environment.Exit (0);			// asked twice: it is stuck
			closing = true;
		}

		// ---- callbacks (on the VM thread) -------------------------------------------------------------------
		readonly List<Delegate> keep = new List<Delegate> ();	// (alive as long as the run)
		IntPtr Fn (Delegate d) { keep.Add (d); return Marshal.GetFunctionPointerForDelegate (d); }
		T Ui<T> (Func<T> f) { try { return (T) Invoke (f); } catch { return default (T); } }
		void Ui (Action f) { try { Invoke (f); } catch { } }

		ObCallbacks MakeCallbacks ()
		{
			return new ObCallbacks
			{
				openWindow = Fn ((OpenWindowFn) ((w, h, t) => Ui (() => OpenWindow (w, h, Latin1.Read (t))))),
				resizeWindow = Fn ((ResizeFn) ((w, h) => Ui (() => SizeFor (w, h)))),
				present = Fn ((PresentFn) Present),
				control = Fn ((ControlFn) ((id, kind, x, y, w, h, t, v) => { string s = Latin1.Read (t); return Ui (() => AddControl (id, kind, x, y, w, h, s, v)); })),
				setText = Fn ((SetTextFn) ((id, t) => { string s = Latin1.Read (t); Ui (() => SetText (id, s)); })),
				getText = Fn ((GetTextFn) ((id, buf, cap) => Latin1.Write (Ui (() => GetText (id)), buf, cap))),
				getValue = Fn ((GetValueFn) (id => Ui (() => GetValue (id)))),
				setValue = Fn ((SetValueFn) ((id, v) => Ui (() => SetValue (id, v)))),
				notify = Fn ((NotifyFn) ((t, m) => { string a = Latin1.Read (t), b = Latin1.Read (m); Ui (() => Notify (a, b)); })),
				msgbox = Fn ((MsgBoxFn) ((t, m, b) => { string a = Latin1.Read (t), c = Latin1.Read (m); return Ui (() => MsgBox (a, c, b)); })),
				clipboard = Fn ((ClipboardFn) ((buf, cap) => Latin1.Write (Ui (() => { try { return Clipboard.GetText (); } catch { return ""; } }), buf, cap))),
				setClipboard = Fn ((SetClipboardFn) (t => { string s = Latin1.Read (t); Ui (() => { try { if (s == "") Clipboard.Clear (); else Clipboard.SetText (s); } catch { } }); })),
				fileDialog = Fn ((FileDialogFn) FileDialog),
			};
		}

		// ---- the window and its picture ------------------------------------------------------------------------
		readonly Screen screen;
		readonly object picLock = new object ();
		int[] pix = new int[0]; int pw, ph, psx = 1, psy = 1; bool picNew; int invalidating;
		Bitmap bmp;
		Rectangle dest;						// where the picture is drawn in the screen
		int zoom = 1;						// the window's first zoom (large displays)
		bool fixedSize;						// controls: no scaling

		int OpenWindow (int w, int h, string title)
		{
			var wa = System.Windows.Forms.Screen.FromControl (this).WorkingArea;
			zoom = Math.Max (1, Math.Min ((wa.Width * 7 / 10) / w, (wa.Height * 7 / 10) / h));
			SizeFor (w, h);
			if (!Visible) { Show (); Activate (); }
			screen.Focus ();
			return 1;
		}
		void SizeFor (int w, int h)
		{
			if (WindowState != FormWindowState.Normal || FormBorderStyle == FormBorderStyle.None) return;
			ClientSize = new Size (w * zoom, h * zoom + menu.Height);
		}
		void Present (IntPtr px, int w, int h, int sx, int sy)
		{
			lock (picLock)
			{
				if (pix.Length != w * h) pix = new int[w * h];
				Marshal.Copy (px, pix, 0, w * h);
				pw = w; ph = h; psx = sx; psy = sy; picNew = true;
			}
			if (Interlocked.Exchange (ref invalidating, 1) == 0)
			{
				try { screen.BeginInvoke ((Action) (() => { invalidating = 0; screen.Invalidate (); })); } catch { invalidating = 0; }
			}
		}
		void OnPaintScreen (object s, PaintEventArgs e)
		{
			var g = e.Graphics;
			lock (picLock)
			{
				if (pw == 0) { g.Clear (Color.Black); return; }
				if (bmp == null || bmp.Width != pw || bmp.Height != ph) { bmp?.Dispose (); bmp = new Bitmap (pw, ph, PixelFormat.Format32bppRgb); picNew = true; }
				if (picNew)
				{
					var d = bmp.LockBits (new Rectangle (0, 0, pw, ph), ImageLockMode.WriteOnly, PixelFormat.Format32bppRgb);
					Marshal.Copy (pix, 0, d.Scan0, pw * ph);
					bmp.UnlockBits (d);
					picNew = false;
				}
				// the displayed size (320-wide modes are shown doubled), fitted into the window
				int dw = pw * psx, dh = ph * psy, cw = screen.ClientSize.Width, ch = screen.ClientSize.Height;
				int ow, oh;
				if (fixedSize) { ow = dw; oh = dh; }
				else
				{
					if ((long) cw * dh <= (long) ch * dw) { ow = cw; oh = (int) ((long) cw * dh / dw); }
					else { oh = ch; ow = (int) ((long) ch * dw / dh); }
					int k = ow / dw;					// a whole-number zoom when it fills nearly as much
					if (k >= 1 && k * dw * 100 >= ow * 85) { ow = k * dw; oh = k * dh; }
				}
				dest = new Rectangle ((cw - ow) / 2, (ch - oh) / 2, ow, oh);
				if (fixedSize) dest.Location = Point.Empty;
				g.Clear (Color.Black);
				g.InterpolationMode = InterpolationMode.NearestNeighbor;
				g.PixelOffsetMode = PixelOffsetMode.Half;
				g.DrawImage (bmp, dest);
			}
		}

		// ---- keys and the mouse --------------------------------------------------------------------------------
		static int SpecialKey (Keys k)
		{
			switch (k)
			{
			case Keys.Up: return K.Up; case Keys.Down: return K.Down; case Keys.Left: return K.Left; case Keys.Right: return K.Right;
			case Keys.Home: return K.Home; case Keys.End: return K.End; case Keys.PageUp: return K.PgUp; case Keys.PageDown: return K.PgDn;
			case Keys.Delete: return K.Del;
			}
			if (k >= Keys.F1 && k <= Keys.F12) return K.F1 + (k - Keys.F1);
			return 0;
		}
		static int HeldKey (Keys k)					// KEYDOWN's codes
		{
			int s = SpecialKey (k);
			if (s != 0 && s < K.F1) return s;
			if (k >= Keys.A && k <= Keys.Z) return 'a' + (k - Keys.A);
			if (k >= Keys.D0 && k <= Keys.D9) return '0' + (k - Keys.D0);
			if (k >= Keys.NumPad0 && k <= Keys.NumPad9) return '0' + (k - Keys.NumPad0);
			switch (k)
			{
			case Keys.Space: return ' '; case Keys.Enter: return K.Enter; case Keys.Escape: return K.Esc;
			case Keys.Tab: return K.Tab; case Keys.Back: return K.Backspace;
			}
			return 0;
		}
		void OnKeyDown (object s, KeyEventArgs e)
		{
			if (e.KeyCode == Keys.Enter && e.Alt) { ToggleFullWindow (); e.Handled = e.SuppressKeyPress = true; return; }
			if (e.KeyCode == Keys.Cancel) { Native.ob_stop (); return; }	// Ctrl+Break
			int h = HeldKey (e.KeyCode);
			if (h != 0) Native.ob_keyheld (h, 1);
			int k = SpecialKey (e.KeyCode);
			if (k != 0) { Native.ob_key (k); e.Handled = true; }
		}
		void OnKeyUp (object s, KeyEventArgs e)
		{
			int h = HeldKey (e.KeyCode);
			if (h != 0) Native.ob_keyheld (h, 0);
		}
		void OnKeyPress (object s, KeyPressEventArgs e)
		{
			int c = e.KeyChar;
			if (c > 0 && c < 256) Native.ob_key (c);		// Latin-1 (3 = Ctrl+C: stop)
			e.Handled = true;
		}
		void OnMouse (object s, MouseEventArgs e)
		{
			int b = (MouseButtons.HasFlag (MouseButtons.Left) ? 1 : 0) | (MouseButtons.HasFlag (MouseButtons.Right) ? 2 : 0) | (MouseButtons.HasFlag (MouseButtons.Middle) ? 4 : 0);
			if (dest.Width <= 0 || dest.Height <= 0 || pw == 0) { Native.ob_mouse (-1, -1, b); return; }
			// the window's pixels -> the program's: / the display scale (the ratio of the sizes)
			int x = (e.X - dest.X) * pw / dest.Width, y = (e.Y - dest.Y) * ph / dest.Height;
			Native.ob_mouse (x, y, b);
		}
		FormWindowState oldState; Rectangle oldBounds;
		void ToggleFullWindow ()
		{
			if (FormBorderStyle != FormBorderStyle.None)
			{
				oldState = WindowState; oldBounds = Bounds;
				FormBorderStyle = FormBorderStyle.None; WindowState = FormWindowState.Normal; menu.Visible = false;
				Bounds = System.Windows.Forms.Screen.FromControl (this).Bounds;
			}
			else
			{
				FormBorderStyle = fixedSize ? FormBorderStyle.FixedSingle : FormBorderStyle.Sizable;
				Bounds = oldBounds; WindowState = oldState; menu.Visible = true;
			}
		}

		// ---- controls ---------------------------------------------------------------------------------------------
		const int CTL_BUTTON = 1, CTL_LABEL = 2, CTL_TEXTBOX = 3, CTL_CHECKBOX = 4, CTL_LISTBOX = 5, CTL_DROPDOWN = 6, CTL_PROGRESS = 7, CTL_SLIDER = 8;
		readonly Dictionary<int, Control> ctl = new Dictionary<int, Control> ();
		readonly Dictionary<int, int> kindOf = new Dictionary<int, int> ();

		int AddControl (int id, int kind, int x, int y, int w, int h, string text, int val)
		{
			Control c;
			switch (kind)
			{
			case CTL_BUTTON: { var b = new Button { Text = text }; b.Click += (s, e) => Native.ob_event (id); c = b; break; }
			case CTL_LABEL: c = new Label { Text = text, ForeColor = Color.Gainsboro, BackColor = Color.Transparent, AutoSize = false }; break;
			case CTL_TEXTBOX:
			{
				var t = new TextBox { Text = text };
				t.KeyDown += (s, e) => { if (e.KeyCode == Keys.Enter) { Native.ob_event (id); e.Handled = e.SuppressKeyPress = true; } };
				c = t; break;
			}
			case CTL_CHECKBOX:
			{
				var k = new CheckBox { Text = text, Checked = val != 0, ForeColor = Color.Gainsboro, BackColor = Color.Transparent };
				k.CheckedChanged += (s, e) => Native.ob_event (id); c = k; break;
			}
			case CTL_LISTBOX:
			{
				var l = new ListBox { IntegralHeight = false };
				foreach (var it in text.Split ('|')) if (it != "") l.Items.Add (it);
				l.SelectedIndexChanged += (s, e) => Native.ob_event (id);
				l.DoubleClick += (s, e) => Native.ob_event (id);
				c = l; break;
			}
			case CTL_DROPDOWN:
			{
				var d = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList };
				foreach (var it in text.Split ('|')) d.Items.Add (it);
				if (d.Items.Count > 0) d.SelectedIndex = 0;
				d.SelectedIndexChanged += (s, e) => Native.ob_event (id);
				c = d; break;
			}
			case CTL_PROGRESS: c = new ProgressBar { Minimum = 0, Maximum = 100, Value = Math.Max (0, Math.Min (100, val)) }; break;
			case CTL_SLIDER:
			{
				var t = new TrackBar { Minimum = 0, Maximum = val > 0 ? val : 100, TickStyle = TickStyle.None, AutoSize = false };
				t.ValueChanged += (s, e) => Native.ob_event (id); c = t; break;
			}
			default: return 0;
			}
			if (!fixedSize)						// the controls have fixed places: no scaling
			{
				fixedSize = true; zoom = 1;
				FormBorderStyle = FormBorderStyle.FixedSingle; MaximizeBox = false;
				if (pw > 0) ClientSize = new Size (pw * psx, ph * psy + menu.Height);
			}
			c.SetBounds (x, y, w, h);
			ctl[id] = c; kindOf[id] = kind;
			screen.Controls.Add (c);
			return 1;
		}
		void SetText (int id, string s)
		{
			if (!ctl.TryGetValue (id, out var c)) return;
			if (c is ListBox l) l.Items.Add (s);				// SETTEXT on a list = add an item
			else c.Text = s;
		}
		string GetText (int id)
		{
			if (!ctl.TryGetValue (id, out var c)) return "";
			if (c is ListBox l) return l.SelectedItem?.ToString () ?? "";
			if (c is ComboBox d) return d.SelectedItem?.ToString () ?? "";
			return c.Text;
		}
		int GetValue (int id)
		{
			if (!ctl.TryGetValue (id, out var c)) return 0;
			switch (c)
			{
			case CheckBox k: return k.Checked ? -1 : 0;
			case ListBox l: return l.SelectedIndex;
			case ComboBox d: return d.SelectedIndex;
			case ProgressBar p: return p.Value;
			case TrackBar t: return t.Value;
			}
			return 0;
		}
		void SetValue (int id, int v)
		{
			if (!ctl.TryGetValue (id, out var c)) return;
			switch (c)
			{
			case CheckBox k: k.Checked = v != 0; break;
			case ListBox l: if (v >= -1 && v < l.Items.Count) l.SelectedIndex = v; break;
			case ComboBox d: if (v >= -1 && v < d.Items.Count) d.SelectedIndex = v; break;
			case ProgressBar p: p.Value = Math.Max (p.Minimum, Math.Min (p.Maximum, v)); break;
			case TrackBar t: t.Value = Math.Max (t.Minimum, Math.Min (t.Maximum, v)); break;
			}
		}

		// ---- the system: notifications, message boxes, file dialogs ---------------------------------------------
		NotifyIcon notifyIcon;
		void Notify (string title, string text)
		{
			if (notifyIcon == null) notifyIcon = new NotifyIcon { Icon = Icon ?? SystemIcons.Information, Visible = true, Text = "Onyx BASIC" };
			notifyIcon.ShowBalloonTip (4000, title, text == "" ? " " : text, ToolTipIcon.Info);
		}
		int MsgBox (string title, string text, int buttons)
		{
			var b = buttons == 1 ? MessageBoxButtons.OKCancel : buttons == 2 ? MessageBoxButtons.YesNo : buttons == 3 ? MessageBoxButtons.YesNoCancel : MessageBoxButtons.OK;
			var r = MessageBox.Show (Visible ? this : null, text, title, b);
			return r == DialogResult.OK || r == DialogResult.Yes ? 1 : r == DialogResult.No && buttons == 3 ? 2 : 0;
		}
		int FileDialog (int save, IntPtr dir, IntPtr name, IntPtr outp, int cap)
		{
			string d = Marshal.PtrToStringUni (dir), n = Marshal.PtrToStringUni (name);
			string r = Ui (() =>
			{
				FileDialog f = save != 0 ? (FileDialog) new SaveFileDialog () : new OpenFileDialog ();
				using (f)
				{
					f.InitialDirectory = d; f.FileName = n ?? "";
					return f.ShowDialog (Visible ? this : null) == DialogResult.OK ? f.FileName : "";
				}
			}) ?? "";
			if (r.Length >= cap) return 0;
			var chars = (r + "\0").ToCharArray ();
			Marshal.Copy (chars, 0, outp, chars.Length);
			return r != "" ? 1 : 0;
		}
	}
}
