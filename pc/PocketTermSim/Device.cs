// Device.cs -- the PocketTerm35's controls and what pressing them sends: the pad's buttons (their chords
// from the mapping), the thumb keyboard's keys (the character with Shift / Caps applied, the Fn layer),
// the sticky modifiers. Everything goes out as rdpd's KEY message (an X11 keysym, down then up): rdpd turns
// a keysym into Onyx input the way vncd does (user/BinUtils/remotekeys.h), Ctrl + a letter included.
//
// Sticky modifiers (a mouse cannot hold two keys): a click arms Shift / Ctrl / Alt / CMD -- its "down" is
// sent at once, so the Pi sees it held -- and the next key pressed releases it; a second click locks it
// (it stays down over several keys), a third releases it. Armed then released with no key in between is
// the modifier tapped alone (CMD: Home; Alt: the Menu entry). Fn works the same but is the simulator's own
// (nothing is sent for it); Caps is a plain toggle.
// MIT licence (Onyx).
using System;
using System.Collections.Generic;

namespace PocketTermSim
{
	enum Mod { None, Shift, Ctrl, Alt, Super, Fn, Caps }

	// One key of the thumb keyboard.
	class KeyDef
	{
		public string Id;			// "key:q", "key:Tab", "key:ShiftL"...
		public string Name;			// its name in the mapping's [fn] section ("q", "Tab", "Up")
		public string Label;			// the legend
		public char Base, Shifted;		// a character key
		public uint Sym;			// a special key (Tab, Enter, an arrow), or the modifier's keysym
		public Mod Mod;
		public int Span = 1;			// (the space bar: 3 places)
		public bool Glyph;			// the legend is a symbol (an arrow...)
	}

	class Device
	{
		public Mapping Map;
		public Action<bool, uint> Send = (down, sym) => { };	// a KEY message
		public event Action Changed;				// a state to redraw

		public readonly List<KeyDef[]> Rows = new List<KeyDef[]> ();
		public readonly Dictionary<string, KeyDef> Keys = new Dictionary<string, KeyDef> ();
		readonly Dictionary<string, int> sticky = new Dictionary<string, int> ();	// a modifier key: 0 off, 1 armed, 2 locked
		readonly Dictionary<string, Chord> down = new Dictionary<string, Chord> ();	// the controls held, what each sent
		public bool Caps { get; private set; }

		public Device (Mapping map)
		{
			Map = map;
			Row ("` 1 2 3 4 5 6 7 8 9 0 - = Backspace", "~ ! @ # $ % ^ & * ( ) _ +");
			Row ("Tab q w e r t y u i o p [ ] \\", "Q W E R T Y U I O P { } |");
			Row ("Caps a s d f g h j k l ; ' Enter", "A S D F G H J K L : \"");
			Row ("ShiftL z x c v b n m , . / Up ShiftR", "Z X C V B N M < > ?");
			Row ("FnL Ctrl AltL Space AltR CMD FnR Left Down Right", "");
		}

		void Row (string keys, string shifted)
		{
			string[] ks = keys.Split (' '), sh = shifted.Length > 0 ? shifted.Split (' ') : new string[0];
			var row = new List<KeyDef> ();
			int c = 0;
			foreach (string k in ks)
			{
				var d = new KeyDef { Id = "key:" + k, Name = k, Label = k };
				switch (k)
				{
				case "Backspace": d.Sym = 0xFF08; d.Label = "⌫"; d.Glyph = true; break;
				case "Tab": d.Sym = 0xFF09; break;
				case "Enter": d.Sym = 0xFF0D; d.Label = "↵"; d.Glyph = true; break;
				case "Up": d.Sym = 0xFF52; d.Label = "↑"; d.Glyph = true; break;
				case "Left": d.Sym = 0xFF51; d.Label = "←"; d.Glyph = true; break;
				case "Down": d.Sym = 0xFF54; d.Label = "↓"; d.Glyph = true; break;
				case "Right": d.Sym = 0xFF53; d.Label = "→"; d.Glyph = true; break;
				case "Caps": d.Mod = Mod.Caps; break;
				case "ShiftL": d.Mod = Mod.Shift; d.Sym = Mapping.ShiftL; d.Label = "⇧"; d.Glyph = true; break;
				case "ShiftR": d.Mod = Mod.Shift; d.Sym = Mapping.ShiftR; d.Label = "⇧"; d.Glyph = true; break;
				case "Ctrl": d.Mod = Mod.Ctrl; d.Sym = Mapping.CtrlL; break;
				case "AltL": d.Mod = Mod.Alt; d.Sym = Mapping.AltL; d.Label = "Alt"; break;
				case "AltR": d.Mod = Mod.Alt; d.Sym = Mapping.AltR; d.Label = "Alt"; break;
				case "CMD": d.Mod = Mod.Super; d.Sym = Mapping.SuperL; break;
				case "FnL": case "FnR": d.Mod = Mod.Fn; d.Label = "Fn"; break;
				case "Space": d.Base = d.Shifted = ' '; d.Label = ""; d.Span = 3; break;
				default:
					d.Base = k[0]; d.Shifted = c < sh.Length ? sh[c][0] : k[0]; c++;
					d.Label = char.IsLetter (d.Base) ? d.Shifted.ToString () : k;
					break;
				}
				row.Add (d); Keys[d.Id] = d;
			}
			Rows.Add (row.ToArray ());
		}

