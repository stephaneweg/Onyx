// Face.cs -- the PocketTerm35's face, drawn and clicked: the black glass with the Onyx screen under it
// (640 x 480, its rounded corners), the four LEDs, the D-pad, L / R, X / Y / A / B, Select / Start and the
// thumb keyboard. The proportions and the buttons' places are those of the mock-ups' drawing
// (tools/screenshot/mockup_pocketterm.py, device (): a face of 93.5 x 168.5 mm, 9.01 pixels a millimetre
// when the screen is shown 1:1) -- themselves assumed from one product photo.
//
// The window has the device's shape: tall and narrow, 771 x 1390 pixels at 1x. Taller than the monitor, the
// view scrolls (SimForm); "Fit" scales the whole device to the monitor instead, "beside" puts the controls
// at the screen's right.
// MIT licence (Onyx).
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Windows.Forms;

namespace PocketTermSim
{
	enum CtlKind { Key, PadArm, Round, Shoulder, Pill }

	class Ctl
	{
		public string Id;
		public RectangleF R;
		public CtlKind Kind;
		public KeyDef Key;
		public string Label;
	}

	class FaceLayout
	{
		// The face in millimetres (the maker's figures: 93.5 x 168.5, 1 : 1.80; the glass about 80 x 61 with the
		// 71 x 53 picture in it). The simulator's unit is set by the glass: 640 pixels of picture and a slim black
		// border must span the glass' 80 mm, so the whole face follows the screen's scale and keeps the device's
		// proportions (771 x 1390 pixels at 1x). The controls and the keyboard stay inside the glass' width: the
		// keyboard's edges line up with the glass' edges.
		public const float FaceW = 93.5f, FaceH = 168.5f, GlassW = 80f, GlassX = (FaceW - GlassW) / 2, GlassY = 11f, LowerTop = 72f;
		public const float Border = 10f;		// the black border round the picture under the glass, pixels at 1x
		public const float PpiDevice = 640f / 71f * 25.4f;	// the real screen: 229 pixels an inch
		public static float UnitAt (float scale) { return (640 + 2 * Border) * scale / GlassW; }	// pixels a millimetre
		public static Size SizeAt (float scale) { float u = UnitAt (scale); return new Size ((int) Math.Ceiling (FaceW * u) + 2 * Margin, (int) Math.Ceiling (FaceH * u) + 2 * Margin); }
		public Size Client;
		public RectangleF Face, Glass;
		public float FaceR, GlassR, ScreenR;
		public Rectangle Screen;			// where the 640 x 480 picture goes
		public float Scale;				// the screen's scale
		public bool Beside;
		public PointF[] Leds = new PointF[4];
		public float LedD;
		public readonly List<Ctl> Ctls = new List<Ctl> ();
		public PointF PadCentre; public float PadArm;
		public float Unit;				// pixels a millimetre in the controls
		public const int Margin = 12;

		// beside: the controls at the screen's right (a low, wide window) instead of the device's own shape
		public static FaceLayout Make (Device dev, float scale, bool beside) { return beside ? Side (dev, scale) : Portrait (dev, scale); }

		// The device as it is: everything from the one unit.
		static FaceLayout Portrait (Device dev, float s)
		{
			var L = new FaceLayout { Scale = s };
			float u = UnitAt (s), sw = 640 * s, sh = 480 * s, b = Border * s;
			L.Face = new RectangleF (Margin, Margin, FaceW * u, FaceH * u);
			L.FaceR = 9 * u;
			L.Screen = new Rectangle ((int) Math.Round (Margin + GlassX * u + b), (int) Math.Round (Margin + GlassY * u + b), (int) Math.Round (sw), (int) Math.Round (sh));
			L.Glass = new RectangleF (L.Screen.X - b, L.Screen.Y - b, sw + 2 * b, sh + 2 * b);
			L.GlassR = 4.6f * u;
			L.ScreenR = L.GlassR - b;
			L.LedSet (Margin + FaceW * u / 2, Margin + 6.5f * u, u);
			L.Lower (dev, new PointF (Margin, Margin), u, GlassX, GlassW);
			L.Client = SizeAt (s);
			return L;
		}

