// Program.cs -- NintendoEMU: the emulators of Onyx on Windows, with a library of the games.
//   NintendoEMU.exe            the library
//   NintendoEMU.exe <rom>      the rom added to the library and played
using System;
using System.IO;
using System.Windows.Forms;

namespace NintendoEMU
{
	static class Program
	{
		[STAThread]
		static void Main (string[] args)
		{
			Application.EnableVisualStyles ();
			Application.SetCompatibleTextRenderingDefault (false);
			try { Native.ne_audio_queued (); }
			catch (DllNotFoundException)
			{
				MessageBox.Show ("nemucore.dll is missing: keep it beside NintendoEMU.exe.", "NintendoEMU", MessageBoxButtons.OK, MessageBoxIcon.Error);
				return;
			}
			Library.Load ();
			string rom = args.Length > 0 && File.Exists (args[0]) ? Path.GetFullPath (args[0]) : null;
			Application.Run (new MainForm (rom));
		}
	}
}
