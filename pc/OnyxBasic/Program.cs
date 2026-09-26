// Program.cs -- Onyx BASIC for Windows.
//   OnyxBasic.exe [file.bas]                      the editor (QBasic style)
//   OnyxBasic.exe --run [options] file.bas [args]  run a program in its window
// Options: --sd <folder> (what SD:/ stands for), --cwd <folder> (the current directory;
// default: the program's), --err <file> (a syntax / runtime error is written there as
// "line" + newline + "message", for the editor).
using System;
using System.IO;
using System.Windows.Forms;

namespace OnyxBasic
{
	static class Program
	{
		[STAThread]
		static int Main (string[] argv)
		{
			Application.EnableVisualStyles ();
			Application.SetCompatibleTextRenderingDefault (false);
			bool run = false; string cwd = null, err = null, file = null; int i = 0;
			for (; i < argv.Length; i++)
			{
				string a = argv[i];
				if (a == "--run") run = true;
				else if (a == "--sd" && i + 1 < argv.Length) Settings.Override = argv[++i];
				else if (a == "--cwd" && i + 1 < argv.Length) cwd = argv[++i];
				else if (a == "--err" && i + 1 < argv.Length) err = argv[++i];
				else { file = a; i++; break; }
			}
			string args = string.Join (" ", argv, i, argv.Length - i);
			if (!File.Exists (Path.Combine (AppDomain.CurrentDomain.BaseDirectory, "obcore.dll")))
			{
				MessageBox.Show ("obcore.dll is missing: keep it next to OnyxBasic.exe.", "Onyx BASIC", MessageBoxButtons.OK, MessageBoxIcon.Error);
				return 1;
			}
			if (run)
			{
				if (file == null) { MessageBox.Show ("--run needs a .bas file.", "Onyx BASIC"); return 1; }
				return Runner.Run (Path.GetFullPath (file), cwd, err, args);
			}
			Application.Run (new EditorForm (file != null ? Path.GetFullPath (file) : null));
			return 0;
		}
	}
}
