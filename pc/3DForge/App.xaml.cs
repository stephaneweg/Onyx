// App.xaml.cs -- 3DForge for Windows: the program starts here. The presets (the router's tool and machine, the resin,
// the filament) are kept in %APPDATA%\3DForge; the resin printers' list is printers.ini beside the program.
//   3DForge.exe [part.3df]
//   3DForge.exe [part.3df] --shot out.png [--size 1280x800] [--do "step;step..."]
// --shot: the window drawn off the screen, a script of steps played (Shot.cs), its picture saved, the program ends --
// the documentation's screenshots, the checks; the screen is never touched.
// MIT licence (Onyx).
using System;
using System.IO;
using System.Windows;

namespace Forge
{
	public partial class App : Application
	{
		public static string ConfigDir, ExeDir;

		protected override void OnStartup (StartupEventArgs e)
		{
			base.OnStartup (e);
			ExeDir = AppDomain.CurrentDomain.BaseDirectory;
			string file = null, shot = null, script = "", size = null;
			for (int i = 0; i < e.Args.Length; i++)
			{
				string a = e.Args[i];
				if (a == "--shot" && i + 1 < e.Args.Length) shot = e.Args[++i];
				else if (a == "--do" && i + 1 < e.Args.Length) script = e.Args[++i];
				else if (a == "--size" && i + 1 < e.Args.Length) size = e.Args[++i];
				else if (!a.StartsWith ("--")) file = a;
			}
			ConfigDir = Path.Combine (Environment.GetFolderPath (Environment.SpecialFolder.ApplicationData), "3DForge");
			if (shot != null) ConfigDir = Path.Combine (Path.GetTempPath (), "3DForge-shot");	// (a shot leaves the user's presets alone)
			try { Directory.CreateDirectory (ConfigDir); } catch { }
			string printers = Path.Combine (ConfigDir, "printers.ini");
			if (!File.Exists (printers)) printers = Path.Combine (ExeDir, "printers.ini");
			try { Core.Init (ConfigDir, printers); }
			catch (DllNotFoundException)
			{
				MessageBox.Show ("forgecore.dll is missing: keep it beside 3DForge.exe.", "3DForge", MessageBoxButton.OK, MessageBoxImage.Error);
				Shutdown (1); return;
			}
			// (an error of the program's own: said, written down in %APPDATA%\3DForge\errors.txt, and the work goes on;
			//  a shot ends there, its code 3)
			DispatcherUnhandledException += (s, ev) =>
			{
				try { File.AppendAllText (Path.Combine (ConfigDir, "errors.txt"), DateTime.Now + "  " + ev.Exception + "\r\n\r\n"); } catch { }
				ev.Handled = true;
				if (shot != null) { Shutdown (3); return; }
				MessageBox.Show ("Something went wrong: " + ev.Exception.Message + "\n\nThe details are in " + Path.Combine (ConfigDir, "errors.txt") + ".", "3DForge", MessageBoxButton.OK, MessageBoxImage.Warning);
			};
			var w = new MainWindow ();
			MainWindow = w;
			if (shot != null) { Shot.Run (w, file, shot, script, size); return; }
			w.Show ();
			if (file != null) w.OpenPath (file);
		}
	}
}
