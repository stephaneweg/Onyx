// Program.cs -- PocketTerm Simulator: the Waveshare PocketTerm35 on a PC, to try Onyx's pocket interface
// before the device is there.
//   PocketTermSim.exe [address[:port]]      the window; with an address, connected at once (rdpd, 3390)
// Options: --scale 1|1.5|2|true|fit   --layout device|beside   --mapping FILE (the buttons' file; by
// default PocketTermSim.buttons.ini beside the program, written when missing)
// For the tests (pc/PocketTermSim/test/run_test.py), none of them keeps a setting:
//   --shot OUT.png [--demo PICTURE.png] [--lit Id,Id] [--fn] [--room WxH]   the face drawn into a file
//   --winshot OUT.png [--demo PICTURE.png | ADDRESS:PORT]   the real window shown, captured, closed
//   --sends OUT.txt                            the table of what each control sends
//   --write-mapping FILE                       the default mapping file
//   --selftest ADDRESS:PORT --out FOLDER       connects, presses every control (a RAISE with a number
//                                              before each, as a mark in the server's log), then leaves
// MIT licence (Onyx).
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace PocketTermSim
{
	static class Program
	{
		[STAThread]
		static int Main (string[] args)
		{
			Application.EnableVisualStyles ();
			Application.SetCompatibleTextRenderingDefault (false);
			string address = null, shot = null, winshot = null, demo = null, sends = null, selftest = null, outDir = null, lit = null, room = null;
			string mapping = Path.Combine (AppDomain.CurrentDomain.BaseDirectory, "PocketTermSim.buttons.ini");
			int scale = -1, layout = -1; bool fn = false;
			for (int i = 0; i < args.Length; i++)
			{
				string a = args[i];
				Func<string> next = () => i + 1 < args.Length ? args[++i] : "";
				if (a == "--scale") { string v = next (); scale = v == "1.5" ? 1 : v == "2" ? 2 : v.StartsWith ("t") ? 3 : v.StartsWith ("f") ? 4 : 0; }
				else if (a == "--layout") { string v = next (); layout = v == "beside" ? 1 : 0; }
				else if (a == "--mapping") mapping = Path.GetFullPath (next ());
				else if (a == "--shot") shot = next ();
				else if (a == "--winshot") winshot = next ();
				else if (a == "--demo") demo = next ();
				else if (a == "--sends") sends = next ();
				else if (a == "--selftest") selftest = next ();
				else if (a == "--out") outDir = next ();
				else if (a == "--lit") lit = next ();
				else if (a == "--room") room = next ();
				else if (a == "--fn") fn = true;
				else if (a == "--write-mapping") { File.WriteAllText (next (), Mapping.Default.Replace ("\r\n", "\n").Replace ("\n", "\r\n"), new UTF8Encoding (false)); return 0; }
				else if (!a.StartsWith ("--")) address = a;
			}
			bool test = shot != null || winshot != null || sends != null || selftest != null;
			Settings.ReadOnly = test;
			var f = new SimForm (mapping);
			if (test) { f.Set.Scale = 0; f.Set.Layout = 0; f.Set.Smooth = false; f.Set.Top = false; f.Set.Mouse = true; f.Quiet = true; }
			if (room != null) { string[] p = room.Split ('x'); f.AvailOverride = new Size (int.Parse (p[0]), int.Parse (p[1])); }
			if (scale >= 0) f.Set.Scale = scale;
			if (layout >= 0) f.Set.Layout = layout;
			f.Relayout (true);
			if (demo != null) using (var src = new Bitmap (demo)) f.Face.Remote = new Bitmap (src);
			if (sends != null) { File.WriteAllText (sends, f.SendsTable ()); return 0; }
			if (shot != null)
			{
				if (fn) { f.Dev.Press ("key:FnL"); f.Dev.Release ("key:FnL"); f.Dev.Press ("key:FnL"); f.Dev.Release ("key:FnL"); }
				if (lit != null) foreach (string id in lit.Split (',')) f.Dev.Press (id);
				using (var b = f.DeviceBitmap ()) b.Save (shot, ImageFormat.Png);
				return 0;
			}
			if (winshot != null)
			{
				f.Shown += async (s, e) =>
				{
					if (address != null)			// (connected first: the banner, the status line)
					{
						int c = address.LastIndexOf (':');
						f.Connect (address.Substring (0, c), int.Parse (address.Substring (c + 1)));
						for (int i = 0; i < 60 && f.RemotePicture == null; i++) await Task.Delay (100);
						await Task.Delay (1300);
					}
					await Task.Delay (700);
					using (var b = new Bitmap (f.Width, f.Height)) { f.DrawToBitmap (b, new Rectangle (0, 0, f.Width, f.Height)); b.Save (winshot, ImageFormat.Png); }
					f.Close ();
				};
				Application.Run (f);
				return 0;
			}
			if (selftest != null)
			{
				int rc = 1;
				f.Shown += async (s, e) => { try { rc = await SelfTest (f, selftest, outDir ?? "."); } catch (Exception x) { File.WriteAllText (Path.Combine (outDir ?? ".", "selftest-error.txt"), x.ToString ()); } f.Close (); };
				Application.Run (f);
				return rc;
			}
			f.Shown += (s, e) =>
			{
				if (address != null)
				{
					int port = 3390, colon = address.LastIndexOf (':');
					if (colon > 0 && int.TryParse (address.Substring (colon + 1), out port)) address = address.Substring (0, colon); else port = 3390;
					f.Connect (address, port);
				}
				else f.AskConnect ();
			};
			Application.Run (f);
			return 0;
		}

		// Every control pressed against a server that logs what it receives. Before each step a RAISE with
		// a number: the server never needs it, the log is cut at it.
		static async Task<int> SelfTest (SimForm f, string address, string outDir)
		{
			int colon = address.LastIndexOf (':');
			f.Connect (address.Substring (0, colon), int.Parse (address.Substring (colon + 1)));
			for (int i = 0; i < 100 && f.RemotePicture == null; i++) await Task.Delay (100);
			if (f.RemotePicture == null) return 2;
			f.Set.Mouse = false;			// (the real mouse may be over the window: not in the log)
			await Task.Delay (300);
			f.RemotePicture.Save (Path.Combine (outDir, "remote.png"), ImageFormat.Png);
			using (var b = f.DeviceBitmap ()) b.Save (Path.Combine (outDir, "device-connected.png"), ImageFormat.Png);
			FaceView v = f.Face;
			Func<string, Point> centre = id => { var c = v.L.Find (id); return new Point ((int) (c.R.X + c.R.Width / 2), (int) (c.R.Y + c.R.Height / 2)); };
			Func<string, Task> click = async id => { v.DoDown (centre (id)); await Task.Delay (12); v.DoUp (); await Task.Delay (12); };
			Action<int> mark = n => f.Conn.Raise ((uint) n);

			// 100..: every control once, in the order of the face (a modifier three times: armed, locked, off;
			// Caps twice)
			var order = new List<string> (Mapping.PadButtons);
			foreach (var row in f.Dev.Rows) foreach (var k in row) order.Add (k.Id);
			var names = new StringBuilder ();
			for (int i = 0; i < order.Count; i++)
			{
				mark (100 + i); names.AppendLine ((100 + i) + " " + order[i]);
				KeyDef d;
				int n = f.Dev.Keys.TryGetValue (order[i], out d) && d.Mod != Mod.None ? (d.Mod == Mod.Caps ? 2 : 3) : 1;
				for (int j = 0; j < n; j++) await click (order[i]);
			}
			File.WriteAllText (Path.Combine (outDir, "order.txt"), names.ToString ());
			// 300..: the Fn layer, Fn locked
			await click ("key:FnL"); await click ("key:FnL");
			int m = 300;
			foreach (var row in f.Dev.Rows) foreach (var k in row) if (k.Mod == Mod.None) { mark (m++); await click (k.Id); }
			await click ("key:FnR");						// (the other Fn key: its own state)
			await click ("key:FnR"); await click ("key:FnR");
			await click ("key:FnL");						// off
			// 500..: Shift locked, every character key
			mark (500);
			await click ("key:ShiftL"); await click ("key:ShiftL");
			m = 501;
			foreach (var row in f.Dev.Rows) foreach (var k in row) if (k.Mod == Mod.None) { mark (m++); await click (k.Id); }
			mark (599); await click ("key:ShiftL");
			// 1000..: chords
			mark (1000); await click ("key:Ctrl"); await click ("key:Right");		// Ctrl + Right, Ctrl armed
			mark (1001); await click ("key:AltL"); await click ("key:Tab");			// Alt + Tab
			mark (1002); await click ("key:FnL"); await click ("key:1"); await click ("key:1");	// Fn armed: F1, then 1
			mark (1003); await click ("key:ShiftR"); await click ("key:a"); await click ("key:a");	// Shift armed: A, then a
			mark (1004); await click ("key:Caps"); await click ("key:a"); await click ("key:1"); await click ("key:ShiftL"); await click ("key:a"); await click ("key:Caps");
			mark (1005); await click ("key:Ctrl"); await click ("key:Ctrl"); await click ("key:c"); await click ("key:v"); await click ("key:Ctrl");	// Ctrl locked
			mark (1006); await click ("key:CMD"); await click ("key:CMD"); await click ("key:CMD");	// CMD tapped alone
			mark (1007); await click ("key:Ctrl"); await click ("Up");				// Ctrl + the D-pad
			mark (1008); await click ("key:Ctrl"); await click ("key:AltL"); await click ("key:FnL"); await click ("key:Backspace");	// Ctrl + Alt + Del
			mark (1009); await click ("key:CMD"); await click ("key:Left");			// CMD + Left
			// 1100..: held with the mouse: it repeats
			mark (1100); v.DoDown (centre ("key:x")); await Task.Delay (750); v.DoUp (); await Task.Delay (20);
			mark (1101); v.DoDown (centre ("Down")); await Task.Delay (750); v.DoUp (); await Task.Delay (20);
			mark (1102); v.DoDown (centre ("L")); await Task.Delay (600); v.DoUp (); await Task.Delay (20);
			// 1200..: the PC's keyboard
			mark (1200); f.PcKeyDown (Keys.F7); f.PcKeyDown (Keys.F7); f.PcKeyUp (Keys.F7);		// L, held: one repeat
			mark (1201); f.PcKeyDown (Keys.F12); f.PcKeyUp (Keys.F12);				// Start
			mark (1202); f.PcKeyDown (Keys.Left); f.PcKeyUp (Keys.Left);
			mark (1203); f.PcKeyDown (Keys.A); f.PcChar ('a'); f.PcKeyUp (Keys.A);
			mark (1204); f.PcChar ('é');
			mark (1205); f.PcKeyDown (Keys.ControlKey); f.PcKeyDown (Keys.C); f.PcChar ((char) 3); f.PcKeyUp (Keys.C); f.PcKeyUp (Keys.ControlKey);
			mark (1206); f.PcKeyDown (Keys.F10); f.PcKeyUp (Keys.F10);
			mark (1207); f.PcKeyDown (Keys.Enter); f.PcChar ('\r'); f.PcKeyUp (Keys.Enter);
			// 1300: the mouse on the screen: the Pi's pointer, in the screen's coordinates
			mark (1300);
			f.Set.Mouse = true;
			var sp = new Point (v.L.Screen.X + (int) (100 * v.L.Scale), v.L.Screen.Y + (int) (50 * v.L.Scale));
			typeof (Control).GetMethod ("OnMouseMove", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke (v, new object[] { new MouseEventArgs (MouseButtons.None, 0, sp.X, sp.Y, 0) });
			await Task.Delay (60);
			mark (9999);
			await Task.Delay (400);
			return 0;
		}
	}
}
