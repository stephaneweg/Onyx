// FormPanel.cs -- the panel at the right: what is selected, or the step being made -- its values as fields, its
// operation, its checks (fui.h's Props). The core says which controls it holds (fc_form: a list of items,
// core/forgecore.cpp tells their kinds); they are made once for a layout, then only their values are put again --
// but a field being typed, whose number has not changed.
// MIT licence (Onyx).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;

namespace Forge
{
	class FormPanel : DockPanel
	{
		readonly StackPanel head = new StackPanel (), body = new StackPanel (), foot = new StackPanel ();
		readonly ScrollViewer sv;
		List<Ctl> ctls = new List<Ctl> ();
		string sig = "";
		public event Action Changed;		// (a value given: the window asks the core what changed)
		public Action<int> Command;		// (a button: the window runs it -- some open a menu or a dialog)
		public static bool Updating;		// (the controls' values being put: their events are not the user's)

		public FormPanel ()
		{
			Background = (Brush) Application.Current.FindResource ("Panel");
			head.Margin = new Thickness (0, 0, 0, 4);
			SetDock (head, Dock.Top); Children.Add (head);
			foot.Margin = new Thickness (12, 6, 12, 12);
			SetDock (foot, Dock.Bottom); Children.Add (foot);
			sv = new ScrollViewer { VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, Content = body, Focusable = false };
			Children.Add (sv);
			body.Margin = new Thickness (0, 0, 0, 10);
		}
		internal void Fire () { if (!Updating) Changed?.Invoke (); }
		internal void Run (int cmd) { Command?.Invoke (cmd); }

		public void Refresh ()
		{
			string form = Core.Form ();
			var items = form.Split ('\x1E').Where (s => s.Length > 0).Select (s => s.Split ('\x1F')).ToList ();
			var s = new StringBuilder ();
			foreach (var f in items) s.Append (Ctl.Signature (f)).Append ('\n');
			Updating = true;
			try
			{
				if (s.ToString () != sig) { sig = s.ToString (); Build (items); }
				else for (int i = 0; i < ctls.Count && i < items.Count; i++) ctls[i].Update (items[i]);
			}
			finally { Updating = false; }
		}
		void Build (List<string[]> items)
		{
			// (the field that has the keyboard keeps it when the new one is the same: the panel made again under it)
			head.Children.Clear (); body.Children.Clear (); foot.Children.Clear (); ctls.Clear ();
			var bottomRow = new UniformGrid { Rows = 1 }; int nb = 0;
			foreach (var f in items)
			{
				var c = Ctl.Make (f, this);
				ctls.Add (c);
				if (c.El == null) continue;
				if (f[0] == "H") head.Children.Add (c.El);
				else if (c.Bottom) { bottomRow.Children.Add (c.El); nb++; }
				else body.Children.Add (c.El);
				c.Update (f);
			}
			if (nb > 0) foot.Children.Add (bottomRow);
			sv.ScrollToTop ();
		}
	}

	// A control of the panel, made from an item.
	abstract class Ctl
	{
		public FrameworkElement El; public bool Bottom;
		protected FormPanel P;
		public abstract void Update (string[] f);

		protected static Brush Res (string k) => (Brush) Application.Current.FindResource (k);
		protected static Thickness Row => new Thickness (14, 3, 14, 3);
		protected static TextBlock Label (string s, double size = 12.5, Brush b = null, bool bold = false)
			=> new TextBlock { Text = s, FontSize = size, Foreground = b ?? Res ("Ink"), FontWeight = bold ? FontWeights.SemiBold : FontWeights.Normal, VerticalAlignment = VerticalAlignment.Center, TextWrapping = TextWrapping.Wrap };

