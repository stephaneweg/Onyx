// Mapping.cs -- what the device's controls send: PocketTermSim.buttons.ini, beside the program.
// rdpd's protocol has no gamepad message (user/BinUtils/rdpd.c: PTR, KEY, CHAR only), so the pad's
// buttons are sent as keys -- X11 keysyms, as the rest of the keyboard (user/BinUtils/remotekeys.h).
//   [buttons]  a pad button = a chord: "Enter", "Ctrl+PageUp", "Shift+F10", "Super", "none"
//   [fn]       a key of the thumb keyboard = the key it gives with Fn held
//   [pc]       a pad button = the PC key that presses it (a System.Windows.Forms.Keys name)
// The file is written with the defaults when it is not there; a line that cannot be read is told.
// MIT licence (Onyx).
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace PocketTermSim
{
	// Modifiers held around one key (or a modifier alone: Key is that modifier, Mods empty).
	class Chord
	{
		public readonly List<uint> Mods = new List<uint> ();
		public uint Key;
		public string Text = "none";
		public string Local;			// a combination the device keeps for itself ("volume +"): nothing is sent
		public bool Empty { get { return Key == 0 && Mods.Count == 0 && Local == null; } }
	}

	class Mapping
	{
		public const uint ShiftL = 0xFFE1, ShiftR = 0xFFE2, CtrlL = 0xFFE3, AltL = 0xFFE9, AltR = 0xFFEA, SuperL = 0xFFEB;
		public static readonly string[] PadButtons = { "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "L", "R", "Select", "Start" };

		public readonly Dictionary<string, Chord> Buttons = new Dictionary<string, Chord> (StringComparer.OrdinalIgnoreCase);
		public readonly Dictionary<string, Chord> Game = new Dictionary<string, Chord> (StringComparer.OrdinalIgnoreCase);	// [game]: the pad with Game keys on
		public readonly Dictionary<string, Chord> Fn = new Dictionary<string, Chord> (StringComparer.OrdinalIgnoreCase);
		public readonly Dictionary<Keys, string> Pc = new Dictionary<Keys, string> ();
		public readonly List<string> Problems = new List<string> ();
		public string Path = "";

		public const string Default =
@"# PocketTermSim.buttons.ini -- what the PocketTerm35's controls send to Onyx (rdpd, port 3390).
# rdpd has no gamepad message: the pad's buttons are sent as keys. Edit, then Device > Reload the mapping.
# A value: a key, or modifiers + a key -- Ctrl+PageUp, Shift+F10, Alt+Tab -- or none.
# Keys: Up Down Left Right Enter Esc Tab Backspace Delete Insert Home End PageUp PageDown Space F1..F12
#       PrintScreen Pause ScrollLock Super Shift Ctrl Alt, one character (a, 1, /), or an X11 keysym in hexadecimal (0xFF67).

[buttons]
Up     = Up
Down   = Down
Left   = Left
Right  = Right
A      = Enter
B      = Esc
# X: closes the chosen app in the shell (the switcher, the Running pills); Delete inside an app
X      = Delete
# Y: the context menu (the Menu key = Shift+F10)
Y      = Shift+F10
L      = Ctrl+PageUp
R      = Ctrl+PageDown
# Select, Start: NOT the shell's -- games use them (PocketUI binds nothing to them either). They send nothing
# here; with Device > Game keys the whole pad sends the [game] keys below. The system's keys are on the
# keyboard: CMD pressed and released = the Menu (the app's menus, Home, Open apps); CMD held = Home; F10 the Menu too.
Select = none
Start  = none

# Device > Game keys (F11): the pad as the emulators read a keyboard (user/Include/gamepad.h's [keyboard]
# defaults: z x a s the face buttons by their place, q w the shoulders, Enter Start, Backspace Select) -- rdpd has
# no gamepad message, so this is how a game is played through the simulator.
[game]
Up     = Up
Down   = Down
Left   = Left
Right  = Right
A      = z
B      = x
X      = a
Y      = s
L      = q
R      = w
Select = Backspace
Start  = Enter

# The thumb keyboard's Fn layer -- the blue legends of the maker's button diagram: key = what Fn + key gives.
# Keys are named by their legend: 1 .. 0, a letter, - = ; ' , . / Backspace Tab Enter Space; Select and Start too.
# device: ... is a combination the PocketTerm35 keeps for itself (its keyboard controller changes the
# brightness or the volume and sends nothing to the computer): the simulator sends nothing and says so.
[fn]
1  = F1
2  = F2
3  = F3
4  = F4
5  = F5
6  = F6
7  = F7
8  = F8
9  = F9
0  = F10
q  = Esc
w  = device: mute
e  = device: volume -
r  = device: volume +
o  = F11
p  = F12
Backspace = Delete
z  = Insert
x  = Home
c  = device: sleep
v  = End
b  = PageUp
n  = PageDown
m  = ScrollLock
-  = device: brightness -
=  = device: brightness +
Select = PrintScreen
Start  = Pause

# The PC's keys that press the pad's buttons (the arrows, Enter and Esc already are the D-pad, A and B
# through the lines above). A name of System.Windows.Forms.Keys (F5, NumPad0, Pause...) or none.
[pc]
X      = F5
Y      = F6
L      = F7
R      = F8
Select = F9
Start  = F12
";

		// The file beside the program (written with the defaults when missing) -> the mapping.
		public static Mapping Load (string path)
		{
			string text = Default;
			try
			{
				if (File.Exists (path)) text = File.ReadAllText (path, Encoding.UTF8);
				else File.WriteAllText (path, Default.Replace ("\r\n", "\n").Replace ("\n", "\r\n"), new UTF8Encoding (false));
			}
			catch { }				// (a read-only folder: the defaults)
			var m = Parse (Default);		// the defaults first: a file may hold only what it changes
			m.Problems.Clear ();
			m.Read (text);
			m.Path = path;
			return m;
		}

		public static Mapping Parse (string text) { var m = new Mapping (); m.Read (text); return m; }

		void Read (string text)
		{
			string section = "";
			int n = 0;
			foreach (string raw in text.Split ('\n'))
			{
				n++;
				string line = raw.Trim ();
				if (line.Length == 0 || line[0] == '#' || line[0] == ';') continue;
				if (line[0] == '[') { section = line.Trim ('[', ']', ' ').ToLowerInvariant (); continue; }
				int eq = line[0] == '=' ? line.IndexOf ('=', 1) : line.IndexOf ('=');	// (the key "=" itself)
				if (eq < 0) { Problems.Add ("line " + n + ": no '='"); continue; }
				string name = line.Substring (0, eq).Trim (), value = line.Substring (eq + 1).Trim ();
				if (section == "buttons" || section == "fn" || section == "game")
				{
					Chord c = section == "fn" && value.StartsWith ("device:", StringComparison.OrdinalIgnoreCase)
						? new Chord { Local = value.Substring (7).Trim (), Text = value } : ParseChord (value);
					if (c == null) { Problems.Add ("line " + n + ": '" + value + "' is not a key"); continue; }
					if (section == "buttons" || section == "game")
					{
						if (Array.FindIndex (PadButtons, b => b.Equals (name, StringComparison.OrdinalIgnoreCase)) < 0) { Problems.Add ("line " + n + ": no button '" + name + "'"); continue; }
						if (section == "game") Game[name] = c; else Buttons[name] = c;
					}
					else if (c.Empty) Fn.Remove (name); else Fn[name] = c;
				}
				else if (section == "pc")
				{
					int b = Array.FindIndex (PadButtons, x => x.Equals (name, StringComparison.OrdinalIgnoreCase));
					if (b < 0) { Problems.Add ("line " + n + ": no button '" + name + "'"); continue; }
					foreach (var old in new List<Keys> (Pc.Keys)) if (Pc[old] == PadButtons[b]) Pc.Remove (old);
					if (value.Equals ("none", StringComparison.OrdinalIgnoreCase) || value.Length == 0) continue;
					Keys k;
					if (!Enum.TryParse (value, true, out k) || k == Keys.None) { Problems.Add ("line " + n + ": '" + value + "' is not a PC key"); continue; }
					Pc[k] = PadButtons[b];
				}
			}
		}

		// "Ctrl+PageUp" -> the chord; null: not understood
		public static Chord ParseChord (string s)
		{
			var c = new Chord ();
			s = s.Trim ();
			if (s.Length == 0 || s.Equals ("none", StringComparison.OrdinalIgnoreCase)) return c;
			var parts = new List<string> ();
			int from = 0;
			for (int i = 1; i < s.Length; i++)		// (a '+' at the start of a part is the key '+')
				if (s[i] == '+' && i > from) { parts.Add (s.Substring (from, i - from)); from = i + 1; }
			parts.Add (s.Substring (from));
			for (int i = 0; i < parts.Count; i++)
			{
				uint sym = Keysym (parts[i].Trim ());
				if (sym == 0) return null;
				if (i < parts.Count - 1) { if (!IsModifier (sym)) return null; c.Mods.Add (sym); }
				else c.Key = sym;
			}
			c.Text = s;
			return c;
		}

		public static bool IsModifier (uint sym) { return sym >= 0xFFE1 && sym <= 0xFFEE; }

		static readonly Dictionary<string, uint> names = new Dictionary<string, uint> (StringComparer.OrdinalIgnoreCase)
		{
			{ "Up", 0xFF52 }, { "Down", 0xFF54 }, { "Left", 0xFF51 }, { "Right", 0xFF53 },
			{ "Enter", 0xFF0D }, { "Return", 0xFF0D }, { "Esc", 0xFF1B }, { "Escape", 0xFF1B }, { "Tab", 0xFF09 },
			{ "Backspace", 0xFF08 }, { "Delete", 0xFFFF }, { "Del", 0xFFFF }, { "Insert", 0xFF63 }, { "Ins", 0xFF63 },
			{ "PrintScreen", 0xFF61 }, { "PrtSc", 0xFF61 }, { "Pause", 0xFF13 }, { "ScrollLock", 0xFF14 }, { "ScrLk", 0xFF14 },
			{ "Home", 0xFF50 }, { "End", 0xFF57 }, { "PageUp", 0xFF55 }, { "PgUp", 0xFF55 }, { "PageDown", 0xFF56 }, { "PgDn", 0xFF56 },
			{ "Space", 0x20 }, { "Super", SuperL }, { "CMD", SuperL }, { "Win", SuperL },
			{ "Shift", ShiftL }, { "Ctrl", CtrlL }, { "Control", CtrlL }, { "Alt", AltL },
		};

		// a key's name -> its X11 keysym (0: unknown)
		public static uint Keysym (string name)
		{
			uint v;
			if (names.TryGetValue (name, out v)) return v;
			if (name.Length == 1 && name[0] >= 0x20 && name[0] <= 0xFF) return name[0];	// (Latin-1: the keysym is the character)
			if (name.Length >= 2 && (name[0] == 'F' || name[0] == 'f') && uint.TryParse (name.Substring (1), out v) && v >= 1 && v <= 12) return 0xFFBE + v - 1;
			if (name.StartsWith ("0x", StringComparison.OrdinalIgnoreCase) && uint.TryParse (name.Substring (2), System.Globalization.NumberStyles.HexNumber, null, out v)) return v;
			return 0;
		}

		// a keysym as the help shows it: "0xFF55 (PageUp)"
		public static string SymName (uint sym)
		{
			string n = null;
			foreach (var kv in names) if (kv.Value == sym) { n = kv.Key; break; }
			if (n == null && sym >= 0xFFBE && sym <= 0xFFC9) n = "F" + (sym - 0xFFBE + 1);
			if (n == null && sym > 0x20 && sym <= 0xFF) n = "'" + (char) sym + "'";
			return "0x" + sym.ToString ("X4") + (n != null ? " (" + n + ")" : "");
		}

		// the PC key that presses a pad button ("": none)
		public string PcKeyOf (string button)
		{
			foreach (var kv in Pc) if (kv.Value == button) return kv.Key.ToString ();
			return "";
		}
	}
}