		// The same controls at the screen's right, a little smaller (the unit from the panel's width).
		static FaceLayout Side (Device dev, float s)
		{
			var L = new FaceLayout { Scale = s, Beside = true };
			float sw = 640 * s, sh = 480 * s, b = Border * s, top = 34, bottom = 22, left = 24;
			float glassW = sw + 2 * b, glassH = sh + 2 * b;
			float u = Math.Max (5.6f, Math.Min (8.25f, (glassH - 16) / (FaceH - LowerTop - 12)));	// the panel about as tall as the glass
			float panelW = FaceW * u, panelH = (FaceH - LowerTop - 6) * u;
			float fh = top + Math.Max (glassH, panelH) + bottom, fw = left + glassW + panelW;
			L.Face = new RectangleF (Margin, Margin, fw, fh);
			L.FaceR = 46;
			L.Screen = new Rectangle ((int) Math.Round (Margin + left + b), (int) Math.Round (Margin + top + (fh - top - bottom - glassH) / 2 + b), (int) Math.Round (sw), (int) Math.Round (sh));
			L.Glass = new RectangleF (L.Screen.X - b, L.Screen.Y - b, glassW, glassH);
			L.GlassR = 4.6f * UnitAt (s);
			L.ScreenR = L.GlassR - b;
			L.LedSet (L.Glass.X + glassW / 2, Margin + top * 0.5f, Math.Min (u, 8));
			L.Lower (dev, new PointF (L.Glass.Right, Margin + top + (fh - top - bottom - panelH) / 2 - (LowerTop + 3) * u), u, GlassX, GlassW);
			L.Client = new Size ((int) Math.Ceiling (fw) + 2 * Margin, (int) Math.Ceiling (fh) + 2 * Margin);
			return L;
		}

		void LedSet (float cx, float cy, float u)
		{
			LedD = Math.Max (4, 0.78f * u);
			for (int i = 0; i < 4; i++) Leds[i] = new PointF (cx - 9 * u + i * 6 * u, cy);
		}

		// The controls and the keyboard: the face's millimetres (o: the face's corner, u: pixels a millimetre),
		// the keyboard's rows from kx over kw millimetres.
		void Lower (Device dev, PointF o, float u, float kx, float kw)
		{
			float m = u;
			Unit = u;
			Func<float, float> X = mm => o.X + mm * u, Y = mm => o.Y + mm * u;
			float sx = u;
			// the D-pad: four arms round its centre
			float cx = X (19.5f), cy = Y (88), a = 5.2f * m;
			PadCentre = new PointF (cx, cy); PadArm = a;
			Ctls.Add (new Ctl { Id = "Up", Kind = CtlKind.PadArm, R = new RectangleF (cx - a / 2, cy - 1.5f * a, a, a) });
			Ctls.Add (new Ctl { Id = "Down", Kind = CtlKind.PadArm, R = new RectangleF (cx - a / 2, cy + a / 2, a, a) });
			Ctls.Add (new Ctl { Id = "Left", Kind = CtlKind.PadArm, R = new RectangleF (cx - 1.5f * a, cy - a / 2, a, a) });
			Ctls.Add (new Ctl { Id = "Right", Kind = CtlKind.PadArm, R = new RectangleF (cx + a / 2, cy - a / 2, a, a) });
			// L, R
			for (int i = 0; i < 2; i++)
				Ctls.Add (new Ctl { Id = i == 0 ? "L" : "R", Label = i == 0 ? "L" : "R", Kind = CtlKind.Shoulder, R = new RectangleF (X (36.5f + i * 11), Y (86.75f) - 2.75f * m, 9 * sx, 5.5f * m) });
			// X (top), Y (left), A (right), B (bottom)
			float fx = X (74.5f), fy = Y (88), d = 6.4f * m, rr = 7.2f * m;
			foreach (var b in new[] { new { n = "X", dx = 0, dy = -1 }, new { n = "Y", dx = -1, dy = 0 }, new { n = "A", dx = 1, dy = 0 }, new { n = "B", dx = 0, dy = 1 } })
				Ctls.Add (new Ctl { Id = b.n, Label = b.n, Kind = CtlKind.Round, R = new RectangleF (fx + b.dx * rr - d / 2, fy + b.dy * rr - d / 2, d, d) });
			// Select, Start
			for (int i = 0; i < 2; i++)
				Ctls.Add (new Ctl { Id = i == 0 ? "Select" : "Start", Label = i == 0 ? "SELECT" : "START", Kind = CtlKind.Pill, R = new RectangleF (X (36 + i * 11.5f), Y (97.8f) - 1.8f * m, 10 * sx, 3.6f * m) });
			// the keyboard: 14 places a row across the glass' width, small rounded keys a little taller than wide
			float rowH = 10.2f * u, keyH = 7.9f * u, gap = 1.0f * u;
			for (int row = 0; row < dev.Rows.Count; row++)
			{
				int n = 0;
				foreach (var kd in dev.Rows[row]) n += kd.Span;
				float pitch = (kw * u + gap) / n;
				int i = 0;
				foreach (var kd in dev.Rows[row])
				{
					float x = (float) Math.Round (X (kx) + i * pitch), x1 = (float) Math.Round (X (kx) + (i + kd.Span) * pitch - gap);
					Ctls.Add (new Ctl { Id = kd.Id, Key = kd, Kind = CtlKind.Key, Label = kd.Label, R = new RectangleF (x, (float) Math.Round (Y (110) + row * rowH), x1 - x, (float) Math.Round (keyH)) });
					i += kd.Span;
				}
			}
		}

