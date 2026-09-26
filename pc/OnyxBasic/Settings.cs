// Settings.cs -- where SD:/ is (a folder of the PC), kept in %APPDATA%\OnyxBasic\settings.ini.
using System;
using System.Collections.Generic;
using System.IO;
using System.Windows.Forms;

namespace OnyxBasic
{
	static class Settings
	{
		static readonly string File_ = Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.ApplicationData), "OnyxBasic", "settings.ini");
		static readonly Dictionary<string, string> kv = new Dictionary<string, string> (StringComparer.OrdinalIgnoreCase);
		static bool loaded;

		static void Load ()
		{
			if (loaded) return;
			loaded = true;
			try
			{
				foreach (var line in File.ReadAllLines (File_))
				{
					int e = line.IndexOf ('=');
					if (e > 0) kv[line.Substring (0, e).Trim ()] = line.Substring (e + 1).Trim ();
				}
			}
			catch { }
		}
		public static string Get (string k, string def = "") { Load (); return kv.TryGetValue (k, out var v) ? v : def; }
		public static void Set (string k, string v)
		{
			Load (); kv[k] = v;
			try
			{
				Directory.CreateDirectory (Path.GetDirectoryName (File_));
				var lines = new List<string> ();
				foreach (var p in kv) lines.Add (p.Key + " = " + p.Value);
				File.WriteAllLines (File_, lines);
			}
			catch { }
		}

		// The SD folder: --sd, else the setting, else an "sdcard" folder beside or above the program
		// (the Onyx repository's pc/dist sits two levels below its sdcard/).
		public static string Override;
		public static string SdFolder
		{
			get
			{
				if (!string.IsNullOrEmpty (Override)) return Override;
				string s = Get ("sd");
				if (s != "" && Directory.Exists (s)) return s;
				string d = AppDomain.CurrentDomain.BaseDirectory;
				for (int i = 0; i < 4 && d != null; i++)
				{
					string c = Path.Combine (d, "sdcard");
					if (Directory.Exists (c)) return Path.GetFullPath (c);
					d = Path.GetDirectoryName (d.TrimEnd ('\\'));
				}
				return Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.MyDocuments), "OnyxSD");
			}
			set { Set ("sd", value); }
		}
		public static bool ChooseSd (IWin32Window owner)
		{
			using (var d = new FolderBrowserDialog ())
			{
				d.Description = "The folder that stands for SD:/ (the Onyx SD card: basic/, apps/, docs/...)";
				d.SelectedPath = SdFolder;
				if (d.ShowDialog (owner) != DialogResult.OK) return false;
				SdFolder = d.SelectedPath;
				return true;
			}
		}
	}
}