		// What makes a layout (the rest -- the values -- is put again in place)
		public static string Signature (string[] f)
		{
			switch (f[0])
			{
			case "F": return "F" + f[1] + "|" + f[2] + "|" + f[4];
			case "T": return "T" + f[1] + "|" + f[2];
			case "C": return "C" + f[1] + "|" + f[2];
			case "S": case "D": return f[0] + f[1] + "|" + f[2] + "|" + string.Join ("|", f.Skip (4));
			case "B": return string.Join ("|", f);
			case "L": return "L" + f[2] + f[3] + f[4];
			case "H": return "H" + f[1];
			default: return f[0];
			}
		}
		public static Ctl Make (string[] f, FormPanel p)
		{
			Ctl c;
			switch (f[0])
			{
			case "H": c = new HeadCtl (); break;
			case "L": c = new LabelCtl (f); break;
			case "F": c = new FieldCtl (f, p); break;
			case "T": c = new NameCtl (f, p); break;
			case "C": c = new CheckCtl (f, p); break;
			case "S": c = new SegCtl (f, p); break;
			case "D": c = new DropCtl (f, p); break;
			case "B": c = new ButtonCtl (f, p); break;
			case "P": c = new ProcessCtl (p); break;
			case "O": c = new OpCtl (p); break;
			case "W": c = new SwatchCtl (p); break;
			case "R": c = new TargetCtl (); break;
			case "K": c = new KvCtl (); break;
			case "I": c = new InfoCtl (); break;
			case "X": c = new MarkCtl (); break;
			case "Y": c = new SummaryCtl (); break;
			case "G": c = new OriginCtl (p); break;
			default: c = new LabelCtl (new[] { "L", "", "", "0", "0" }); break;
			}
			c.P = p; return c;
		}
		protected static string F (string[] f, int i) => i < f.Length ? f[i] : "";
	}