		public Ctl Find (string id) { foreach (var c in Ctls) if (c.Id == id) return c; return null; }
		public Ctl Hit (PointF p)
		{
			foreach (var c in Ctls)
			{
				if (!c.R.Contains (p)) continue;
				if (c.Kind == CtlKind.Round)
				{
					float dx = p.X - (c.R.X + c.R.Width / 2), dy = p.Y - (c.R.Y + c.R.Height / 2), rad = c.R.Width / 2;
					if (dx * dx + dy * dy > rad * rad) continue;
				}
				return c;
			}
			return null;
		}
	}

	// The face's view: draws the layout, turns the mouse into presses (held: repeats) and, on the screen,
	// into the Pi's pointer.
	class FaceView : Control
	{
		public FaceLayout L;
		public Device Dev;
		public Bitmap Remote;				// the Pi's screen (null: not connected)
		public bool Smooth;
		public string Idle = "Not connected";		// the text on the dark screen
		public event Action<int, int, int, int> Pointer;	// x, y on the Pi's screen, buttons, wheel
		readonly Timer repeat = new Timer ();
		readonly Timer flashTimer = new Timer { Interval = 130 };
		readonly Dictionary<string, DateTime> flash = new Dictionary<string, DateTime> ();
		string held;
		int buttons;

		static readonly Color Back = Color.FromArgb (236, 237, 240), Body = Color.FromArgb (22, 23, 26), Edge = Color.FromArgb (52, 54, 60),
			GlassC = Color.FromArgb (4, 4, 5), GlassEdge = Color.FromArgb (40, 42, 48), KeyC = Color.FromArgb (34, 36, 41),
			Cap = Color.FromArgb (48, 50, 57), Legend = Color.FromArgb (176, 180, 190), Dim = Color.FromArgb (130, 134, 144),
			Accent = Color.FromArgb (0x3D, 0x86, 0xDA), AccentHi = Color.FromArgb (0x7D, 0xB4, 0xF2), Locked = Color.FromArgb (0xF1, 0xB3, 0x3B);

		public FaceView ()
		{
			SetStyle (ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.Opaque, true);
			SetStyle (ControlStyles.Selectable, false);
			TabStop = false;
			repeat.Tick += (s, e) => { repeat.Interval = 60; if (held != null) Dev.Repeat (held); };
			flashTimer.Tick += (s, e) =>
			{
				foreach (string id in new List<string> (flash.Keys)) if (flash[id] <= DateTime.Now) { flash.Remove (id); InvalidateCtl (id); }
				if (flash.Count == 0) flashTimer.Stop ();
			};
		}

		// ---- the mouse ----
		public void DoDown (Point p)
		{
			Ctl c = L.Hit (p);
			if (c == null) return;
			held = c.Id;
			Dev.Press (c.Id);
			repeat.Interval = 400; repeat.Start ();
		}
		public void DoUp ()
		{
			repeat.Stop ();
			if (held != null) { string h = held; held = null; Dev.Release (h); }
		}
		// a control lit for a moment (pressed from the PC's keyboard)
		public void Flash (string id)
		{
			if (L.Find (id) == null) return;
			flash[id] = DateTime.Now.AddMilliseconds (140);
			flashTimer.Start ();
			InvalidateCtl (id);
		}
		public void InvalidateCtl (string id) { Ctl c = L.Find (id); if (c != null) Invalidate (Rectangle.Inflate (Rectangle.Ceiling (c.R), 3, 3)); }

