// Timeline.cs -- under the view, the history as a row of pictures from the left (fui.h's Timeline): a click shows a
// step at the right (its values can be changed there), a double click on a sketch opens it, the right button rolls the
// part back to a step or deletes it. The blue bar marks where the part is rebuilt up to: dragged, it rolls the part
// back and forth. While a sketch is drawn: its elements; in Manufacture: the setup and its operations. Longer than
// its room, the row moves by its arrows, the wheel, or dragged. At the right: the step pointed or selected.
// MIT licence (Onyx).
using System;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace Forge
{
	class Timeline : FrameworkElement
	{
		const int CELL = 38, ARROW = 22, H = 50;
		int scroll, hot = -1, shownSel = -1, shownN, pressI = -2, pressScroll;
		double pressX; bool moved, barDrag, barHot;
		int n, upto, sel; int[] icons = new int[0], flags = new int[0]; string[] texts = new string[0]; State st;
		public event Action Changed;
		public Func<int, int, ContextMenu> Menu;	// (the item, its flags: the right button's menu)

		public Timeline () { Height = H; ClipToBounds = true; }

		// What the core says now.
		public void Refresh (State s)
		{
			st = s; n = Core.fc_tl_count (); upto = Core.fc_tl_upto ();
			if (icons.Length != n) { icons = new int[n]; flags = new int[n]; texts = new string[n]; }
			sel = -1;
			for (int i = 0; i < n; i++) { flags[i] = Core.TlItem (i, out icons[i], out texts[i]); if ((flags[i] & 1) != 0) sel = i; }
			InvalidateVisual ();
		}
		void Room (out double x0, out double x1, out bool arrows)
		{
			double w = ActualWidth, textW = w > 620 ? 260 : 0; x1 = w - 8 - textW; x0 = 8;
			arrows = n * CELL + 10 > x1 - x0;
			if (arrows) { x0 += ARROW; x1 -= ARROW; }
		}
		int CellAt (double mx)
		{
			Room (out double x0, out double x1, out _);
			if (mx < x0 || mx >= x1) return -1;
			int i = (int) ((mx - x0 + scroll) / CELL); return i < n ? i : -1;
		}
		static Typeface face = new Typeface (new FontFamily ("Segoe UI"), FontStyles.Normal, FontWeights.Normal, FontStretches.Normal);
		static Typeface bold = new Typeface (new FontFamily ("Segoe UI"), FontStyles.Normal, FontWeights.SemiBold, FontStretches.Normal);
		FormattedText Text (string s, double size, Brush b, Typeface f = null, double max = 0)
		{
			var t = new FormattedText (s ?? "", CultureInfo.CurrentUICulture, FlowDirection.LeftToRight, f ?? face, size, b, VisualTreeHelper.GetDpi (this).PixelsPerDip);
			if (max > 0) { t.MaxTextWidth = max; t.MaxLineCount = 1; t.Trimming = TextTrimming.CharacterEllipsis; }
			return t;
		}
		protected override void OnRender (DrawingContext dc)
		{
			double w = ActualWidth, h = ActualHeight;
			var accent = (Brush) FindResource ("Accent"); var dim = (Brush) FindResource ("Dim"); var ink = (Brush) FindResource ("Ink");
			dc.DrawRoundedRectangle (Brushes.White, new Pen ((Brush) FindResource ("Line"), 1), new Rect (0.5, 0.5, w - 1, h - 1), 6, 6);
			Room (out double x0, out double x1, out bool arrows);
			int most = (int) Math.Max (0, n * CELL + 10 - (x1 - x0));
			if (n > shownN) scroll = most;						// (a step added: the end shown)
			else if (sel != shownSel && sel >= 0)					// (one selected: brought into view)
			{
				if (sel * CELL < scroll) scroll = sel * CELL;
				if ((sel + 1) * CELL + 10 > scroll + (x1 - x0)) scroll = (int) ((sel + 1) * CELL + 10 - (x1 - x0));
			}
			shownSel = sel; shownN = n;
			scroll = Math.Max (0, Math.Min (scroll, most));
			dc.PushClip (new RectangleGeometry (new Rect (x0, 0, Math.Max (0, x1 - x0 + 6), h)));
			for (int i = 0; i < n; i++)
			{
				double x = x0 + i * CELL - scroll;
				if (x + CELL < x0 || x > x1 + 6) continue;
				bool on = (flags[i] & 1) != 0, off = (flags[i] & 2) != 0, bad = (flags[i] & 4) != 0;
				var r = new Rect (x + 1, 6, CELL - 4, h - 12);
				if (on) dc.DrawRoundedRectangle ((Brush) FindResource ("AccentSoft"), new Pen (accent, 1), r, 6, 6);
				else if (i == hot) dc.DrawRoundedRectangle ((Brush) FindResource ("Hot"), null, r, 6, 6);
				var ic = Icons.Get (icons[i], 24, off ? 0xB4B8C0u : Icons.INK, off ? 0xA9C2EAu : Icons.ACC);
				dc.DrawImage (ic, new Rect (x + (CELL - 2 - 24) / 2.0, (h - 24) / 2, 24, 24));
				if (bad) dc.DrawImage (Icons.Get (I.WARN, 14, 0x4A3A10), new Rect (x + CELL - 19, h - 20, 14, 14));
			}
			dc.Pop ();
			if (upto >= 0)							// where the part is rebuilt up to: a bar, a grip at its top
			{
				double mx = x0 + upto * CELL - scroll - 2;
				if (mx >= x0 - 2 && mx <= x1)
				{
					double g = barDrag || barHot ? 7 : 5;
					dc.DrawRectangle (accent, null, new Rect (mx - (barDrag ? 1 : 0), 6, barDrag ? 4 : 2, h - 12));
					var tri = new StreamGeometry ();
					using (var c = tri.Open ()) { c.BeginFigure (new Point (mx + 1 - g, 2), true, true); c.LineTo (new Point (mx + 1 + g, 2), true, false); c.LineTo (new Point (mx + 1, 4 + g), true, false); }
					dc.DrawGeometry (accent, null, tri);
				}
			}
			if (arrows)
			{
				dc.DrawText (Text ("", 12, scroll > 0 ? ink : (Brush) FindResource ("Line"), new Typeface ("Segoe MDL2 Assets")), new Point (8 + 4, h / 2 - 8));
				dc.DrawText (Text ("", 12, scroll < most ? ink : (Brush) FindResource ("Line"), new Typeface ("Segoe MDL2 Assets")), new Point (x1 + 6, h / 2 - 8));
			}
			if (n == 0)
				dc.DrawText (Text (st != null && st.Sketching ? "The sketch's elements come here, in the order they are drawn." : "The steps you make come here: click one to change its values.", 12, dim), new Point (14, h / 2 - 9));
			// at the right: the step pointed or selected; a sketch: its outlines
			double tx = (arrows ? x1 + ARROW : x1) + 10, tw = w - 10 - tx;
			if (tw < 80) return;
			dc.DrawRectangle ((Brush) FindResource ("Line"), null, new Rect (tx - 7, 9, 1, h - 18));
			int show = hot >= 0 ? hot : sel;
			if (show >= 0 && show < n)
			{
				string t = texts[show]; int dot = t.IndexOf ("  ·  ", StringComparison.Ordinal);
				string a = dot >= 0 ? t.Substring (0, dot) : t, b = dot >= 0 ? t.Substring (dot + 5) : "";
				dc.DrawText (Text (a, 12.5, ink, bold, tw), new Point (tx, 7));
				dc.DrawText (Text (b, 11.5, dim, null, tw), new Point (tx, 26));
			}
			else if (st != null && st.Sketching)
			{
				var o = Core.Outlines.Split ('\n'); int closed = int.Parse (o[0]), open = o.Length > 1 ? int.Parse (o[1]) : 0;
				if (closed > 0) dc.DrawImage (Icons.Get (I.CHECK, 14, 0x3C9650, 0x3C9650), new Rect (tx, 9, 14, 14));
				dc.DrawText (Text (closed + " closed outline" + (closed == 1 ? "" : "s"), 12, closed > 0 ? new SolidColorBrush (Color.FromRgb (0x28, 0x6E, 0x38)) : dim), new Point (tx + 18, 7));
				if (open > 0) dc.DrawImage (Icons.Get (I.WARN, 14, 0x4A3A10), new Rect (tx, 27, 14, 14));
				dc.DrawText (Text (open > 0 ? open + " still open" : "none open", 12, open > 0 ? new SolidColorBrush (Color.FromRgb (0x8A, 0x5A, 0x10)) : dim), new Point (tx + 18, 26));
			}
			else if (n > 0)
			{
				dc.DrawText (Text ("History", 12.5, ink, bold), new Point (tx, 7));
				dc.DrawText (Text (n + " step" + (n == 1 ? "" : "s"), 11.5, dim), new Point (tx, 26));
			}
		}

		double BarX () { Room (out double x0, out _, out _); return x0 + upto * CELL - scroll - 1; }
		protected override void OnMouseMove (MouseEventArgs e)
		{
			var p = e.GetPosition (this);
			Room (out double x0, out double x1, out bool arrows);
			if (barDrag)
			{
				if (e.LeftButton != MouseButtonState.Pressed) { barDrag = false; ReleaseMouseCapture (); InvalidateVisual (); return; }
				if (p.X < x0 + 8 && scroll > 0) scroll -= CELL / 2; if (p.X > x1 - 8) scroll += CELL / 2;	// (near an end: the row moves)
				int k = (int) Math.Floor ((p.X - x0 + scroll + CELL / 2.0) / CELL); k = Math.Max (0, Math.Min (n, k));
				if (k != upto) { Core.fc_tl_set_upto (k); Changed?.Invoke (); }
				InvalidateVisual (); return;
			}
			bool onBar = upto >= 0 && Math.Abs (p.X - BarX ()) <= 5 && p.X >= x0 - 4 && p.X <= x1 + 4;
			if (onBar != barHot) { barHot = onBar; Cursor = onBar ? Cursors.SizeWE : null; InvalidateVisual (); }
			int h = CellAt (p.X); if (h != hot) { hot = h; InvalidateVisual (); }
			if (e.LeftButton == MouseButtonState.Pressed && pressI >= -1 && Math.Abs (p.X - pressX) > 5) { moved = true; scroll = (int) (pressScroll - (p.X - pressX)); InvalidateVisual (); }
		}
		protected override void OnMouseLeave (MouseEventArgs e) { if (!barDrag && (hot >= 0 || barHot)) { hot = -1; barHot = false; Cursor = null; InvalidateVisual (); } }
		protected override void OnMouseWheel (MouseWheelEventArgs e) { scroll -= Math.Sign (e.Delta) * CELL * 2; InvalidateVisual (); e.Handled = true; }
		protected override void OnMouseLeftButtonDown (MouseButtonEventArgs e)
		{
			var p = e.GetPosition (this);
			Room (out double x0, out double x1, out bool arrows);
			if (barHot) { barDrag = true; CaptureMouse (); pressI = -2; InvalidateVisual (); e.Handled = true; return; }
			if (arrows && p.X < x0) { scroll -= CELL * 3; InvalidateVisual (); return; }
			if (arrows && p.X >= x1 && p.X < x1 + ARROW) { scroll += CELL * 3; InvalidateVisual (); return; }
			pressX = p.X; pressScroll = scroll; pressI = CellAt (p.X); moved = false; CaptureMouse ();
			if (e.ClickCount == 2 && pressI >= 0) { Core.fc_tl_pick (pressI, 1, 0); pressI = -2; Changed?.Invoke (); }
			e.Handled = true;
		}
		protected override void OnMouseLeftButtonUp (MouseButtonEventArgs e)
		{
			ReleaseMouseCapture ();
			if (barDrag) { barDrag = false; InvalidateVisual (); return; }
			int h = CellAt (e.GetPosition (this).X);
			if (!moved && pressI >= 0 && pressI == h) { Core.fc_tl_pick (h, 0, 0); Changed?.Invoke (); }
			pressI = -2;
		}
		protected override void OnMouseRightButtonUp (MouseButtonEventArgs e)
		{
			int h = CellAt (e.GetPosition (this).X);
			if (h < 0) return;
			Core.fc_tl_pick (h, 0, 1); Changed?.Invoke ();
			var m = Menu?.Invoke (h, flags.Length > h ? flags[h] : 0);
			if (m != null) { m.PlacementTarget = this; m.IsOpen = true; }
			e.Handled = true;
		}
	}
}
