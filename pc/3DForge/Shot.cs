// Shot.cs -- 3DForge.exe --shot out.png: the window drawn off the screen (never shown over what the user is doing),
// a script of steps played as the user would -- a tool chosen, the pointer moved and clicked in the view, a value
// typed --, then its picture saved (the window's inside, at the screen's 96 dpi) and the program ended. The
// documentation's screenshots are made so (pc/3DForge/tools/shots.sh), and the checks.
//
// Steps, separated by ';' (view coordinates: pixels from the view's top left corner):
//   tool:N  cmd:N  key:N (27 Esc, 13 Enter, 46 Del)  tl:I (a step of the timeline)  look:AZ,EL
//   move:X,Y  click:X,Y  rclick:X,Y  drag:X0,Y0,X1,Y1  wheel:X,Y,N  form:ID=TEXT  enter  wait:MS
//   dlg:export / dlg:gcode / dlg:print (the picture is then the dialog's)
// MIT licence (Onyx).
using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace Forge
{
	static class Shot
	{
		public static void Run (MainWindow w, string file, string outPath, string script, string size)
		{
			int W = 1280, H = 800;
			if (size != null) { var p = size.Split ('x'); W = int.Parse (p[0]); H = int.Parse (p[1]); }
			w.WindowStartupLocation = WindowStartupLocation.Manual; w.Left = -32000; w.Top = -32000; w.Width = W; w.Height = H;
			w.ShowActivated = false; w.NoGuard = true; w.ShowInTaskbar = false;
			var steps = script.Split (';').Select (s => s.Trim ()).Where (s => s.Length > 0).ToList ();
			int at = 0;
			var t = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds (60) };
			w.Loaded += (s, e) =>
			{
				if (file != null) w.OpenPath (file, false);
				t.Tick += (s2, e2) =>
				{
					Trace ("tick " + at);
					if (at < steps.Count) { int wait = Step (w, steps[at++]); t.Interval = TimeSpan.FromMilliseconds (Math.Max (60, wait)); w.SyncNow (); return; }
					t.Stop (); w.SyncNow ();
					w.Dispatcher.BeginInvoke (new Action (() => { Trace ("save"); Save (dialog ?? w, outPath); Trace ("saved"); Application.Current.Shutdown (0); }), DispatcherPriority.Background);
				};
				t.Start ();
			};
			w.Show ();
		}
		public static void Trace (string s) { var f = Environment.GetEnvironmentVariable ("FORGE_TRACE"); if (f != null) File.AppendAllText (f, s + Environment.NewLine); }
		static double[] N (string s) => s.Split (',').Select (x => double.Parse (x, CultureInfo.InvariantCulture)).ToArray ();
		static Window dialog;
		static int Step (MainWindow w, string s)
		{
			int c = s.IndexOf (':'); string k = c < 0 ? s : s.Substring (0, c), a = c < 0 ? "" : s.Substring (c + 1);
			switch (k)
			{
			case "tool": Core.fc_tool (int.Parse (a), 0); break;
			case "cmd": Core.fc_cmd (int.Parse (a)); break;
			case "key": Core.fc_key (int.Parse (a)); break;
			case "tl": Core.fc_tl_pick (int.Parse (a), 0, 0); break;
			case "look": { var v = N (a); Core.fc_view_look (v[0], v[1]); break; }
			case "move": { var v = N (a); Core.fc_view_mouse ((int) v[0], (int) v[1], 0, 0, 0); break; }
			case "click": { var v = N (a); int x = (int) v[0], y = (int) v[1]; Core.fc_view_mouse (x, y, 0, 0, 0); Core.fc_view_mouse (x, y, 1, 0, 0); Core.fc_view_mouse (x, y, 0, 0, 0); break; }
			case "rclick": { var v = N (a); int x = (int) v[0], y = (int) v[1]; Core.fc_view_mouse (x, y, 2, 0, 0); Core.fc_view_mouse (x, y, 0, 0, 0); break; }
			case "drag":
			{
				var v = N (a); Core.fc_view_mouse ((int) v[0], (int) v[1], 1, 0, 0);
				for (int i = 1; i <= 12; i++) Core.fc_view_mouse ((int) (v[0] + (v[2] - v[0]) * i / 12), (int) (v[1] + (v[3] - v[1]) * i / 12), 1, 0, 0);
				Core.fc_view_mouse ((int) v[2], (int) v[3], 0, 0, 0); break;
			}
			case "wheel": { var v = N (a); Core.fc_view_mouse ((int) v[0], (int) v[1], 0, (int) v[2], 0); break; }
			case "form": { int e = a.IndexOf ('='); Core.FormText (int.Parse (a.Substring (0, e)), a.Substring (e + 1)); break; }
			case "enter": { int r = Core.fc_form_enter (); break; }
			case "dlg":		// (a dialog, shown off the screen too: the picture is its own)
				dialog = a == "export" ? new ExportDialog (w) : a == "gcode" ? new GcodeDialog (w) : (Window) new PrintFileDialog (w);
				dialog.WindowStartupLocation = WindowStartupLocation.Manual; dialog.Left = -32000; dialog.Top = -32000; dialog.ShowActivated = false; dialog.Show ();
				return 600;
			case "wait": return int.Parse (a);
			}
			return 60;
		}
		static void Save (Window w, string path)
		{
			var el = (FrameworkElement) w.Content;
			int pw = (int) Math.Ceiling (el.ActualWidth), ph = (int) Math.Ceiling (el.ActualHeight);
			var rt = new RenderTargetBitmap (pw, ph, 96, 96, PixelFormats.Pbgra32);
			var dv = new DrawingVisual ();
			using (var dc = dv.RenderOpen ()) dc.DrawRectangle (w.Background, null, new Rect (0, 0, pw, ph));
			rt.Render (dv); rt.Render (el);
			var enc = new PngBitmapEncoder (); enc.Frames.Add (BitmapFrame.Create (rt));
			using (var f = File.Create (path)) enc.Save (f);
		}
	}
}