		// ---- the state, for the drawing ----
		public bool IsDown (string id) { return down.ContainsKey (id); }
		public int Sticky (string id) { int v; return sticky.TryGetValue (id, out v) ? v : 0; }
		public bool Active (Mod m)
		{
			if (m == Mod.Caps) return Caps;
			foreach (var kv in sticky) if (kv.Value != 0 && Keys[kv.Key].Mod == m) return true;
			return false;
		}
		// what a character key gives now
		public char CharOf (KeyDef d)
		{
			bool shift = Active (Mod.Shift);
			if (char.IsLetter (d.Base) && Caps) shift = !shift;
			return shift ? d.Shifted : d.Base;
		}
		// the Fn layer's key of a key (null: none)
		public Chord FnOf (KeyDef d) { Chord c; return Map.Fn.TryGetValue (d.Name, out c) && !c.Empty ? c : null; }

		// Game keys: the pad's buttons send the mapping's [game] keys (the emulators' keyboard) instead of [buttons]
		public bool GameKeys;

		// ---- pressing ----
		public void Press (string id)
		{
			if (down.ContainsKey (id)) { Repeat (id); return; }
			KeyDef d;
			Chord c;
			if (!Keys.TryGetValue (id, out d))			// a pad button
			{
				if (!(GameKeys ? Map.Game : Map.Buttons).TryGetValue (id, out c)) c = new Chord ();
			}
			else if (d.Mod != Mod.None)
			{
				Modifier (d);
				down[id] = new Chord ();			// (shown pressed; nothing more to send)
				Fire ();
				return;
			}
			else
			{
				c = Active (Mod.Fn) ? FnOf (d) : null;
				if (c == null) c = new Chord { Key = d.Sym != 0 ? d.Sym : CharOf (d) };
			}
			down[id] = c;
			foreach (uint m in c.Mods) Send (true, m);
			if (c.Key != 0) Send (true, c.Key);
			Fire ();
		}

		// held: the key again (never a modifier)
		public void Repeat (string id)
		{
			Chord c;
			if (down.TryGetValue (id, out c) && c.Key != 0 && !Mapping.IsModifier (c.Key)) Send (true, c.Key);
		}

		public void Release (string id)
		{
			Chord c;
			if (!down.TryGetValue (id, out c)) return;
			down.Remove (id);
			KeyDef d;
			bool modifier = Keys.TryGetValue (id, out d) && d.Mod != Mod.None;
			if (!modifier)
			{
				if (c.Key != 0) Send (false, c.Key);
				for (int i = c.Mods.Count - 1; i >= 0; i--) Send (false, c.Mods[i]);
				if (down.Count == 0) OneShotsOff ();		// (the armed modifiers served this key)
			}
			Fire ();
		}

		void Modifier (KeyDef d)
		{
			if (d.Mod == Mod.Caps) { Caps = !Caps; return; }
			int s = Sticky (d.Id);
			if (s == 0) { sticky[d.Id] = 1; if (d.Sym != 0) Send (true, d.Sym); }
			else if (s == 1) sticky[d.Id] = 2;
			else { sticky[d.Id] = 0; if (d.Sym != 0) Send (false, d.Sym); }
		}

		void OneShotsOff ()
		{
			foreach (string id in new List<string> (sticky.Keys))
				if (sticky[id] == 1) { sticky[id] = 0; if (Keys[id].Sym != 0) Send (false, Keys[id].Sym); }
		}

		// every modifier up (Device > Release all modifiers); quiet: the link is gone, nothing to send
		public void ReleaseAll (bool quiet = false)
		{
			foreach (string id in new List<string> (sticky.Keys))
			{
				if (sticky[id] != 0 && Keys[id].Sym != 0 && !quiet) Send (false, Keys[id].Sym);
				sticky[id] = 0;
			}
			down.Clear ();
			Fire ();
		}

		void Fire () { var h = Changed; if (h != null) h (); }

		// the modifiers held now, for the status line ("Ctrl Alt")
		public string ModsText ()
		{
			var s = new List<string> ();
			foreach (Mod m in new[] { Mod.Shift, Mod.Ctrl, Mod.Alt, Mod.Super, Mod.Fn, Mod.Caps })
				if (Active (m)) s.Add (m == Mod.Super ? "CMD" : m.ToString ());
			return string.Join (" ", s.ToArray ());
		}
	}
}