		bool OnScreen (Point p, out int x, out int y)
		{
			x = y = 0;
			if (Remote == null || !L.Screen.Contains (p)) return false;
			RectangleF d = RemoteRect ();
			if (!d.Contains (p)) return false;
			x = Math.Min (Remote.Width - 1, (int) ((p.X - d.X) * Remote.Width / d.Width));
			y = Math.Min (Remote.Height - 1, (int) ((p.Y - d.Y) * Remote.Height / d.Height));
			return true;
		}
		static int Buttons (MouseButtons b) { return ((b & MouseButtons.Left) != 0 ? 1 : 0) | ((b & MouseButtons.Right) != 0 ? 2 : 0) | ((b & MouseButtons.Middle) != 0 ? 4 : 0); }
		void Ptr (MouseEventArgs e, int wheel)
		{
			int x, y;
			var h = Pointer;
			if (h == null) return;
			if (OnScreen (e.Location, out x, out y)) { buttons = Buttons (MouseButtons); h (x, y, buttons, wheel); }
			else if (buttons != 0 && Buttons (MouseButtons) == 0) { buttons = 0; h (0, 0, 0, 0); }	// (released outside)
		}
		protected override void OnMouseDown (MouseEventArgs e)
		{
			if (L.Screen.Contains (e.Location)) { Ptr (e, 0); return; }
			if (e.Button == MouseButtons.Left) DoDown (e.Location);
		}
		protected override void OnMouseUp (MouseEventArgs e)
		{
			if (e.Button == MouseButtons.Left) DoUp ();
			Ptr (e, 0);
		}
		protected override void OnMouseMove (MouseEventArgs e) { Ptr (e, 0); }
		// the wheel over the Pi's screen (the window gives it: this view never has the focus)
		public void Wheel (Point p, int wheel) { Ptr (new MouseEventArgs (MouseButtons.None, 0, p.X, p.Y, 0), wheel); }

		// ---- drawing ----
		// where the Pi's screen is drawn: the whole screen area at 640 x 480, else fitted inside it
		public RectangleF RemoteRect ()
		{
			Rectangle s = L.Screen;
			if (Remote == null || (Remote.Width == 640 && Remote.Height == 480)) return s;
			float k = Math.Min ((float) s.Width / Remote.Width, (float) s.Height / Remote.Height);
			float w = Remote.Width * k, h = Remote.Height * k;
			return new RectangleF (s.X + (s.Width - w) / 2, s.Y + (s.Height - h) / 2, w, h);
		}

		static GraphicsPath Round (RectangleF r, float rad)
		{
			var p = new GraphicsPath ();
			float d = Math.Max (0.5f, Math.Min (rad * 2, Math.Min (r.Width, r.Height)));
			p.AddArc (r.X, r.Y, d, d, 180, 90); p.AddArc (r.Right - d, r.Y, d, d, 270, 90);
			p.AddArc (r.Right - d, r.Bottom - d, d, d, 0, 90); p.AddArc (r.X, r.Bottom - d, d, d, 90, 90);
			p.CloseFigure ();
			return p;
		}
		static void Fill (Graphics g, RectangleF r, float rad, Color c) { using (var p = Round (r, rad)) using (var b = new SolidBrush (c)) g.FillPath (b, p); }
		static void Txt (Graphics g, string s, Font f, Color c, RectangleF r, StringAlignment h = StringAlignment.Center, StringAlignment v = StringAlignment.Center)
		{
			using (var b = new SolidBrush (c)) using (var sf = new StringFormat (StringFormat.GenericTypographic) { Alignment = h, LineAlignment = v, Trimming = StringTrimming.None })
			{
				sf.FormatFlags |= StringFormatFlags.NoWrap | StringFormatFlags.NoClip;
				g.DrawString (s, f, b, r, sf);
			}
		}
		// a legend in a box: the font made smaller until it fits the width
		static void Fit (Graphics g, string s, float px, bool bold, bool symbol, Color c, RectangleF r, float room, StringAlignment h = StringAlignment.Center, StringAlignment v = StringAlignment.Center)
		{
			for (;;)
			{
				using (var f = F (px, bold, symbol))
				{
					if (px > 7 && g.MeasureString (s, f, 1000, StringFormat.GenericTypographic).Width > room) { px -= 0.5f; continue; }
					Txt (g, s, f, c, r, h, v);
					return;
				}
			}
		}
		static string Short (string t)
		{
			switch (t.ToLowerInvariant ())
			{
			case "pageup": return "PgUp"; case "pagedown": return "PgDn"; case "delete": return "Del"; case "backspace": return "Bksp";
			case "escape": return "Esc"; case "insert": return "Ins";
			}
			return t;
		}
		static Font F (float px, bool bold = false, bool symbol = false) { return new Font (symbol ? "Segoe UI Symbol" : "Segoe UI", Math.Max (6f, px), bold ? FontStyle.Bold : FontStyle.Regular, GraphicsUnit.Pixel); }