	class HeadCtl : Ctl
	{
		readonly Grid g = new Grid { Margin = new Thickness (14, 12, 14, 8) };
		readonly ContentControl icon = new ContentControl { Width = 28, Height = 28, Margin = new Thickness (0, 0, 10, 0), VerticalAlignment = VerticalAlignment.Top, Focusable = false };
		readonly TextBlock title = Label ("", 16, null, true), sub = new TextBlock { FontSize = 11.5, Margin = new Thickness (0, 1, 0, 0) };
		public HeadCtl ()
		{
			g.ColumnDefinitions.Add (new ColumnDefinition { Width = GridLength.Auto }); g.ColumnDefinitions.Add (new ColumnDefinition ());
			var st = new StackPanel (); st.Children.Add (title); st.Children.Add (sub); Grid.SetColumn (st, 1);
			title.TextTrimming = TextTrimming.CharacterEllipsis; title.TextWrapping = TextWrapping.NoWrap;
			g.Children.Add (icon); g.Children.Add (st);
			var sep = new Border { Height = 1, Background = Res ("Line"), Margin = new Thickness (12, 0, 12, 0) };
			var outer = new StackPanel (); outer.Children.Add (g); outer.Children.Add (sep);
			El = outer;
		}
		public override void Update (string[] f)
		{
			int ic = int.Parse (F (f, 1)); title.Text = F (f, 2);
			if (ic >= 0) { icon.Content = Icons.Image (ic, 26); icon.Visibility = Visibility.Visible; }
			else if (ic == -2) { icon.Content = new Border { Width = 22, Height = 22, CornerRadius = new CornerRadius (4), Background = Icons.Brush (F (f, 6), Brushes.Gray), BorderBrush = new SolidColorBrush (Color.FromArgb (90, 0, 0, 0)), BorderThickness = new Thickness (1), Margin = new Thickness (2, 3, 0, 0) }; icon.Visibility = Visibility.Visible; }
			else icon.Visibility = Visibility.Collapsed;
			// the steps of a gesture: done (green, a check), at (the accent), to come (grey)
			sub.Inlines.Clear (); int at = int.Parse (F (f, 5));
			for (int i = 0; i < 2; i++)
			{
				string s = F (f, 3 + i); if (s.Length == 0) continue;
				bool done = at > i; var col = at < 0 ? Res ("Dim") : at == i ? Res ("Accent") : done ? new SolidColorBrush (Color.FromRgb (0x3C, 0x96, 0x50)) : new SolidColorBrush (Color.FromRgb (0xA8, 0xAC, 0xB4));
				if (sub.Inlines.Count > 0) sub.Inlines.Add (new System.Windows.Documents.Run ("    "));
				sub.Inlines.Add (new System.Windows.Documents.Run ((done ? "✓ " : "") + s) { Foreground = col, FontWeight = at == i ? FontWeights.SemiBold : FontWeights.Normal });
			}
		}
	}
	class LabelCtl : Ctl
	{
		readonly TextBlock t;
		public LabelCtl (string[] f)
		{
			bool bold = F (f, 3) == "1", small = F (f, 4) == "1";
			t = Label ("", small ? 11.5 : 12.5, null, bold);
			t.Margin = bold ? new Thickness (14, 12, 14, 2) : Row;
			El = t;
		}
		public override void Update (string[] f) { t.Text = F (f, 1); t.Foreground = Icons.Brush (F (f, 2), Res (F (f, 4) == "1" ? "Dim" : "Ink")); }
	}
	class FieldCtl : Ctl
	{
		readonly TextBox tb; readonly TextBlock name, unit; readonly int id; bool focused;
		public FieldCtl (string[] f, FormPanel p)
		{
			id = int.Parse (f[1]); P = p;
			var g = new Grid { Margin = Row };
			g.ColumnDefinitions.Add (new ColumnDefinition ()); g.ColumnDefinitions.Add (new ColumnDefinition { Width = new GridLength (76) }); g.ColumnDefinitions.Add (new ColumnDefinition { Width = new GridLength (32) });
			name = Label (f[2]); name.TextWrapping = TextWrapping.NoWrap; name.TextTrimming = TextTrimming.CharacterEllipsis;
			tb = new TextBox { Style = (Style) Application.Current.FindResource ("Field"), HorizontalContentAlignment = HorizontalAlignment.Right };
			unit = Label (F (f, 4), 11.5, Res ("Dim")); unit.Margin = new Thickness (5, 0, 0, 0);
			Grid.SetColumn (tb, 1); Grid.SetColumn (unit, 2);
			g.Children.Add (name); g.Children.Add (tb); g.Children.Add (unit);
			tb.TextChanged += (s, e) => { if (FormPanel.Updating) return; Core.FormText (id, tb.Text); P.Fire (); };
			tb.GotKeyboardFocus += (s, e) => tb.SelectAll ();
			tb.PreviewMouseLeftButtonDown += (s, e) => { if (!tb.IsKeyboardFocusWithin) { tb.Focus (); e.Handled = true; } };
			tb.PreviewKeyDown += (s, e) =>
			{
				if (e.Key == Key.Enter) { int r = Core.fc_form_enter (); P.Fire (); if (r != Core.REQ_NONE) P.Run (-r); e.Handled = true; }
			};
			El = g;
		}
		static double Num (string s)
		{
			double.TryParse ((s ?? "").Replace (',', '.').Trim (), NumberStyles.Float, CultureInfo.InvariantCulture, out double v); return v;
		}
		public override void Update (string[] f)
		{
			string v = F (f, 3);
			if (tb.Text != v && !(tb.IsKeyboardFocused && Math.Abs (Num (tb.Text) - Num (v)) < 1e-9)) { tb.Text = v; tb.CaretIndex = v.Length; }
			bool want = F (f, 5) == "1";
			if (want && !focused) tb.Dispatcher.BeginInvoke (new Action (() => { if (tb.IsVisible) { tb.Focus (); Keyboard.Focus (tb); tb.SelectAll (); } }), System.Windows.Threading.DispatcherPriority.Input);
			focused = want;
		}
	}
	class NameCtl : Ctl
	{
		readonly TextBox tb; readonly int id;
		public NameCtl (string[] f, FormPanel p)
		{
			id = int.Parse (f[1]); P = p;
			var g = new Grid { Margin = Row };
			g.ColumnDefinitions.Add (new ColumnDefinition { Width = GridLength.Auto }); g.ColumnDefinitions.Add (new ColumnDefinition ());
			tb = new TextBox { Style = (Style) Application.Current.FindResource ("Field"), MaxLength = 22, Margin = new Thickness (12, 0, 0, 0) };
			Grid.SetColumn (tb, 1); g.Children.Add (Label (f[2])); g.Children.Add (tb);
			tb.TextChanged += (s, e) => { if (FormPanel.Updating) return; Core.FormText (id, tb.Text); P.Fire (); };
			El = g;
		}
		public override void Update (string[] f) { if (!tb.IsKeyboardFocused && tb.Text != F (f, 3)) tb.Text = F (f, 3); }
	}
	class CheckCtl : Ctl
	{
		readonly CheckBox cb; readonly int tag;
		public CheckCtl (string[] f, FormPanel p)
		{
			tag = int.Parse (f[1]); P = p;
			cb = new CheckBox { Content = f[2], Margin = new Thickness (14, 5, 14, 5), Focusable = false, VerticalContentAlignment = VerticalAlignment.Center };
			cb.Click += (s, e) => { Core.fc_form_check (tag, cb.IsChecked == true ? 1 : 0); P.Fire (); };
			El = cb;
		}
		public override void Update (string[] f) { cb.IsChecked = F (f, 3) == "1"; }
	}
	class SegCtl : Ctl
	{
		readonly List<RadioButton> rb = new List<RadioButton> (); readonly int tag;
		static int group;
		public SegCtl (string[] f, FormPanel p)
		{
			tag = int.Parse (f[1]); P = p; string name = f[2], g = "seg" + (group++);
			var ug = new UniformGrid { Rows = 1 };
			for (int i = 4; i < f.Length; i++)
			{
				int k = i - 4;
				var r = new RadioButton { Content = f[i], GroupName = g, Style = (Style) Application.Current.FindResource ("Seg"), FontSize = 12 };
				r.Checked += (s, e) => { if (FormPanel.Updating) return; Core.fc_form_seg (tag, k); P.Fire (); };
				rb.Add (r); ug.Children.Add (r);
			}
			var box = new Border { Child = ug, BorderBrush = Res ("FieldLine"), BorderThickness = new Thickness (1), CornerRadius = new CornerRadius (4), Background = Brushes.White };
			if (name.Length == 0) { box.Margin = new Thickness (14, 4, 14, 4); El = box; return; }
			var gr = new Grid { Margin = Row };
			gr.ColumnDefinitions.Add (new ColumnDefinition ()); gr.ColumnDefinitions.Add (new ColumnDefinition { Width = new GridLength (Math.Max (130, 64 * rb.Count)) });
			Grid.SetColumn (box, 1); gr.Children.Add (Label (name)); gr.Children.Add (box);
			El = gr;
		}
		public override void Update (string[] f) { int s = int.Parse (F (f, 3)); for (int i = 0; i < rb.Count; i++) rb[i].IsChecked = i == s; }
	}
	class DropCtl : Ctl
	{
		readonly ComboBox cb; readonly int tag;
		public DropCtl (string[] f, FormPanel p)
		{
			tag = int.Parse (f[1]); P = p;
			cb = new ComboBox { Focusable = false, Height = 25, VerticalContentAlignment = VerticalAlignment.Center };
			for (int i = 4; i < f.Length; i++) cb.Items.Add (f[i]);
			cb.SelectionChanged += (s, e) => { if (FormPanel.Updating || cb.SelectedIndex < 0) return; Core.fc_form_drop (tag, cb.SelectedIndex); P.Fire (); };
			var gr = new Grid { Margin = Row };
			gr.ColumnDefinitions.Add (new ColumnDefinition ()); gr.ColumnDefinitions.Add (new ColumnDefinition { Width = new GridLength (124) });
			Grid.SetColumn (cb, 1); gr.Children.Add (Label (f[2])); gr.Children.Add (cb);
			El = gr;
		}
		public override void Update (string[] f) { cb.SelectedIndex = int.Parse (F (f, 3)); }
	}
	class ButtonCtl : Ctl
	{
		readonly Button b; readonly int cmd;
		public ButtonCtl (string[] f, FormPanel p)
		{
			cmd = int.Parse (f[1]); P = p; Bottom = F (f, 4) == "1";
			b = new Button { Content = f[2], Style = (Style) Application.Current.FindResource (F (f, 3) == "1" ? "AccentButton" : "Plain"), Margin = Bottom ? new Thickness (2, 0, 2, 0) : new Thickness (14, 6, 14, 6) };
			b.Click += (s, e) => P.Run (cmd);
			El = b;
		}
		public override void Update (string[] f) { b.Content = F (f, 2); }
	}
	// The body cut or printed, and the process: two buttons that open their lists.
	class ProcessCtl : Ctl
	{
		readonly Button body, proc;
		public ProcessCtl (FormPanel p)
		{
			P = p;
			var g = new Grid { Margin = new Thickness (14, 4, 14, 6) };
			g.ColumnDefinitions.Add (new ColumnDefinition ()); g.ColumnDefinitions.Add (new ColumnDefinition { Width = new GridLength (8) }); g.ColumnDefinitions.Add (new ColumnDefinition ());
			g.RowDefinitions.Add (new RowDefinition { Height = GridLength.Auto }); g.RowDefinitions.Add (new RowDefinition { Height = GridLength.Auto });
			var l1 = Label ("Body", 12.5, null, true); var l2 = Label ("Process", 12.5, null, true); Grid.SetColumn (l2, 2);
			body = new Button { Style = (Style) Application.Current.FindResource ("Plain"), Margin = new Thickness (0, 4, 0, 0), HorizontalContentAlignment = HorizontalAlignment.Left };
			proc = new Button { Style = (Style) Application.Current.FindResource ("Plain"), Margin = new Thickness (0, 4, 0, 0) };
			Grid.SetRow (body, 1); Grid.SetRow (proc, 1); Grid.SetColumn (proc, 2);
			g.Children.Add (l1); g.Children.Add (l2); g.Children.Add (body); g.Children.Add (proc);
			body.Click += (s, e) => Menu (body, 0); proc.Click += (s, e) => Menu (proc, 1);
			El = g;
		}
		void Menu (Button at, int which)
		{
			var m = new ContextMenu ();
			var list = Core.List (which);
			for (int i = 0; i < list.Length; i++) { int k = i; var mi = new MenuItem { Header = list[i] }; mi.Click += (s, e) => { Core.fc_pick (which, k); P.Fire (); }; m.Items.Add (mi); }
			m.PlacementTarget = at; m.Placement = PlacementMode.Bottom; m.IsOpen = true;
		}
		public override void Update (string[] f) { body.Content = new TextBlock { Text = F (f, 1), TextTrimming = TextTrimming.CharacterEllipsis }; proc.Content = F (f, 2) + "  ▾"; }
	}
	// The operation: one of four (a new body, or joined to, cut from, kept inside another).
	class OpCtl : Ctl
	{
		static readonly string[] NAMES = { "New body", "Union", "Subtract", "Intersect" };
		static readonly int[] ICONS = { I.NEWBODY, I.UNION, I.SUB, I.INT };
		readonly Border[] rows = new Border[4]; readonly TextBlock[] texts = new TextBlock[4]; readonly ContentControl[] ics = new ContentControl[4];
		public OpCtl (FormPanel p)
		{
			P = p;
			var st = new StackPanel ();
			var box = new Border { Child = st, BorderBrush = Res ("FieldLine"), BorderThickness = new Thickness (1), CornerRadius = new CornerRadius (4), Background = Brushes.White, Padding = new Thickness (2) };
			for (int i = 0; i < 4; i++)
			{
				int k = i;
				var row = new StackPanel { Orientation = Orientation.Horizontal };
				ics[i] = new ContentControl { Width = 20, Height = 20, Margin = new Thickness (6, 0, 10, 0), Focusable = false };
				texts[i] = Label (NAMES[i]);
				row.Children.Add (ics[i]); row.Children.Add (texts[i]);
				rows[i] = new Border { Child = row, Height = 28, CornerRadius = new CornerRadius (3), Background = Brushes.Transparent, Cursor = Cursors.Hand };
				rows[i].MouseLeftButtonUp += (s, e) => { Core.fc_form_op (k); P.Fire (); };
				st.Children.Add (rows[i]);
			}
			var outer = new StackPanel { Margin = new Thickness (14, 10, 14, 4) };
			outer.Children.Add (Label ("Operation", 12.5, null, true)); outer.Children.Add (new Border { Height = 4 }); outer.Children.Add (box);
			El = outer;
		}
		public override void Update (string[] f)
		{
			int sel = int.Parse (F (f, 1));
			for (int i = 0; i < 4; i++)
			{
				bool on = i == sel, can = F (f, 2 + i) == "1";
				rows[i].Background = on ? Res ("Accent") : Brushes.Transparent;
				texts[i].Foreground = on ? Brushes.White : can ? Res ("Ink") : new SolidColorBrush (Color.FromRgb (0xA8, 0xAC, 0xB4));
				texts[i].FontWeight = on ? FontWeights.SemiBold : FontWeights.Normal;
				ics[i].Content = Icons.Image (ICONS[i], 20, on ? 0xFFFFFFu : can ? Icons.INK : 0xB4B8C0u, on ? 0xFFFFFFu : Icons.ACC);
				rows[i].IsEnabled = can;
			}
		}
	}
	class SwatchCtl : Ctl
	{
		readonly Border[] sw = new Border[5]; string[] cols = new string[0];
		public SwatchCtl (FormPanel p)
		{
			P = p;
			var g = new Grid { Margin = new Thickness (14, 6, 14, 6) };
			g.ColumnDefinitions.Add (new ColumnDefinition ()); g.ColumnDefinitions.Add (new ColumnDefinition { Width = GridLength.Auto });
			var st = new StackPanel { Orientation = Orientation.Horizontal }; Grid.SetColumn (st, 1);
			for (int i = 0; i < 5; i++)
			{
				int k = i;
				sw[i] = new Border { Width = 22, Height = 22, CornerRadius = new CornerRadius (5), Margin = new Thickness (3, 0, 0, 0), BorderThickness = new Thickness (2), Cursor = Cursors.Hand };
				sw[i].MouseLeftButtonUp += (s, e) => { Core.fc_form_colour (k); P.Fire (); };
				st.Children.Add (sw[i]);
			}
			g.Children.Add (Label ("Colour")); g.Children.Add (st);
			El = g;
		}
		public override void Update (string[] f)
		{
			for (int i = 0; i < 5; i++)
			{
				string c = F (f, 2 + i);
				sw[i].Background = Icons.Brush (c, Brushes.Gray);
				sw[i].BorderBrush = c == F (f, 1) ? Res ("Accent") : new SolidColorBrush (Color.FromArgb (60, 0, 0, 0));
			}
		}
	}
	class TargetCtl : Ctl
	{
		readonly TextBlock k = Label ("", 12.5, Res ("Dim")), v = Label (""); readonly Border sw = new Border { Width = 14, Height = 14, CornerRadius = new CornerRadius (3), Margin = new Thickness (0, 0, 6, 0) };
		public TargetCtl ()
		{
			var g = new Grid { Margin = Row };
			g.ColumnDefinitions.Add (new ColumnDefinition ()); g.ColumnDefinitions.Add (new ColumnDefinition { Width = GridLength.Auto });
			var st = new StackPanel { Orientation = Orientation.Horizontal }; st.Children.Add (sw); st.Children.Add (v); Grid.SetColumn (st, 1);
			g.Children.Add (k); g.Children.Add (st); El = g;
		}
		public override void Update (string[] f) { k.Text = F (f, 1); v.Text = F (f, 2); sw.Background = Icons.Brush (F (f, 3), Brushes.Gray); }
	}
	class KvCtl : Ctl
	{
		readonly TextBlock k = Label (""), v = Label ("");
		public KvCtl ()
		{
			var g = new Grid { Margin = new Thickness (14, 2, 14, 2) };
			g.ColumnDefinitions.Add (new ColumnDefinition { Width = GridLength.Auto }); g.ColumnDefinitions.Add (new ColumnDefinition ());
			v.HorizontalAlignment = HorizontalAlignment.Right; v.TextAlignment = TextAlignment.Right; v.Margin = new Thickness (10, 0, 0, 0); Grid.SetColumn (v, 1);
			g.Children.Add (k); g.Children.Add (v); El = g;
		}
		public override void Update (string[] f) { k.Text = F (f, 1); v.Text = F (f, 2); k.Foreground = Icons.Brush (F (f, 3), Res ("Ink")); }
	}
	class InfoCtl : Ctl
	{
		readonly TextBlock t = new TextBlock { FontSize = 11.5, TextWrapping = TextWrapping.Wrap };
		public InfoCtl ()
		{
			t.Foreground = Res ("Dim");
			El = new Border { Child = t, Background = new SolidColorBrush (Color.FromRgb (0xF1, 0xF3, 0xF7)), BorderBrush = Res ("Line"), BorderThickness = new Thickness (1), CornerRadius = new CornerRadius (6), Padding = new Thickness (10, 7, 10, 7), Margin = new Thickness (14, 8, 14, 4) };
		}
		public override void Update (string[] f) { t.Text = string.Join (" ", f.Skip (1)); }
	}
	class MarkCtl : Ctl
	{
		readonly ContentControl ic = new ContentControl { Width = 18, Height = 18, Margin = new Thickness (0, 0, 8, 0), Focusable = false }; readonly TextBlock t = Label ("");
		public MarkCtl ()
		{
			var st = new StackPanel { Orientation = Orientation.Horizontal, Margin = Row }; st.Children.Add (ic); st.Children.Add (t); El = st;
		}
		public override void Update (string[] f)
		{
			bool ok = F (f, 1) == "0";
			ic.Content = Icons.Image (ok ? I.CHECK : I.WARN, 16, ok ? 0x3C9650u : 0x4A3A10u, ok ? 0x3C9650u : Icons.ACC);
			t.Text = F (f, 2); t.Foreground = Icons.Brush (F (f, 3), Res ("Ink"));
		}
	}
	class SummaryCtl : Ctl
	{
		readonly ContentControl ic = new ContentControl { Width = 20, Height = 20, Margin = new Thickness (0, 0, 10, 0), VerticalAlignment = VerticalAlignment.Center, Focusable = false };
		readonly TextBlock a = Label ("", 12), b = Label ("", 11.5);
		public SummaryCtl ()
		{
			var st = new StackPanel { VerticalAlignment = VerticalAlignment.Center }; st.Children.Add (a); st.Children.Add (b);
			var row = new StackPanel { Orientation = Orientation.Horizontal }; row.Children.Add (ic); row.Children.Add (st);
			El = new Border { Child = row, Background = Brushes.White, BorderBrush = Res ("Line"), BorderThickness = new Thickness (1), CornerRadius = new CornerRadius (6), Padding = new Thickness (10, 7, 10, 7), Margin = new Thickness (14, 10, 14, 4) };
		}
		public override void Update (string[] f)
		{
			bool ok = F (f, 1) == "0";
			ic.Content = Icons.Image (ok ? I.CHECK : I.WARN, 18, ok ? 0x3C9650u : 0x4A3A10u, ok ? 0x3C9650u : Icons.ACC);
			a.Text = F (f, 2); a.Foreground = Icons.Brush (F (f, 3), Res ("Ink"));
			b.Text = F (f, 4); b.Foreground = Icons.Brush (F (f, 5), Res ("Dim")); b.Visibility = b.Text.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
		}
	}
	// The origin among the stock's 27 points: three small grids, its top, its middle, its bottom.
	class OriginCtl : Ctl
	{
		readonly Ellipse[] dots = new Ellipse[27];
		public OriginCtl (FormPanel p)
		{
			P = p;
			var st = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness (14, 2, 14, 4) };
			string[] LN = { "Top", "Middle", "Bottom" };
			for (int li = 0; li < 3; li++)
			{
				var col = new StackPanel { Margin = new Thickness (0, 0, 10, 0) };
				col.Children.Add (new TextBlock { Text = LN[li], FontSize = 11, Foreground = Res ("Dim"), HorizontalAlignment = HorizontalAlignment.Center });
				var cv = new Canvas { Width = 58, Height = 46 };
				var bg = new Border { Width = 58, Height = 46, Background = Brushes.White, BorderBrush = Res ("FieldLine"), BorderThickness = new Thickness (1), CornerRadius = new CornerRadius (4) };
				cv.Children.Add (bg);
				for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++)
				{
					int k = i + 3 * (2 - j) + 9 * (2 - li);
					var e = new Ellipse { Width = 9, Height = 9, StrokeThickness = 1.2, Cursor = Cursors.Hand };
					e.Tag = new Point (11 + i * 18, 9 + j * 14);
					e.MouseLeftButtonUp += (s, ev) => { Core.fc_form_origin (k); P.Fire (); };
					dots[k] = e; cv.Children.Add (e);
				}
				col.Children.Add (cv); st.Children.Add (col);
			}
			El = st;
		}
		public override void Update (string[] f)
		{
			int sel = int.Parse (F (f, 1));
			for (int k = 0; k < 27; k++)
			{
				bool on = k == sel; var e = dots[k];
				e.Fill = on ? Res ("Accent") : new SolidColorBrush (Color.FromRgb (0xD6, 0xD9, 0xDF));
				e.Stroke = on ? Res ("AccentHot") : new SolidColorBrush (Color.FromRgb (0xA9, 0xAE, 0xB8));
				e.Width = e.Height = on ? 11 : 8;
				var c = (Point) e.Tag; Canvas.SetLeft (e, c.X - e.Width / 2); Canvas.SetTop (e, c.Y - e.Height / 2);
			}
		}
	}
}
