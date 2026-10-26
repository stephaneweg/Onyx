// Lang.cs -- 3DForge for Windows in the user's language: Windows' (its display language); French has its catalogues --
// Onyx's own (lang\fr.txt beside the program: the core's words, the same file as sdcard/apps/3dforge.app/lang/fr.txt) and
// the window's (lang\fr-win.txt: the menus, the dialogs, what only the Windows version says). "English<TAB>translation"
// a line (\t \n \\ escaped, "#" a comment); a word not found stays in English. FORGE_LANG=en|fr chooses another.
// MIT licence (Onyx).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Forge
{
	static class Lang
	{
		static readonly Dictionary<string, string> words = new Dictionary<string, string> ();
		public static string Code = "en";

		// The language chosen, its catalogues loaded here and in the core (before Core.Init).
		public static void Load (string exeDir)
		{
			string code = Environment.GetEnvironmentVariable ("FORGE_LANG");
			if (string.IsNullOrEmpty (code)) code = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
			if (code != "fr") return;
			string dir = Path.Combine (exeDir, "lang"), core = Path.Combine (dir, code + ".txt"), win = Path.Combine (dir, code + "-win.txt");
			bool any = false;
			foreach (var p in new[] { core, win })
			{
				if (!File.Exists (p)) continue;
				any = true; Read (p);
				Core.fc_lang (Encoding.UTF8.GetBytes (p + "\0"));
			}
			if (any) Code = code;
		}
		static void Read (string path)
		{
			foreach (var raw in File.ReadAllLines (path, Encoding.UTF8))
			{
				if (raw.Length == 0 || raw[0] == '#') continue;
				int t = raw.IndexOf ('\t'); if (t < 0 || t + 1 >= raw.Length) continue;
				words[Unescape (raw.Substring (0, t))] = Unescape (raw.Substring (t + 1));
			}
		}
		static string Unescape (string s)
		{
			if (s.IndexOf ('\\') < 0) return s;
			var b = new StringBuilder ();
			for (int i = 0; i < s.Length; i++)
			{
				if (s[i] != '\\' || i + 1 >= s.Length) { b.Append (s[i]); continue; }
				char c = s[++i]; b.Append (c == 't' ? '\t' : c == 'n' ? '\n' : c);
			}
			return b.ToString ();
		}
		// The word in the user's language, else in English.
		public static string Tr (string en) => en != null && words.TryGetValue (en, out var w) ? w : en;
		// ... with an underscore marking the menu's access key: looked up without it.
		public static string TrMenu (string en)
		{
			string t = Tr (en.Replace ("_", ""));
			if (t == en.Replace ("_", "")) return en;
			return "_" + t;
		}
	}
}
