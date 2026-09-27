// Library.cs -- the games of the library (their paths, kept in library.txt, one a line, UTF-8),
// the settings (settings.ini) and the pictures of the games (thumbs\*.png). All of it beside
// NintendoEMU.exe when that folder can be written, else in %APPDATA%\NintendoEMU.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace NintendoEMU
{
	class Game
	{
		public string Path;
		public Sys System;
		public string Name;			// from the file's name: "Super Mario World (Europe).sfc" -> "Super Mario World"
		public bool Exists => File.Exists (Path);
		public string SystemName => Native.SystemName (System);
	}

	static class Store
	{
		static string dir;
		public static string Dir
		{
			get
			{
				if (dir != null) return dir;
				string exe = AppDomain.CurrentDomain.BaseDirectory;
				try
				{
					string t = System.IO.Path.Combine (exe, ".write-test");
					File.WriteAllText (t, "");
					File.Delete (t);
					dir = exe;
				}
				catch
				{
					dir = System.IO.Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.ApplicationData), "NintendoEMU");
					Directory.CreateDirectory (dir);
				}
				return dir;
			}
		}
		public static string File_ (string name) => System.IO.Path.Combine (Dir, name);
	}

	static class Settings
	{
		static readonly Dictionary<string, string> kv = new Dictionary<string, string> (StringComparer.OrdinalIgnoreCase);
		static bool loaded;
		static void Load ()
		{
			if (loaded) return;
			loaded = true;
			try
			{
				foreach (var line in File.ReadAllLines (Store.File_ ("settings.ini")))
				{
					int e = line.IndexOf ('=');
					if (e > 0 && !line.TrimStart ().StartsWith (";")) kv[line.Substring (0, e).Trim ()] = line.Substring (e + 1).Trim ();
				}
			}
			catch { }
		}
		public static string Get (string k, string def = "") { Load (); return kv.TryGetValue (k, out var v) ? v : def; }
		public static int GetInt (string k, int def) => int.TryParse (Get (k, ""), out int v) ? v : def;
		public static bool GetBool (string k, bool def) => GetInt (k, def ? 1 : 0) != 0;
		public static void Set (string k, object v)
		{
			Load ();
			kv[k] = v is bool b ? (b ? "1" : "0") : v.ToString ();
			try
			{
				var lines = new List<string> { "; NintendoEMU settings" };
				foreach (var p in kv) lines.Add (p.Key + " = " + p.Value);
				File.WriteAllLines (Store.File_ ("settings.ini"), lines);
			}
			catch { }
		}
	}

	static class Library
	{
		public static readonly List<Game> Games = new List<Game> ();
		static string ListFile => Store.File_ ("library.txt");

		public static void Load ()
		{
			Games.Clear ();
			try
			{
				foreach (var line in File.ReadAllLines (ListFile, Encoding.UTF8))
				{
					string p = line.Trim ();
					if (p.Length == 0 || p.StartsWith (";")) continue;
					Add (p, false);
				}
			}
			catch { }
		}
		public static void Save ()
		{
			try
			{
				var lines = new List<string> { "; NintendoEMU library: the games, one path a line" };
				lines.AddRange (Games.Select (g => g.Path));
				File.WriteAllLines (ListFile, lines, new UTF8Encoding (false));
			}
			catch { }
		}

		// a file -> the library (false: not a game, or already there)
		public static bool Add (string path, bool save = true)
		{
			try { path = System.IO.Path.GetFullPath (path); } catch { return false; }
			var sys = Native.SystemOf (path);
			if (sys == Sys.None) return false;
			if (Games.Any (g => string.Equals (g.Path, path, StringComparison.OrdinalIgnoreCase))) return false;
			if (sys == Sys.GB && path.EndsWith (".gbc", StringComparison.OrdinalIgnoreCase)) sys = Sys.GBC;
			Games.Add (new Game { Path = path, System = sys, Name = NiceName (path) });
			if (save) Save ();
			return true;
		}
		// every game of a folder and its sub-folders -> how many were added
		public static int AddFolder (string folder)
		{
			int n = 0;
			try
			{
				foreach (var f in Directory.EnumerateFiles (folder, "*.*", SearchOption.AllDirectories))
					if (Add (f, false)) n++;
			}
			catch { }
			Save ();
			return n;
		}
		public static void Remove (Game g) { Games.Remove (g); Save (); }

		public static string NiceName (string path)
		{
			string n = System.IO.Path.GetFileNameWithoutExtension (path);
			string s = Regex.Replace (n, @"\s*[\(\[][^\)\]]*[\)\]]", "");	// "(Europe)", "[!]"
			s = s.Replace ('_', ' ').Trim ();
			// "Legend of Zelda, The - A Link to the Past" -> "The Legend of Zelda - A Link to the Past"
			var m = Regex.Match (s, @"^(.*?), (The|A|An|La|Le|Les|L')( - .*)?$");
			if (m.Success) s = m.Groups[2].Value + " " + m.Groups[1].Value + m.Groups[3].Value;
			return s.Length > 0 ? s : n;
		}

		// ---- the pictures ----------------------------------------------------------------------------
		public const int TW = 160, TH = 144;
		static string ThumbFile (Game g)
		{
			using (var md5 = MD5.Create ())
			{
				var h = md5.ComputeHash (Encoding.UTF8.GetBytes (g.Path.ToLowerInvariant ()));
				return Store.File_ (System.IO.Path.Combine ("thumbs", BitConverter.ToString (h).Replace ("-", "").Substring (0, 16) + ".png"));
			}
		}
		public static Bitmap CachedThumb (Game g)
		{
			try
			{
				string f = ThumbFile (g);
				if (!File.Exists (f)) return null;
				using (var b = new Bitmap (f)) return new Bitmap (b);		// (the file is not kept open)
			}
			catch { return null; }
		}
		// made by the core (a game run a few seconds unseen, or a label / a banner): a worker thread
		public static Bitmap MakeThumb (Game g)
		{
			var px = new uint[TW * TH]; var banner = new uint[96 * 32];
			int r = Native.Thumb (g.Path, px, banner, out string name);
			if (r == 0) return null;
			Bitmap b;
			if (r == 1)
			{
				b = new Bitmap (TW, TH, PixelFormat.Format32bppArgb);
				var d = b.LockBits (new Rectangle (0, 0, TW, TH), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
				unsafe { fixed (uint *s = px) Buffer.MemoryCopy (s, (void *) d.Scan0, TW * TH * 4, TW * TH * 4); }
				b.UnlockBits (d);
			}
			else b = Label (g, name.Length > 0 ? name : g.Name, r == 3 ? banner : null);
			try
			{
				string f = ThumbFile (g);
				Directory.CreateDirectory (System.IO.Path.GetDirectoryName (f));
				b.Save (f, ImageFormat.Png);
			}
			catch { }
			return b;
		}

		static readonly Color[] SysColor = { Color.Gray, Color.FromArgb (0x8B, 0xAC, 0x0F), Color.FromArgb (0x60, 0x40, 0xC0), Color.FromArgb (0x30, 0x30, 0xA0),
			Color.FromArgb (0xC0, 0x20, 0x20), Color.FromArgb (0x70, 0x70, 0x80), Color.FromArgb (0xC0, 0x20, 0x20), Color.FromArgb (0x6A, 0x5A, 0xCD) };
		// a card: the system's band, (a GameCube banner,) the name -- also the picture while none is made
		public static Bitmap Label (Game g, string name, uint[] banner)
		{
			var b = new Bitmap (TW, TH, PixelFormat.Format32bppArgb);
			using (var gr = Graphics.FromImage (b))
			{
				gr.SmoothingMode = SmoothingMode.AntiAlias;
				gr.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
				gr.Clear (Color.FromArgb (0x20, 0x24, 0x38));
				using (var bg = new SolidBrush (Color.FromArgb (0x38, 0x3C, 0x58))) gr.FillRectangle (bg, 6, 6, TW - 12, TH - 12);
				using (var band = new SolidBrush (SysColor[(int) g.System])) gr.FillRectangle (band, 6, 6, TW - 12, 20);
				using (var f = new Font ("Segoe UI", 8f, FontStyle.Bold)) gr.DrawString (g.SystemName.ToUpperInvariant (), f, Brushes.White, 10, 8);
				int y = 32;
				if (banner != null)
				{
					var bb = new Bitmap (96, 32, PixelFormat.Format32bppArgb);
					var d = bb.LockBits (new Rectangle (0, 0, 96, 32), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
					unsafe { fixed (uint *s = banner) Buffer.MemoryCopy (s, (void *) d.Scan0, 96 * 32 * 4, 96 * 32 * 4); }
					bb.UnlockBits (d);
					gr.InterpolationMode = InterpolationMode.HighQualityBicubic;
					gr.DrawImage (bb, 8, y, 144, 48);
					bb.Dispose ();
					y += 52;
				}
				using (var f = new Font ("Segoe UI", 10f))
				using (var c = new SolidBrush (Color.FromArgb (0xF0, 0xE8, 0xC0)))
					gr.DrawString (name, f, c, new RectangleF (10, y, TW - 20, TH - y - 8));
			}
			return b;
		}
	}
}
