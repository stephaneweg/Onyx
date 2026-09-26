// EditorForm.cs -- the Onyx BASIC editor for Windows, QBasic style (like Onyx's qbasic app):
//   * the program is split into modules edited one at a time: the main module and each SUB /
//     FUNCTION (View > SUBs... lists them; Edit > New SUB / New FUNCTION, or type "SUB Name" on
//     a line and press Enter, as in QBasic). On disk: one ordinary .bas file (the main module,
//     then every SUB / FUNCTION block), Latin-1, as on Onyx.
//   * Run > Start checks the syntax (obcore.dll) -- an error jumps to its module and line --
//     then runs a copy in its own window (OnyxBasic.exe --run); a runtime error jumps the
//     same way. SD:/ is the folder of Options > SD folder.
//   * Leaving a line capitalises the language's words, as QBasic does; Enter keeps the indent.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace OnyxBasic
{
	class Module { public string Name; public int Kind; public string Text; }	// kind 0 main, 1 SUB, 2 FUNCTION

	class EditorForm : Form
	{
		readonly List<Module> mods = new List<Module> ();
		int cur;
		string path;
		bool dirty;
		readonly RichTextBox ed;
		readonly Label head;
		readonly ToolStripStatusLabel stFile, stPos;
		readonly HashSet<string> words;
		ToolStripMenuItem miStart, miStop;
		Process running;

		// A fixed-width font: Consolas (every Windows), else the system's monospace one.
		static Font Mono (float size, FontStyle st = FontStyle.Regular)
		{
			var f = new Font ("Consolas", size, st);
			if (f.Name == "Consolas") return f;
			f.Dispose ();
			return new Font (FontFamily.GenericMonospace, size, st);
		}

		public EditorForm (string file)
		{
			Text = "Onyx BASIC";
			AutoScaleMode = AutoScaleMode.Dpi;
			ClientSize = new Size (900, 640);
			StartPosition = FormStartPosition.CenterScreen;
			try { Icon = Icon.ExtractAssociatedIcon (Application.ExecutablePath); } catch { }
			KeyPreview = true;

			words = new HashSet<string> (StringComparer.OrdinalIgnoreCase);
			try { foreach (var w in Native.Words ()) words.Add (w); } catch { }

			ed = new RichTextBox
			{
				Dock = DockStyle.Fill, Font = Mono (11f), WordWrap = false, AcceptsTab = true,
				DetectUrls = false, BorderStyle = BorderStyle.None, BackColor = Color.FromArgb (0, 0, 168),
				ForeColor = Color.FromArgb (220, 220, 220), ScrollBars = RichTextBoxScrollBars.Both, HideSelection = false,
				ShortcutsEnabled = true, MaxLength = int.MaxValue
			};
			ed.TextChanged += (s, e) => { if (!loading) { dirty = true; UpdateTitle (); } };
			ed.SelectionChanged += OnCaret;
			ed.KeyDown += OnEditorKey;
			head = new Label
			{
				Dock = DockStyle.Top, Height = 24, TextAlign = ContentAlignment.MiddleCenter, Font = Mono (10f, FontStyle.Bold),
				BackColor = Color.FromArgb (170, 170, 170), ForeColor = Color.Black
			};
			var status = new StatusStrip ();
			stFile = new ToolStripStatusLabel { Spring = true, TextAlign = ContentAlignment.MiddleLeft };
			stPos = new ToolStripStatusLabel ("1 : 1");
			status.Items.Add (stFile); status.Items.Add (stPos);
			Controls.Add (ed); Controls.Add (head); Controls.Add (status);
			var menu = MakeMenu ();
			Controls.Add (menu); MainMenuStrip = menu;
			FormClosing += (s, e) => { if (!ConfirmDiscard ()) e.Cancel = true; };

			if (file != null && File.Exists (file)) LoadFile (file);
			else LoadText ("");
		}

		// ---- the menus --------------------------------------------------------------------------------------
		MenuStrip MakeMenu ()
		{
			var m = new MenuStrip ();
			ToolStripMenuItem Item (string t, EventHandler h, Keys k = Keys.None, string disp = null)
			{
				var i = new ToolStripMenuItem (t, null, h);
				if (k != Keys.None) i.ShortcutKeys = k;
				if (disp != null) i.ShortcutKeyDisplayString = disp;
				return i;
			}
			var file = new ToolStripMenuItem ("&File");
			file.DropDownItems.AddRange (new ToolStripItem[] {
				Item ("&New", (s, e) => OpNew (), Keys.Control | Keys.N),
				Item ("&Open...", (s, e) => OpOpen (null), Keys.Control | Keys.O),
				Item ("E&xamples...", (s, e) => OpOpen (Path.Combine (Settings.SdFolder, "basic", "examples"))),
				new ToolStripSeparator (),
				Item ("&Save", (s, e) => OpSave (), Keys.Control | Keys.S),
				Item ("Save &As...", (s, e) => OpSaveAs ()),
				Item ("&Make App...", (s, e) => OpMakeApp ()),
				new ToolStripSeparator (),
				Item ("E&xit", (s, e) => Close ()) });
			var edit = new ToolStripMenuItem ("&Edit");
			edit.DropDownItems.AddRange (new ToolStripItem[] {
				Item ("&Undo", (s, e) => ed.Undo (), Keys.None, "Ctrl+Z"),
				Item ("&Redo", (s, e) => ed.Redo (), Keys.None, "Ctrl+Y"),
				new ToolStripSeparator (),
				Item ("Cu&t", (s, e) => ed.Cut (), Keys.None, "Ctrl+X"),
				Item ("&Copy", (s, e) => ed.Copy (), Keys.None, "Ctrl+C"),
				Item ("&Paste", (s, e) => PastePlain (), Keys.None, "Ctrl+V"),
				Item ("Select &All", (s, e) => ed.SelectAll (), Keys.None, "Ctrl+A"),
				new ToolStripSeparator (),
				Item ("&Find...", (s, e) => OpFind (false), Keys.Control | Keys.F),
				Item ("Find &Next", (s, e) => OpFind (true), Keys.F3),
				Item ("&Go to Line...", (s, e) => OpGoto (), Keys.Control | Keys.G),
				new ToolStripSeparator (),
				Item ("New &SUB...", (s, e) => OpNewProc (1)),
				Item ("New F&UNCTION...", (s, e) => OpNewProc (2)) });
			var view = new ToolStripMenuItem ("&View");
			view.DropDownItems.AddRange (new ToolStripItem[] {
				Item ("&SUBs...", (s, e) => OpSubs (), Keys.F2),
				Item ("&Next SUB", (s, e) => ShowModule ((cur + 1) % mods.Count), Keys.Shift | Keys.F2),
				Item ("&Previous SUB", (s, e) => ShowModule ((cur + mods.Count - 1) % mods.Count), Keys.Control | Keys.F2),
				Item ("&Main Module", (s, e) => ShowModule (0)) });
			var run = new ToolStripMenuItem ("&Run");
			miStart = Item ("&Start", (s, e) => OpRun (), Keys.F5);
			miStop = Item ("S&top", (s, e) => { try { running?.Kill (); } catch { } });
			miStop.Enabled = false;
			run.DropDownItems.AddRange (new ToolStripItem[] { miStart, miStop, Item ("&Check Syntax", (s, e) => OpCheck (true)) });
			var opt = new ToolStripMenuItem ("&Options");
			opt.DropDownItems.Add (Item ("&SD Folder...", (s, e) => { Settings.ChooseSd (this); UpdateTitle (); }));
			var help = new ToolStripMenuItem ("&Help");
			help.DropDownItems.AddRange (new ToolStripItem[] {
				Item ("&Keywords...", (s, e) => OpHelp (), Keys.F1),
				Item ("&About", (s, e) => MessageBox.Show (this, "Onyx BASIC for Windows\nThe editor, compiler and runtime of Onyx's BASIC, to write and try\nprograms on the PC. SD:/ = " + Settings.SdFolder, "About")) });
			m.Items.AddRange (new ToolStripItem[] { file, edit, view, run, opt, help });
			return m;
		}

		// ---- modules: split / compose (the same as Onyx's qbasic) ------------------------------------------------
		static readonly Regex Header = new Regex (@"^\s*(SUB|FUNCTION)\s+([A-Za-z][A-Za-z0-9_.]*[$%&!#]?)", RegexOptions.IgnoreCase);
		static readonly Regex EndOf = new Regex (@"^\s*END\s+(SUB|FUNCTION)\b", RegexOptions.IgnoreCase);

		void Split (string src)
		{
			mods.Clear ();
			var main = new StringBuilder ();
			var lines = src.Replace ("\r\n", "\n").Replace ('\r', '\n').Split ('\n');
			for (int i = 0; i < lines.Length; i++)
			{
				var h = Header.Match (lines[i]);
				if (h.Success)
				{
					int kind = h.Groups[1].Value.ToUpperInvariant () == "SUB" ? 1 : 2;
					var b = new StringBuilder (lines[i]);
					int j = i + 1;
					for (; j < lines.Length; j++)
					{
						b.Append ('\n').Append (lines[j]);
						var e = EndOf.Match (lines[j]);
						if (e.Success && (e.Groups[1].Value.ToUpperInvariant () == "SUB") == (kind == 1)) break;
					}
					mods.Add (new Module { Name = h.Groups[2].Value, Kind = kind, Text = b.ToString ().TrimEnd ('\n') });
					i = j;
					continue;
				}
				main.Append (lines[i]).Append ('\n');
			}
			mods.Insert (0, new Module { Name = "(main)", Kind = 0, Text = main.ToString ().TrimEnd ('\n') });
		}
		void Sync ()
		{
			if (cur < 0 || cur >= mods.Count) return;
			var m = mods[cur];
			m.Text = ed.Text.Replace ("\r", "");
			if (m.Kind != 0) { var h = Header.Match (m.Text.TrimStart ()); if (h.Success) m.Name = h.Groups[2].Value; }
		}
		// The whole program; starts[i] = the first line (1-based) of module i.
		string Compose (out int[] starts)
		{
			Sync ();
			var s = new StringBuilder (); starts = new int[mods.Count]; int line = 1;
			for (int i = 0; i < mods.Count; i++)
			{
				if (i > 0) { s.Append ('\n'); line++; }
				starts[i] = line;
				s.Append (mods[i].Text).Append ('\n');
				line += mods[i].Text.Count (c => c == '\n') + 1;
			}
			return s.ToString ();
		}

		bool loading; int caretLine;
		void ShowModule (int i)
		{
			if (i < 0 || i >= mods.Count) return;
			if (i != cur) Sync ();
			cur = i;
			loading = true;
			ed.Text = mods[i].Text;
			ed.SelectAll (); ed.SelectionColor = ed.ForeColor;	// light grey on blue, as in QBasic
			ed.SelectionStart = 0; ed.SelectionLength = 0;
			ed.SelectionColor = ed.ForeColor;
			caretLine = 0;
			loading = false;
			head.Text = mods[i].Kind == 0 ? "Main module" : (mods[i].Kind == 1 ? "SUB " : "FUNCTION ") + mods[i].Name;
			ed.Focus ();
			UpdateTitle ();
		}
		void GotoProgramLine (int line)
		{
			Compose (out int[] starts);
			int m = 0;
			for (int i = 0; i < mods.Count; i++) if (starts[i] <= line) m = i;
			ShowModule (m);
			GotoLine (line - starts[m]);
		}
		void GotoLine (int l)					// 0-based, in the shown module
		{
			l = Math.Max (0, Math.Min (l, ed.Lines.Length - 1));
			int at = ed.GetFirstCharIndexFromLine (l);
			if (at < 0) return;
			int len = ed.Lines.Length > l ? ed.Lines[l].Length : 0;
			ed.Select (at, len);
			ed.ScrollToCaret ();
			ed.Focus ();
		}

		// ---- editing: capitalised words, the indent, "SUB name" + Enter ------------------------------------------
		void OnCaret (object s, EventArgs e)
		{
			if (loading) return;
			int line = ed.GetLineFromCharIndex (ed.SelectionStart);
			int col = ed.SelectionStart - ed.GetFirstCharIndexFromLine (line);
			stPos.Text = (line + 1) + " : " + (col + 1);
			if (line != caretLine) { int old = caretLine; caretLine = line; Capitalise (old); }
		}
		string Words (string line)				// the language's words in capitals (not in strings, comments)
		{
			var o = new StringBuilder (line.Length);
			int i = 0;
			while (i < line.Length)
			{
				char c = line[i];
				if (c == '"') { int j = line.IndexOf ('"', i + 1); j = j < 0 ? line.Length : j + 1; o.Append (line, i, j - i); i = j; continue; }
				if (c == '\'') { o.Append (line, i, line.Length - i); break; }
				if (char.IsLetter (c))
				{
					int j = i; while (j < line.Length && (char.IsLetterOrDigit (line[j]) || line[j] == '_' || line[j] == '.')) j++;
					if (j < line.Length && "$%&!#".IndexOf (line[j]) >= 0) j++;
					string w = line.Substring (i, j - i);
					if (words.Contains (w)) w = w.ToUpperInvariant ();
					o.Append (w); i = j;
					if (w == "REM") { o.Append (line, i, line.Length - i); break; }
					continue;
				}
				o.Append (c); i++;
			}
			return o.ToString ();
		}
		void Capitalise (int line)
		{
			if (line < 0 || line >= ed.Lines.Length) return;
			string t = ed.Lines[line], n = Words (t);
			if (n == t) return;
			int at = ed.GetFirstCharIndexFromLine (line);
			int ss = ed.SelectionStart, sl = ed.SelectionLength;
			loading = true;
			ed.Select (at, t.Length); ed.SelectedText = n;
			ed.Select (ss, sl);
			loading = false;
			dirty = true; UpdateTitle ();
		}
		void OnEditorKey (object s, KeyEventArgs e)
		{
			if (e.KeyCode == Keys.V && e.Control) { PastePlain (); e.SuppressKeyPress = true; return; }
			if (e.KeyCode != Keys.Enter || e.Control || e.Alt) return;
			int line = ed.GetLineFromCharIndex (ed.SelectionStart);
			string text = line < ed.Lines.Length ? ed.Lines[line] : "";
			// QBasic: "SUB Name" typed outside that SUB's first line opens a new SUB
			var h = Header.Match (text);
			if (h.Success && !(mods[cur].Kind != 0 && line == 0))
			{
				e.SuppressKeyPress = true;
				int at = ed.GetFirstCharIndexFromLine (line);
				ed.Select (at, text.Length + (line < ed.Lines.Length - 1 ? 1 : 0)); ed.SelectedText = "";
				NewProc (Words (text.Trim ()), h.Groups[1].Value.ToUpperInvariant () == "SUB" ? 1 : 2, h.Groups[2].Value);
				return;
			}
			// keep the indentation of the line
			string ind = new string (text.TakeWhile (c => c == ' ' || c == '\t').ToArray ());
			e.SuppressKeyPress = true;
			ed.SelectedText = "\n" + ind;
		}
		void PastePlain ()
		{
			try { if (Clipboard.ContainsText ()) ed.SelectedText = Clipboard.GetText ().Replace ("\r\n", "\n").Replace ('\r', '\n'); } catch { }
		}

		// ---- files --------------------------------------------------------------------------------------------
		void LoadText (string src)
		{
			Split (src);
			cur = -1;						// (nothing to keep from the editor)
			ShowModule (0);
			dirty = false; UpdateTitle ();
		}
		void LoadFile (string p)
		{
			try { LoadText (Latin1.Enc.GetString (File.ReadAllBytes (p))); path = p; dirty = false; UpdateTitle (); }
			catch (Exception e) { MessageBox.Show (this, "Cannot open " + p + "\n" + e.Message, "Onyx BASIC"); }
		}
		bool WriteTo (string p)
		{
			try { File.WriteAllBytes (p, Latin1.Enc.GetBytes (Compose (out _))); return true; }
			catch (Exception e) { MessageBox.Show (this, "Cannot write " + p + "\n" + e.Message, "Onyx BASIC"); return false; }
		}
		void UpdateTitle ()
		{
			Text = (path != null ? Path.GetFileName (path) : "Untitled") + (dirty ? " *" : "") + " - Onyx BASIC";
			stFile.Text = (path ?? "Untitled") + (dirty ? "  (modified)" : "") + "    SD:/ = " + Settings.SdFolder;
		}
		bool ConfirmDiscard ()
		{
			if (!dirty) return true;
			var r = MessageBox.Show (this, "The program has changed. Save it first?", "Onyx BASIC", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
			if (r == DialogResult.Cancel) return false;
			if (r == DialogResult.Yes) return OpSave ();
			return true;
		}
		void OpNew () { if (!ConfirmDiscard ()) return; path = null; LoadText (""); }
		void OpOpen (string dir)
		{
			if (!ConfirmDiscard ()) return;
			using (var d = new OpenFileDialog { Filter = "BASIC programs (*.bas)|*.bas|All files (*.*)|*.*" })
			{
				d.InitialDirectory = dir != null && Directory.Exists (dir) ? dir : path != null ? Path.GetDirectoryName (path) : Settings.SdFolder;
				if (d.ShowDialog (this) == DialogResult.OK) LoadFile (d.FileName);
			}
		}
		bool OpSave ()
		{
			if (path == null) return OpSaveAs ();
			if (!WriteTo (path)) return false;
			dirty = false; UpdateTitle ();
			return true;
		}
		bool OpSaveAs ()
		{
			using (var d = new SaveFileDialog { Filter = "BASIC programs (*.bas)|*.bas|All files (*.*)|*.*", DefaultExt = "bas" })
			{
				d.InitialDirectory = path != null ? Path.GetDirectoryName (path) : Path.Combine (Settings.SdFolder, "basic");
				d.FileName = path != null ? Path.GetFileName (path) : "untitled.bas";
				if (d.ShowDialog (this) != DialogResult.OK) return false;
				path = d.FileName;
			}
			return OpSave ();
		}
		// File > Make App...: SD:/apps/<name>.app/{main.bas, app.txt, icon.bmp}, as on Onyx.
		void OpMakeApp ()
		{
			string name = path != null ? Path.GetFileNameWithoutExtension (path) : "";
			name = Ask ("Make App", "App folder name (apps/<name>.app):", name);
			if (string.IsNullOrEmpty (name)) return;
			if (name.IndexOfAny (new[] { ' ', '/', '\\', ':' }) >= 0) { MessageBox.Show (this, "No spaces, / or : in an app folder name.", "Make App"); return; }
			string title = Ask ("Make App", "App title (shown under its icon):", name);
			if (string.IsNullOrEmpty (title)) return;
			string dir = Path.Combine (Settings.SdFolder, "apps", name + ".app");
			try
			{
				Directory.CreateDirectory (dir);
				if (!WriteTo (Path.Combine (dir, "main.bas"))) return;
				File.WriteAllBytes (Path.Combine (dir, "app.txt"), Latin1.Enc.GetBytes ("# Onyx application metadata (written by QBasic > Make App)\nname = " + title + "\ncategory = BASIC\n"));
				string icon = Path.Combine (Settings.SdFolder, "apps", "qbasic.app", "program.bmp");
				if (File.Exists (icon)) File.Copy (icon, Path.Combine (dir, "icon.bmp"), true);
				MessageBox.Show (this, "App created: " + dir + "\nCopy it to the SD card's apps folder (it is already there if SD:/ is the card).", "Make App");
			}
			catch (Exception e) { MessageBox.Show (this, e.Message, "Make App"); }
		}

		// ---- SUBs -----------------------------------------------------------------------------------------------
		void NewProc (string header, int kind, string name)
		{
			Sync ();
			int have = mods.FindIndex (m => m.Kind != 0 && string.Equals (m.Name, name, StringComparison.OrdinalIgnoreCase));
			if (have >= 0) { ShowModule (have); return; }
			mods.Add (new Module { Name = name, Kind = kind, Text = header + "\n\nEND " + (kind == 1 ? "SUB" : "FUNCTION") });
			ShowModule (mods.Count - 1);
			GotoLine (1);
			dirty = true; UpdateTitle ();
		}
		void OpNewProc (int kind)
		{
			string n = Ask (kind == 1 ? "New SUB" : "New FUNCTION", "Name:", "");
			if (string.IsNullOrWhiteSpace (n)) return;
			n = n.Trim ();
			NewProc ((kind == 1 ? "SUB " : "FUNCTION ") + n, kind, Regex.Match (n, @"^[A-Za-z][A-Za-z0-9_.]*[$%&!#]?").Value);
		}
		void OpSubs ()
		{
			Sync ();
			using (var f = new Form { Text = "SUBs", FormBorderStyle = FormBorderStyle.FixedDialog, MinimizeBox = false, MaximizeBox = false, StartPosition = FormStartPosition.CenterParent, ClientSize = new Size (380, 340), AutoScaleMode = AutoScaleMode.Dpi })
			{
				var l = new ListBox { Left = 10, Top = 10, Width = 360, Height = 280, IntegralHeight = false, Font = Mono (10f) };
				foreach (var m in mods) l.Items.Add (m.Kind == 0 ? "(main module)" : (m.Kind == 1 ? "SUB " : "FUNCTION ") + m.Name);
				l.SelectedIndex = cur;
				var bEdit = new Button { Text = "Edit", Left = 10, Top = 300, Width = 90, DialogResult = DialogResult.OK };
				var bDel = new Button { Text = "Delete", Left = 110, Top = 300, Width = 90, DialogResult = DialogResult.Abort };
				var bCancel = new Button { Text = "Cancel", Left = 280, Top = 300, Width = 90, DialogResult = DialogResult.Cancel };
				l.DoubleClick += (s, e) => { f.DialogResult = DialogResult.OK; };
				f.Controls.AddRange (new Control[] { l, bEdit, bDel, bCancel });
				f.AcceptButton = bEdit; f.CancelButton = bCancel;
				var r = f.ShowDialog (this);
				int i = l.SelectedIndex;
				if (r == DialogResult.OK && i >= 0) ShowModule (i);
				else if (r == DialogResult.Abort && i > 0)
				{
					if (MessageBox.Show (this, "Delete " + l.Items[i] + "?", "SUBs", MessageBoxButtons.YesNo) != DialogResult.Yes) return;
					mods.RemoveAt (i);
					cur = -1; ShowModule (0);
					dirty = true; UpdateTitle ();
				}
			}
		}

		// ---- run --------------------------------------------------------------------------------------------------
		bool OpCheck (bool tell)
		{
			string src = Compose (out _);
			int line = Native.Check (src, out string msg);
			if (line == 0) { if (tell) MessageBox.Show (this, "No syntax error.", "Onyx BASIC"); return true; }
			GotoProgramLine (line);
			MessageBox.Show (this, "Syntax error in line " + line + ": " + msg, "Onyx BASIC", MessageBoxButtons.OK, MessageBoxIcon.Error);
			return false;
		}
		void OpRun ()
		{
			if (running != null) return;
			if (!OpCheck (false)) return;
			string tmpDir = Path.Combine (Path.GetTempPath (), "OnyxBasic");
			Directory.CreateDirectory (tmpDir);
			string tmp = Path.Combine (tmpDir, path != null ? Path.GetFileName (path) : "untitled.bas");
			string err = Path.Combine (tmpDir, "error.txt");
			try { File.Delete (err); File.WriteAllBytes (tmp, Latin1.Enc.GetBytes (Compose (out _))); }
			catch (Exception e) { MessageBox.Show (this, e.Message, "Onyx BASIC"); return; }
			string cwd = path != null ? Path.GetDirectoryName (path) : Settings.SdFolder;
			string Q (string p) { p = p.TrimEnd ('\\'); if (p.EndsWith (":")) p += "\\."; return "\"" + p + "\""; }
			var psi = new ProcessStartInfo (Application.ExecutablePath,
				"--run --sd " + Q (Settings.SdFolder) + " --cwd " + Q (cwd) + " --err " + Q (err) + " " + Q (tmp)) { UseShellExecute = false };
			try
			{
				running = Process.Start (psi);
				running.EnableRaisingEvents = true;
				running.Exited += (s, e) => BeginInvoke ((Action) (() => RunEnded (err)));
				miStart.Enabled = false; miStop.Enabled = true;
			}
			catch (Exception e) { MessageBox.Show (this, e.Message, "Onyx BASIC"); running = null; }
		}
		void RunEnded (string err)
		{
			running = null; miStart.Enabled = true; miStop.Enabled = false;
			Activate ();
			if (!File.Exists (err)) return;
			var t = File.ReadAllText (err).Split (new[] { '\n' }, 2);
			int.TryParse (t[0], out int line);
			if (line > 0) GotoProgramLine (line);
			MessageBox.Show (this, "Error in line " + line + ": " + (t.Length > 1 ? t[1] : ""), "Onyx BASIC", MessageBoxButtons.OK, MessageBoxIcon.Error);
		}

		// ---- find, go to, help, small dialogs ----------------------------------------------------------------------
		string findText = "";
		void OpFind (bool next)
		{
			if (!next || findText == "") { var t = Ask ("Find", "Find what:", findText); if (string.IsNullOrEmpty (t)) return; findText = t; }
			// the shown module from the caret, then the other modules
			int start = ed.SelectionStart + (next ? ed.SelectionLength : 0);
			int at = ed.Text.IndexOf (findText, start, StringComparison.OrdinalIgnoreCase);
			if (at >= 0) { ed.Select (at, findText.Length); ed.ScrollToCaret (); return; }
			Sync ();
			for (int k = 1; k <= mods.Count; k++)
			{
				int i = (cur + k) % mods.Count;
				int a = mods[i].Text.IndexOf (findText, StringComparison.OrdinalIgnoreCase);
				if (a >= 0) { ShowModule (i); ed.Select (a, findText.Length); ed.ScrollToCaret (); return; }
			}
			MessageBox.Show (this, "\"" + findText + "\" was not found.", "Find");
		}
		void OpGoto ()
		{
			var t = Ask ("Go to Line", "Line (in this module):", "");
			if (int.TryParse (t, out int l) && l > 0) GotoLine (l - 1);
		}
		void OpHelp ()
		{
			string p = Path.Combine (Settings.SdFolder, "apps", "qbasic.app", "help.txt");
			if (!File.Exists (p)) p = Path.Combine (AppDomain.CurrentDomain.BaseDirectory, "help.txt");
			string text = File.Exists (p) ? Latin1.Enc.GetString (File.ReadAllBytes (p)).Replace ("\r\n", "\n").Replace ("\n", "\r\n") : "help.txt was not found.";
			var f = new Form { Text = "Onyx BASIC - Keywords", ClientSize = new Size (820, 620), StartPosition = FormStartPosition.CenterParent, AutoScaleMode = AutoScaleMode.Dpi };
			var tb = new TextBox { Multiline = true, ReadOnly = true, Dock = DockStyle.Fill, ScrollBars = ScrollBars.Both, WordWrap = false, Font = Mono (10f), Text = text, BackColor = Color.White };
			f.Controls.Add (tb);
			f.Show (this);
			tb.Select (0, 0);
		}
		string Ask (string title, string prompt, string init)
		{
			using (var f = new Form { Text = title, FormBorderStyle = FormBorderStyle.FixedDialog, MinimizeBox = false, MaximizeBox = false, StartPosition = FormStartPosition.CenterParent, ClientSize = new Size (380, 110), AutoScaleMode = AutoScaleMode.Dpi })
			{
				var l = new Label { Text = prompt, Left = 10, Top = 10, Width = 360 };
				var t = new TextBox { Text = init ?? "", Left = 10, Top = 34, Width = 360 };
				var ok = new Button { Text = "OK", Left = 200, Top = 72, Width = 80, DialogResult = DialogResult.OK };
				var cancel = new Button { Text = "Cancel", Left = 290, Top = 72, Width = 80, DialogResult = DialogResult.Cancel };
				f.Controls.AddRange (new Control[] { l, t, ok, cancel });
				f.AcceptButton = ok; f.CancelButton = cancel;
				return f.ShowDialog (this) == DialogResult.OK ? t.Text : null;
			}
		}
	}
}