		bool Lit (string id) { return Dev.IsDown (id) || flash.ContainsKey (id); }

		protected override void OnPaint (PaintEventArgs e) { Draw (e.Graphics); }

		public void Draw (Graphics g)
		{
			g.Clear (Back);
			g.SmoothingMode = SmoothingMode.AntiAlias;
			g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
			g.PixelOffsetMode = PixelOffsetMode.HighQuality;
			Fill (g, L.Face, L.FaceR, Body);
			using (var p = Round (L.Face, L.FaceR)) using (var pen = new Pen (Edge, 2)) g.DrawPath (pen, p);
			for (int i = 0; i < 4; i++)
				using (var b = new SolidBrush (i == 0 && Remote != null ? Color.FromArgb (60, 200, 110) : Color.FromArgb (46, 48, 54)))
					g.FillEllipse (b, L.Leds[i].X - L.LedD / 2, L.Leds[i].Y - L.LedD / 2, L.LedD, L.LedD);
			DrawScreen (g);
			DrawPad (g);
			foreach (Ctl c in L.Ctls) if (c.Kind != CtlKind.PadArm) DrawCtl (g, c);
		}

		void DrawScreen (Graphics g)
		{
			Rectangle s = L.Screen;
			if (Remote == null)
			{
				using (var b = new SolidBrush (Color.FromArgb (11, 12, 14))) g.FillRectangle (b, s);
				using (var f = F (Math.Max (11, 17 * L.Scale))) Txt (g, Idle, f, Color.FromArgb (113, 119, 131), s);
			}
			else
			{
				RectangleF d = RemoteRect ();
				var st = g.Save ();
				if (d.Width < s.Width || d.Height < s.Height) g.FillRectangle (Brushes.Black, s);
				bool exact = Remote.Width == 640 && Remote.Height == 480 && Math.Abs (L.Scale - Math.Round (L.Scale)) < 0.001f;
				g.SmoothingMode = SmoothingMode.None;
				g.InterpolationMode = exact && !(Smooth && L.Scale > 1) ? InterpolationMode.NearestNeighbor : InterpolationMode.HighQualityBicubic;
				g.PixelOffsetMode = PixelOffsetMode.Half;
				g.CompositingMode = CompositingMode.SourceCopy;
				using (var ia = new System.Drawing.Imaging.ImageAttributes ())
				{
					ia.SetWrapMode (WrapMode.TileFlipXY);		// (no dark fringe at the edges when scaled)
					g.DrawImage (Remote, Rectangle.Round (d), 0, 0, Remote.Width, Remote.Height, GraphicsUnit.Pixel, ia);
				}
				g.Restore (st);
			}
			// the glass over the picture: its black border, the picture's corners rounded (the glass' shape less
			// the picture's rounded shape, the second inside the first)
			using (var p = Round (L.Glass, L.GlassR)) using (var inner = Round (s, L.ScreenR)) using (var b = new SolidBrush (GlassC))
			{
				p.AddPath (inner, false);
				g.FillPath (b, p);
			}
			using (var p = Round (L.Glass, L.GlassR)) using (var pen = new Pen (GlassEdge, 1)) g.DrawPath (pen, p);
		}

