// Dialogs.cs -- Export (the meshes: STL, OBJ; a flat drawing from a side: DXF, SVG, PDF; a picture: PNG), the G-code
// (what was checked, how it starts) and the print file of a resin printer (what was checked). What they show is the
// core's (main.cpp's dialogs on Onyx); the file is chosen by Windows' own dialog.
// MIT licence (Onyx).
using System;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using Microsoft.Win32;

using static Forge.Lang;

namespace Forge
{
	class DialogBase : Window
	{
		protected readonly Grid G = new Grid { Margin = new Thickness (20, 16, 20, 10) };
		protected readonly StackPanel Buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness (20, 4, 20, 16) };
		protected DialogBase (Window owner, string title, double width)
		{
			Owner = owner; Title = title; Width = width; SizeToContent = SizeToContent.Height; ResizeMode = ResizeMode.NoResize;
			WindowStartupLocation = WindowStartupLocation.CenterOwner; ShowInTaskbar = false; Background = (Brush) FindResource ("Panel");
			FontFamily = new FontFamily ("Segoe UI"); FontSize = 12.5; UseLayoutRounding = true;
			var dp = new DockPanel (); DockPanel.SetDock (Buttons, Dock.Bottom); dp.Children.Add (Buttons); dp.Children.Add (G);
			Content = dp;
			G.ColumnDefinitions.Add (new ColumnDefinition { Width = new GridLength (110) }); G.ColumnDefinitions.Add (new ColumnDefinition ());
		}
		protected int row;
		protected void Row (string label, UIElement e, double top = 6)
		{
			G.RowDefinitions.Add (new RowDefinition { Height = GridLength.Auto });
			if (label != null) { var t = new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness (0, top, 0, 0) }; Grid.SetRow (t, row); G.Children.Add (t); }
			if (e is FrameworkElement fe) fe.Margin = new Thickness (0, top, 0, 0);
			Grid.SetRow (e, row); Grid.SetColumn (e, label != null ? 1 : 0); if (label == null) Grid.SetColumnSpan (e, 2);
			G.Children.Add (e); row++;
		}
		protected Button Add (string text, bool accent, Action a)
		{
			var b = new Button { Content = text, Style = (Style) FindResource (accent ? "AccentButton" : "Plain"), MinWidth = 92, Margin = new Thickness (8, 0, 0, 0), IsDefault = accent, IsCancel = !accent };
			b.Click += (s, e) => a ();
			Buttons.Children.Add (b); return b;
		}
		protected (Border box, UniformGrid g, RadioButton[] r) Segmented (string[] opts, int sel, Action<int> changed)
		{
			var ug = new UniformGrid { Rows = 1 }; var rs = new RadioButton[opts.Length]; string grp = Guid.NewGuid ().ToString ();
			for (int i = 0; i < opts.Length; i++)
			{
				int k = i; rs[i] = new RadioButton { Content = opts[i], GroupName = grp, IsChecked = i == sel, Style = (Style) FindResource ("Seg") };
				rs[i].Checked += (s, e) => changed (k); ug.Children.Add (rs[i]);
			}
			var box = new Border { Child = ug, BorderBrush = (Brush) FindResource ("FieldLine"), BorderThickness = new Thickness (1), CornerRadius = new CornerRadius (4), Background = Brushes.White, Height = 30 };
			return (box, ug, rs);
		}
		// The checks: a mark and a line each ("state<TAB>text": 0 right, 1 to look at, 2 wrong).
		protected StackPanel Checks (string text)
		{
			var sp = new StackPanel ();
			foreach (var l in text.Split ('\n').Where (x => x.Length > 1))
			{
				int st = l[0] - '0'; string t = l.Substring (2);
				var r = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness (0, 3, 0, 3) };
				r.Children.Add (Icons.Image (st == 0 ? I.CHECK : I.WARN, 16, st == 0 ? 0x3C9650u : st == 2 ? 0xB03A30u : 0x4A3A10u, st == 0 ? 0x3C9650u : Icons.ACC));
				r.Children.Add (new TextBlock { Text = t, Margin = new Thickness (10, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center, TextWrapping = TextWrapping.Wrap, MaxWidth = Width - 90,
					Foreground = st == 2 ? new SolidColorBrush (Color.FromRgb (0xB0, 0x3A, 0x30)) : (Brush) FindResource ("Ink"), FontWeight = st == 2 ? FontWeights.SemiBold : FontWeights.Normal });
				sp.Children.Add (r);
			}
			return sp;
		}
		protected static string BaseName ()
		{
			string p = Core.Path; return p.Length > 0 ? Path.GetFileNameWithoutExtension (p) : Tr ("part");
		}
	}

	class ExportDialog : DialogBase
	{
		static readonly string[] FMT = { "STL", "OBJ", "DXF", "SVG", "PDF", "PNG" }, EXT = { ".stl", ".obj", ".dxf", ".svg", ".pdf", ".png" };
		static readonly string[] DESC = { Tr ("STL meshes"), Tr ("OBJ meshes"), Tr ("DXF drawings"), Tr ("SVG drawings"), Tr ("PDF drawings"), Tr ("PNG pictures") };
		static int sFmt, sView, sCurve = -1; static bool sBin = true, sHid;
		int fmt = sFmt, what, curve, view = sView; bool bin = sBin, hid = sHid;
		readonly ComboBox viewD = new ComboBox { Width = 180, HorizontalAlignment = HorizontalAlignment.Left, Height = 26 };
		readonly TextBlock curveNote = new TextBlock (), viewNote = new TextBlock (), sum = new TextBlock { VerticalAlignment = VerticalAlignment.Center, TextWrapping = TextWrapping.Wrap };
		readonly CheckBox binC = new CheckBox { Content = Tr ("Binary file (smaller)") }, hidC = new CheckBox { Content = Tr ("Hidden edges too, dashed") };
		readonly ContentControl sumIcon = new ContentControl { Width = 18, Height = 18, Margin = new Thickness (0, 0, 10, 0), Focusable = false };
		readonly FrameworkElement viewRow, checkRow;
		double[] info = new double[8];

		public ExportDialog (Window owner) : base (owner, Tr ("Export"), 520)
		{
			var stt = Core.State ();
			int segs = Core.fc_doc_segs (); curve = sCurve >= 0 ? sCurve : segs <= 48 ? 0 : segs <= 96 ? 1 : 2;
			Row (Tr ("Format"), Segmented (FMT, fmt, k => { fmt = k; Recompute (); }).box, 0);
			var w = Segmented (new[] { Tr ("Whole part"), Tr ("Selected body") }, 0, k => { what = k; Recompute (); });
			if (stt.SelBody < 0) w.r[1].IsEnabled = false;
			Row (Tr ("What"), w.box, 10);
			var cs = new StackPanel (); cs.Children.Add (Segmented (new[] { Tr ("Draft"), Tr ("Fine"), Tr ("Very fine") }, curve, k => { curve = k; Recompute (); }).box);
			curveNote.FontSize = 11.5; curveNote.Foreground = (Brush) FindResource ("Dim"); curveNote.Margin = new Thickness (2, 4, 0, 0); cs.Children.Add (curveNote);
			Row (Tr ("Curves"), cs, 10);
			foreach (var v in new[] { Tr ("Front"), Tr ("Back"), Tr ("Left"), Tr ("Right"), Tr ("Top"), Tr ("Bottom"), Tr ("As on screen") }) viewD.Items.Add (v);
			viewD.SelectedIndex = view; viewD.SelectionChanged += (s, e) => { view = viewD.SelectedIndex; Recompute (); };
			var vs = new StackPanel { Orientation = Orientation.Horizontal }; vs.Children.Add (viewD);
			viewNote.Foreground = (Brush) FindResource ("Dim"); viewNote.FontSize = 11.5; viewNote.VerticalAlignment = VerticalAlignment.Center; viewNote.Margin = new Thickness (12, 0, 0, 0); vs.Children.Add (viewNote);
			Row (Tr ("Seen from"), vs, 10); viewRow = vs;
			binC.IsChecked = bin; hidC.IsChecked = hid;
			binC.Click += (s, e) => { bin = binC.IsChecked == true; Recompute (); }; hidC.Click += (s, e) => { hid = hidC.IsChecked == true; Recompute (); };
			var ck = new StackPanel (); ck.Children.Add (binC); ck.Children.Add (hidC); Row (null, ck, 10); checkRow = ck;
			var sp = new StackPanel { Orientation = Orientation.Horizontal }; sp.Children.Add (sumIcon); sp.Children.Add (sum);
			Row (null, new Border { Child = sp, Background = Brushes.White, BorderBrush = (Brush) FindResource ("Line"), BorderThickness = new Thickness (1), CornerRadius = new CornerRadius (6), Padding = new Thickness (12, 9, 12, 9) }, 16);
			Add (Tr ("Cancel"), false, Close);
			Add (Tr ("Export..."), true, Export);
			Loaded += (s, e) => Recompute ();
		}
		bool Flat => fmt >= 2;
		void Recompute ()
		{
			viewRow.Visibility = Flat ? Visibility.Visible : Visibility.Collapsed;
			((FrameworkElement) G.Children.OfType<TextBlock> ().First (t => t.Text == Tr ("Seen from"))).Visibility = viewRow.Visibility;
			binC.Visibility = fmt == 0 ? Visibility.Visible : Visibility.Collapsed; hidC.Visibility = fmt >= 2 && fmt <= 4 ? Visibility.Visible : Visibility.Collapsed;
			checkRow.Visibility = fmt == 0 || (fmt >= 2 && fmt <= 4) ? Visibility.Visible : Visibility.Collapsed;
			curveNote.Text = string.Format (Tr ("{0} sides to a circle"), curve == 0 ? 48 : curve == 1 ? 96 : 192);
			viewNote.Text = fmt == 5 ? Tr ("1024 pixels") : Tr ("at its size, in mm");
			Cursor = System.Windows.Input.Cursors.Wait;
			try { info = Core.Export (fmt, what, curve, view, bin, hid, BaseName ()); } finally { Cursor = null; }
			double bytes = info[4]; bool closed = info[5] != 0, ok = bytes > 0 && (Flat || closed);
			string size = bytes >= 1048576 ? string.Format (Tr ("{0} MB"), (bytes / 1048576).ToString ("0.0")) : string.Format (Tr ("{0} KB"), Math.Ceiling (bytes / 1024));
			if (bytes <= 0) sum.Text = Tr ("Nothing to export yet");
			else if (fmt == 5) sum.Text = string.Format (Tr ("{0} × {1} pixels · {2}"), info[2], info[3], size);
			else if (Flat) sum.Text = string.Format (Tr ("{0} lines · {1} × {2} mm · {3}"), info[1], info[6].ToString ("0.0"), info[7].ToString ("0.0"), size);
			else sum.Text = string.Format (Tr ("{0} triangles · {1} · {2}"), info[0].ToString ("N0"), size, closed ? Tr ("closed solid") : Tr ("not closed"));
			sumIcon.Content = Icons.Image (ok ? I.CHECK : I.WARN, 18, ok ? 0x3C9650u : 0x4A3A10u, ok ? 0x3C9650u : Icons.ACC);
		}
		void Export ()
		{
			if (info[4] <= 0) { MessageBox.Show (this, Tr ("There is nothing to export yet."), Tr ("Export"), MessageBoxButton.OK, MessageBoxImage.Information); return; }
			var d = new SaveFileDialog { Filter = DESC[fmt] + " (*" + EXT[fmt] + ")|*" + EXT[fmt], DefaultExt = EXT[fmt], AddExtension = true, FileName = BaseName () + EXT[fmt], InitialDirectory = MainWindow.LastDir };
			if (d.ShowDialog (this) != true) return;
			if (fmt == 4) Core.Export (fmt, what, curve, view, bin, hid, Path.GetFileNameWithoutExtension (d.FileName));	// (its title: the file's name)
			if (!Core.ExportWrite (d.FileName)) { MessageBox.Show (this, Tr ("The file could not be written."), Tr ("Export"), MessageBoxButton.OK, MessageBoxImage.Warning); return; }
			MainWindow.LastDir = Path.GetDirectoryName (d.FileName);
			sFmt = fmt; sView = view; sCurve = curve; sBin = bin; sHid = hid;
			Core.SetHint (string.Format (Tr ("Exported: {0}."), Path.GetFileName (d.FileName)));
			Close ();
		}
	}

	class GcodeDialog : DialogBase
	{
		readonly bool good;
		public GcodeDialog (Window owner) : base (owner, Tr ("G-code"), 640)
		{
			good = Core.Gcode (out string checks, out string sum);
			Row (null, Checks (checks), 0);
			var code = Core.GcodeText; var lines = code.Split ('\n');
			var tb = new TextBox { Text = string.Join ("\n", lines.Take (400)) + (lines.Length > 400 ? "\n..." : ""), IsReadOnly = true, FontFamily = new FontFamily ("Consolas"), FontSize = 12,
				Height = 240, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, Background = Brushes.White, BorderBrush = (Brush) FindResource ("FieldLine"), Padding = new Thickness (6) };
			Row (null, tb, 10);
			Row (null, new TextBlock { Text = sum, Foreground = (Brush) FindResource ("Dim"), FontSize = 11.5, TextWrapping = TextWrapping.Wrap }, 8);
			Row (null, new TextBlock { Text = good ? Tr (".nc · GRBL, millimetres · try it in the air first") : Tr ("Nothing is written until this is put right."), FontSize = 11.5,
				Foreground = good ? (Brush) FindResource ("Dim") : new SolidColorBrush (Color.FromRgb (0xB0, 0x3A, 0x30)) }, 4);
			Add (Tr ("Cancel"), false, Close);
			var s = Add (Tr ("Save..."), true, Save); s.IsEnabled = good;
		}
		void Save ()
		{
			var d = new SaveFileDialog { Filter = Tr ("G-code (*.nc)|*.nc|G-code (*.gcode)|*.gcode"), DefaultExt = ".nc", AddExtension = true, FileName = BaseName () + ".nc", InitialDirectory = MainWindow.LastDir };
			if (d.ShowDialog (this) != true) return;
			if (!Core.GcodeWrite (d.FileName)) { MessageBox.Show (this, Tr ("The file could not be written."), Tr ("G-code"), MessageBoxButton.OK, MessageBoxImage.Warning); return; }
			MainWindow.LastDir = Path.GetDirectoryName (d.FileName);
			Core.SetHint (Tr ("G-code written. Run it in the air first, the spindle well above the stock."));
			Close ();
		}
	}

	class PrintFileDialog : DialogBase
	{
		readonly string ext;
		public PrintFileDialog (Window owner) : base (owner, Tr ("Print file"), 560)
		{
			bool good = Core.PrintFile (out string checks, out string sum, out ext);
			Row (null, Checks (checks), 0);
			Row (null, new TextBlock { Text = sum, Foreground = (Brush) FindResource ("Dim"), FontSize = 11.5, TextWrapping = TextWrapping.Wrap }, 10);
			if (!good) Row (null, new TextBlock { Text = Tr ("Nothing is written until this is put right."), FontSize = 11.5, Foreground = new SolidColorBrush (Color.FromRgb (0xB0, 0x3A, 0x30)) }, 6);
			Add (Tr ("Cancel"), false, Close);
			var s = Add (Tr ("Save..."), true, Save); s.IsEnabled = good;
		}
		void Save ()
		{
			var d = new SaveFileDialog { Filter = Tr ("Print file") + " (*." + ext + ")|*." + ext, DefaultExt = "." + ext, AddExtension = true, FileName = BaseName () + "." + ext, InitialDirectory = MainWindow.LastDir };
			if (d.ShowDialog (this) != true) return;
			if (!Core.PrintFileWrite (d.FileName)) { MessageBox.Show (this, Tr ("The file could not be written."), Tr ("Print file"), MessageBoxButton.OK, MessageBoxImage.Warning); return; }
			MainWindow.LastDir = Path.GetDirectoryName (d.FileName);
			Core.SetHint (Tr ("The print file is written."));
			Close ();
		}
	}
}
