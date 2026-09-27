// PadDialog.cs -- Options > Gamepad...: the buttons of a USB pad that is not an Xbox-style one
// (those, XInput, need nothing). Each button of the Onyx pad (named after its PLACE, as on Onyx:
// A = the bottom face button, B = the right one...) gets a button, an axis end or a hat direction of
// the pad: "Set", then press it. The sticks: an axis each. Kept in settings.ini (PadMap, PadAxes).
using System;
using System.Drawing;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace NintendoEMU
{
	static class Pads
	{
		public static readonly string[] Names = { "Up", "Down", "Left", "Right", "A (bottom)", "B (right)", "X (left)", "Y (top)",
			"L", "R", "L2 / ZL", "R2 / ZR", "Select / Back", "Start", "L3 (left stick)", "R3 (right stick)" };
		public static readonly int[] DefaultMap = { 200, 202, 203, 201, 3, 2, 4, 1, 5, 6, 7, 8, 9, 10, 11, 12 };
		public static readonly int[] DefaultAxes = { 0, 1, 2, 3 };
		public static int[] Map = (int[]) DefaultMap.Clone (), Axes = (int[]) DefaultAxes.Clone ();
		static readonly string[] AxisName = { "X", "Y", "Z", "R", "U", "V" };

		[DllImport ("nemucore.dll", CallingConvention = CallingConvention.Cdecl)] static extern void ne_pad_map (int[] map, int n, int[] axes);
		[DllImport ("nemucore.dll", CallingConvention = CallingConvention.Cdecl)] static extern int ne_joy_raw (byte[] name, int cap, out uint buttons, int[] axes, out int pov);

		public static void Load ()
		{
			var m = Parse (Settings.Get ("PadMap", ""), 16); if (m != null) Map = m;
			var a = Parse (Settings.Get ("PadAxes", ""), 4); if (a != null) Axes = a;
			Apply ();
		}
		static int[] Parse (string s, int n)
		{
			var p = s.Split (',');
			if (p.Length != n) return null;
			var r = new int[n];
			for (int i = 0; i < n; i++) if (!int.TryParse (p[i].Trim (), out r[i])) return null;
			return r;
		}
		public static void Save ()
		{
			Settings.Set ("PadMap", string.Join (",", Map));
			Settings.Set ("PadAxes", string.Join (",", Axes));
			Apply ();
		}
		public static void Apply () { try { ne_pad_map (Map, Map.Length, Axes); } catch { } }

		public class Raw { public string Name; public uint Buttons; public int[] Axis = new int[6]; public int Pov = -1; }
		public static Raw Read ()
		{
			var b = new byte[64]; var r = new Raw ();
			try { if (ne_joy_raw (b, b.Length, out r.Buttons, r.Axis, out r.Pov) == 0) return null; } catch { return null; }
			r.Name = Native.FromZ (b);
			return r;
		}
		public static bool Held (Raw j, int src)
		{
			if (src >= 1 && src <= 32) return ((j.Buttons >> (src - 1)) & 1) != 0;
			if (src >= 100 && src < 112) { int v = j.Axis[(src - 100) / 2]; return (src & 1) != 0 ? v > 500 : v < -500; }
			if (src >= 200 && src < 204 && j.Pov >= 0)
			{
				int d = j.Pov / 100, c = (src - 200) * 90, diff = Math.Abs (d - c); if (diff > 180) diff = 360 - diff;
				return diff <= 45;
			}
			return false;
		}
		public static string Describe (int src)
		{
			if (src >= 1 && src <= 32) return "Button " + src;
			if (src >= 100 && src < 112) return "Axis " + AxisName[(src - 100) / 2] + ((src & 1) != 0 ? "+" : "-");
			if (src >= 200 && src < 204) return "Hat " + new[] { "up", "right", "down", "left" }[src - 200];
			return "-";
		}
		public static string AxisLabel (int a) => a >= 0 && a < 6 ? AxisName[a] : "None";
	}

	class PadDialog : Form
	{
		readonly int[] map = (int[]) Pads.Map.Clone (), axes = (int[]) Pads.Axes.Clone ();
		readonly Label padName = new Label { AutoSize = false };
		readonly Label[] src = new Label[16];
		readonly Panel[] lamp = new Panel[16];
		readonly ComboBox[] axisBox = new ComboBox[4];
		readonly Label axisLive = new Label { AutoSize = false };
		readonly Timer timer = new Timer { Interval = 30 };
		int capturing = -1; Pads.Raw rest;

		public PadDialog ()
		{
			Text = "Gamepad";
			FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = false; MinimizeBox = false;
			StartPosition = FormStartPosition.CenterParent;
			ClientSize = new Size (560, 650);
			Font = new Font ("Segoe UI", 9f);
			KeyPreview = true;
			var intro = new Label
			{
				Text = "For USB pads that are not Xbox-style ones (Xbox / XInput pads need nothing). The buttons are named after their " +
				       "place, as on Onyx: each emulator maps them as its console's pad (Help > Controls in a game). " +
				       "Set, then press the pad's button (Esc: stop, Delete: none).",
				Location = new Point (12, 8), Size = new Size (536, 48)
			};
			Controls.Add (intro);
			padName.Location = new Point (12, 58); padName.Size = new Size (536, 20); padName.Font = new Font (Font, FontStyle.Bold);
			Controls.Add (padName);
			int y = 84;
			for (int i = 0; i < 16; i++)
			{
				int k = i;
				lamp[i] = new Panel { Location = new Point (12, y + 5), Size = new Size (12, 12), BackColor = Color.Gainsboro };
				Controls.Add (lamp[i]);
				Controls.Add (new Label { Text = Pads.Names[i], Location = new Point (32, y + 3), Size = new Size (140, 20) });
				src[i] = new Label { Location = new Point (176, y + 3), Size = new Size (160, 20) };
				Controls.Add (src[i]);
				var set = new Button { Text = "Set", Location = new Point (344, y), Size = new Size (70, 24) };
				set.Click += (s, e) => StartCapture (k);
				Controls.Add (set);
				var none = new Button { Text = "None", Location = new Point (420, y), Size = new Size (60, 24) };
				none.Click += (s, e) => { map[k] = 0; capturing = -1; Show_ (); };
				Controls.Add (none);
				y += 28;
			}
			y += 6;
			string[] an = { "Left stick X", "Left stick Y", "Right stick X", "Right stick Y" };
			for (int i = 0; i < 4; i++)
			{
				int k = i;
				Controls.Add (new Label { Text = an[i], Location = new Point (12 + (i % 2) * 270, y + 3 + (i / 2) * 28), Size = new Size (100, 20) });
				var cb = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Location = new Point (114 + (i % 2) * 270, y + (i / 2) * 28), Size = new Size (90, 24) };
				cb.Items.AddRange (new object[] { "None", "X", "Y", "Z", "R", "U", "V" });
				cb.SelectedIndex = axes[i] + 1;
				cb.SelectedIndexChanged += (s, e) => axes[k] = cb.SelectedIndex - 1;
				axisBox[i] = cb;
				Controls.Add (cb);
			}
			y += 60;
			axisLive.Location = new Point (12, y); axisLive.Size = new Size (536, 20);
			Controls.Add (axisLive);
			y += 28;
			var def = new Button { Text = "Defaults", Location = new Point (12, y), Size = new Size (90, 28) };
			def.Click += (s, e) =>
			{
				Array.Copy (Pads.DefaultMap, map, 16); Array.Copy (Pads.DefaultAxes, axes, 4);
				for (int i = 0; i < 4; i++) axisBox[i].SelectedIndex = axes[i] + 1;
				capturing = -1; Show_ ();
			};
			var ok = new Button { Text = "OK", Location = new Point (372, y), Size = new Size (84, 28), DialogResult = DialogResult.OK };
			var cancel = new Button { Text = "Cancel", Location = new Point (464, y), Size = new Size (84, 28), DialogResult = DialogResult.Cancel };
			Controls.AddRange (new Control[] { def, ok, cancel });
			AcceptButton = ok; CancelButton = cancel;
			ClientSize = new Size (560, y + 40);
			Show_ ();
			timer.Tick += (s, e) => Poll ();
			timer.Start ();
			FormClosed += (s, e) =>
			{
				timer.Stop ();
				if (DialogResult == DialogResult.OK) { Array.Copy (map, Pads.Map, 16); Array.Copy (axes, Pads.Axes, 4); Pads.Save (); }
			};
		}

		protected override bool ProcessCmdKey (ref Message msg, Keys k)
		{
			if (capturing >= 0 && k == Keys.Escape) { capturing = -1; Show_ (); return true; }
			if (capturing >= 0 && k == Keys.Delete) { map[capturing] = 0; capturing = -1; Show_ (); return true; }
			return base.ProcessCmdKey (ref msg, k);
		}

		void StartCapture (int i) { capturing = i; rest = Pads.Read (); Show_ (); }

		void Show_ ()
		{
			for (int i = 0; i < 16; i++)
			{
				src[i].Text = i == capturing ? "press a button..." : Pads.Describe (map[i]);
				src[i].ForeColor = i == capturing ? Color.RoyalBlue : SystemColors.ControlText;
			}
		}

		void Poll ()
		{
			var j = Pads.Read ();
			padName.Text = j == null ? "No USB pad found (plug it in: it is looked for all the time)." : "Pad: " + j.Name;
			for (int i = 0; i < 16; i++) lamp[i].BackColor = j != null && map[i] != 0 && Pads.Held (j, map[i]) ? Color.LimeGreen : Color.Gainsboro;
			axisLive.Text = j == null ? "" : "Axes  " + string.Join ("   ", Enumerable.Range (0, 6).Select (a => Pads.AxisLabel (a) + " " + j.Axis[a])) + (j.Pov >= 0 ? "   Hat " + j.Pov / 100 + "°" : "");
			if (capturing < 0 || j == null) return;
			if (rest == null) { rest = j; return; }
			int found = 0;
			for (int b = 1; b <= 32 && found == 0; b++) if (Pads.Held (j, b) && !Pads.Held (rest, b)) found = b;
			for (int a = 0; a < 6 && found == 0; a++)
			{
				if (Math.Abs (rest.Axis[a]) > 300) continue;				// (an axis resting off-centre: a trigger)
				if (j.Axis[a] > 600) found = 100 + a * 2 + 1; else if (j.Axis[a] < -600) found = 100 + a * 2;
			}
			if (found == 0 && j.Pov >= 0 && rest.Pov < 0) found = 200 + ((j.Pov / 100 + 45) / 90) % 4;
			if (found == 0) { if (j.Buttons == 0 && j.Pov < 0) rest = j; return; }	// (all released: a new rest)
			map[capturing] = found;
			capturing = -1;
			Show_ ();
		}
	}
}
