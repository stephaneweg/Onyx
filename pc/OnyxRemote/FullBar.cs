// FullBar.cs -- the full screen's connection bar (as mstsc's): at the top of the screen, in its
// middle, when the pointer touches the screen's top edge; hidden again ~1 s after the pointer left
// it, unless pinned. A pin, the Pi's name, Screenshot, Minimise, Leave full screen, Disconnect -- dark, its
// bottom corners rounded. It never takes the focus (the keys stay with the Onyx window:
// WS_EX_NOACTIVATE); above the overlays (TopMost).
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace OnyxRemote
{
	// the bar's colours (ToolStripProfessionalRenderer)
	class FullBarColors : ProfessionalColorTable
	{
		static readonly Color Top = FullBarRenderer.Top, Bottom = FullBarRenderer.Bottom;
		static readonly Color Hot = Color.FromArgb (70, 120, 190), HotEdge = Color.FromArgb (110, 160, 230), Down = Color.FromArgb (50, 95, 160);
		public override Color ToolStripGradientBegin { get { return Top; } }
		public override Color ToolStripGradientMiddle { get { return Top; } }
		public override Color ToolStripGradientEnd { get { return Bottom; } }
		public override Color ToolStripBorder { get { return Bottom; } }
		public override Color ButtonSelectedHighlight { get { return Hot; } }
		public override Color ButtonSelectedGradientBegin { get { return Hot; } }
		public override Color ButtonSelectedGradientMiddle { get { return Hot; } }
		public override Color ButtonSelectedGradientEnd { get { return Hot; } }
		public override Color ButtonSelectedBorder { get { return HotEdge; } }
		public override Color ButtonPressedGradientBegin { get { return Down; } }
		public override Color ButtonPressedGradientMiddle { get { return Down; } }
		public override Color ButtonPressedGradientEnd { get { return Down; } }
		public override Color ButtonPressedBorder { get { return HotEdge; } }
		public override Color ButtonCheckedGradientBegin { get { return Down; } }
		public override Color ButtonCheckedGradientMiddle { get { return Down; } }
		public override Color ButtonCheckedGradientEnd { get { return Down; } }
		public override Color ButtonCheckedHighlight { get { return Down; } }
		public override Color ButtonCheckedHighlightBorder { get { return HotEdge; } }
		public override Color SeparatorDark { get { return Color.FromArgb (20, 28, 40); } }
		public override Color SeparatorLight { get { return Color.FromArgb (80, 96, 120); } }
	}

	// Its face: a vertical gradient (a lighter line at the very top), a darker border along the
	// sides and the rounded bottom.
	class FullBarRenderer : ToolStripProfessionalRenderer
	{
		public static readonly Color Top = Color.FromArgb (62, 82, 112), Bottom = Color.FromArgb (22, 30, 44);
		public static readonly Color Edge = Color.FromArgb (10, 14, 22), Shine = Color.FromArgb (96, 120, 156);
		public FullBarRenderer () : base (new FullBarColors ()) { RoundedEdges = false; }

		protected override void OnRenderToolStripBackground (ToolStripRenderEventArgs e)
		{
			var r = new Rectangle (Point.Empty, e.ToolStrip.Size);
			using (var b = new LinearGradientBrush (r, Top, Bottom, LinearGradientMode.Vertical))
				e.Graphics.FillRectangle (b, r);
			using (var p = new Pen (Shine)) e.Graphics.DrawLine (p, 1, 0, r.Width - 2, 0);
		}

		protected override void OnRenderToolStripBorder (ToolStripRenderEventArgs e)
		{
			var g = e.Graphics;
			g.SmoothingMode = SmoothingMode.AntiAlias;
			using (var path = FullBar.Shape (e.ToolStrip.Width - 1, e.ToolStrip.Height - 1))
			using (var p = new Pen (Edge)) g.DrawPath (p, path);
			g.SmoothingMode = SmoothingMode.Default;
		}
	}

	class FullBar : Form
	{
		readonly ToolStrip ts;
		readonly ToolStripLabel name = new ToolStripLabel ();
		readonly ToolStripButton pin = new ToolStripButton ("Pin") { CheckOnClick = true, ToolTipText = "Keep this bar shown" };
		public bool Pinned { get { return pin.Checked; } }
		const int R = 8;						// (the bottom corners' radius)

		public FullBar (Form owner, Action minimise, Action leave, Action disconnect, Action screenshot)
		{
			FormBorderStyle = FormBorderStyle.None; ShowInTaskbar = false; StartPosition = FormStartPosition.Manual;
			ControlBox = false; MinimizeBox = false; MaximizeBox = false; Text = "";
			TopMost = true;
			Owner = owner;
			DoubleBuffered = true;
			Font = new Font ("Segoe UI", 9.5f);
			BackColor = FullBarRenderer.Bottom;
			ts = new ToolStrip
			{
				GripStyle = ToolStripGripStyle.Hidden, Dock = DockStyle.Fill, AutoSize = false,
				Renderer = new FullBarRenderer (),
				Padding = new Padding (8, 3, 8, 3), CanOverflow = false, TabStop = false,
			};
			var min = new ToolStripButton ("Minimise") { Alignment = ToolStripItemAlignment.Right };
			var restore = new ToolStripButton ("Leave full screen") { ToolTipText = "F11", Alignment = ToolStripItemAlignment.Right };
			var disc = new ToolStripButton ("Disconnect") { Alignment = ToolStripItemAlignment.Right };
			var shot = new ToolStripButton ("Screenshot") { ToolTipText = "Save the screen to Pictures\\Onyx (Ctrl+Shift+S)", Alignment = ToolStripItemAlignment.Right };
			name.Font = new Font ("Segoe UI Semibold", 9.5f);
			min.Click += (s, e) => minimise ();
			restore.Click += (s, e) => leave ();
			disc.Click += (s, e) => disconnect ();
			shot.Click += (s, e) => screenshot ();
			// (right-aligned items are laid out from the right: the last added is the leftmost)
			ts.Items.AddRange (new ToolStripItem[] { pin, new ToolStripSeparator (), name, disc, restore, min, shot });
			foreach (ToolStripItem it in ts.Items) { it.ForeColor = Color.White; it.Margin = new Padding (2, 1, 2, 1); }
			Controls.Add (ts);
			Fit ();
		}

		// the width for the items (the name included), the bottom corners rounded
		void Fit ()
		{
			int w = 40;
			foreach (ToolStripItem it in ts.Items) w += it.GetPreferredSize (Size.Empty).Width + it.Margin.Horizontal;
			ClientSize = new Size (Math.Max (420, w + 24), 34);
			var old = Region;
			using (var p = Shape (ClientSize.Width, ClientSize.Height)) Region = new Region (p);
			old?.Dispose ();
		}

		// the bar's outline, w x h: square at the top (against the screen's edge), rounded below
		public static GraphicsPath Shape (int w, int h)
		{
			var p = new GraphicsPath ();
			int d = 2 * R;
			p.AddLine (0, 0, w, 0);
			p.AddArc (w - d, h - d, d, d, 0, 90);
			p.AddArc (0, h - d, d, d, 90, 90);
			p.CloseFigure ();
			return p;
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

		// Shown at the top of that screen, in its middle, with the Pi's name.
		public void ShowOn (Rectangle screen, string pi)
		{
			if (name.Text != pi) { name.Text = pi; Fit (); }
			var at = new Point (screen.Left + (screen.Width - Width) / 2, screen.Top);
			if (Location != at) Location = at;
			if (!Visible) Show ();
		}

		const int WS_EX_TOOLWINDOW = 0x80, WS_EX_NOACTIVATE = 0x08000000;
		const int WM_MOUSEACTIVATE = 0x21, MA_NOACTIVATE = 3;
	}
}