		void DrawPad (Graphics g)
		{
			float a = L.PadArm, cx = L.PadCentre.X, cy = L.PadCentre.Y;
			Fill (g, new RectangleF (cx - 1.5f * a, cy - a / 2, 3 * a, a), a * 0.25f, Cap);
			Fill (g, new RectangleF (cx - a / 2, cy - 1.5f * a, a, 3 * a), a * 0.25f, Cap);
			foreach (Ctl c in L.Ctls)
			{
				if (c.Kind != CtlKind.PadArm) continue;
				if (Lit (c.Id)) Fill (g, c.R, a * 0.25f, Accent);
				// a small arrow head on each arm
				float mx = c.R.X + c.R.Width / 2, my = c.R.Y + c.R.Height / 2, t = a * 0.2f;
				int dx = c.Id == "Left" ? -1 : c.Id == "Right" ? 1 : 0, dy = c.Id == "Up" ? -1 : c.Id == "Down" ? 1 : 0;
				var pts = new[] { new PointF (mx + dx * t, my + dy * t), new PointF (mx - dx * t + dy * t, my - dy * t + dx * t), new PointF (mx - dx * t - dy * t, my - dy * t - dx * t) };
				using (var b = new SolidBrush (Lit (c.Id) ? Color.White : Dim)) g.FillPolygon (b, pts);
			}
		}

		void DrawCtl (Graphics g, Ctl c)
		{
			bool lit = Lit (c.Id);
			float m = L.Unit;
			if (c.Kind == CtlKind.Round)
			{
				using (var b = new SolidBrush (lit ? Accent : Cap)) g.FillEllipse (b, c.R);
				using (var f = F (3.4f * m, true)) Txt (g, c.Label, f, lit ? Color.White : Legend, c.R);
			}
			else if (c.Kind == CtlKind.Shoulder)
			{
				Fill (g, c.R, 1.6f * m, lit ? Accent : Cap);
				using (var f = F (3.2f * m, true)) Txt (g, c.Label, f, lit ? Color.White : Legend, c.R);
			}
			else if (c.Kind == CtlKind.Pill)
			{
				Fill (g, c.R, c.R.Height / 2, lit ? Accent : Cap);
				using (var f = F (Math.Max (8.5f, 1.9f * m), true)) Txt (g, c.Label, f, Dim, new RectangleF (c.R.X - 20, c.R.Bottom + 1, c.R.Width + 40, Math.Max (12, 4 * m)));
			}
			else DrawKey (g, c, lit);
		}

		void DrawKey (Graphics g, Ctl c, bool lit)
		{
			KeyDef d = c.Key;
			int sticky = Dev.Sticky (d.Id);
			bool on = sticky != 0 || (d.Mod == Mod.Caps && Dev.Caps);
			bool fn = Dev.Active (Mod.Fn);
			Chord layer = d.Mod == Mod.None ? Dev.FnOf (d) : null;
			Color body = lit ? Accent : on ? Color.FromArgb (38, 62, 96) : KeyC;
			float rad = Math.Min (1.4f * L.Unit, c.R.Height / 4);
			Fill (g, c.R, rad, body);
			if (on) using (var p = Round (RectangleF.Inflate (c.R, -1, -1), rad)) using (var pen = new Pen (sticky == 2 ? Locked : Accent, 2)) g.DrawPath (pen, p);
			float size = Math.Max (9, Math.Min (c.R.Height * 0.5f, c.R.Width * 0.42f)), room = c.R.Width - 6;
			string label = d.Label;
			if (d.Mod == Mod.None && d.Sym == 0 && d.Base != ' ' && !char.IsLetter (d.Base)) label = Dev.CharOf (d).ToString ();	// (a letter: its capital, as printed)
			bool small = layer != null && c.R.Height >= 22;
			Color ink = lit || on ? Color.White : Legend;
			if (fn && layer != null)			// the Fn layer shown: its legend takes the key
			{
				Fit (g, Short (layer.Text), Math.Min (size, c.R.Width * 0.3f), true, false, lit ? Color.White : AccentHi, c.R, room);
				return;
			}
			RectangleF main = small ? new RectangleF (c.R.X, c.R.Y + c.R.Height * 0.2f, c.R.Width, c.R.Height * 0.8f) : c.R;
			Fit (g, label, size, false, d.Glyph, ink, main, room);
			if (small)
				Fit (g, Short (layer.Text), Math.Max (7.5f, Math.Min (c.R.Height * 0.3f, c.R.Width * 0.23f)), true, false, lit ? Color.White : AccentHi,
				     new RectangleF (c.R.X, c.R.Y + 1, c.R.Width - 3, c.R.Height * 0.4f), room, StringAlignment.Far, StringAlignment.Near);
			if (sticky == 2) using (var b = new SolidBrush (Locked)) g.FillEllipse (b, c.R.X + 4, c.R.Y + 4, 4, 4);
		}
	}
}
