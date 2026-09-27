// MainForm.cs -- the library: a tile per game (a picture of it and its name), one section per
// system, on a dark background, like Onyx's Game Library. Library > Add Games... / Add Folder...
// (or drop files and folders on the window) keeps their paths in library.txt; a double-click,
// Enter or a gamepad's Start (or A) plays.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Forms;

namespace NintendoEMU
{
	class MainForm : Form
	{
		static readonly Sys[] Order = { Sys.GC, Sys.N64, Sys.SNES, Sys.GBA, Sys.GBC, Sys.GB, Sys.NES };

		readonly ListView list = new ListView ();
		readonly ImageList images = new ImageList ();
		readonly MenuStrip menu = new MenuStrip ();
		readonly StatusStrip status = new StatusStrip ();
		readonly ToolStripStatusLabel statusText = new ToolStripStatusLabel ();
		readonly System.Windows.Forms.Timer padTimer = new System.Windows.Forms.Timer { Interval = 50 };
		ToolStripMenuItem miTiles, miDetails, miSound, miSmooth, miFull;
		readonly Queue<Game> thumbQueue = new Queue<Game> ();
		Thread thumbThread;
		readonly string startRom;
		uint lastPad; int padRepeat;

		public MainForm (string rom)
		{
			startRom = rom;
			Text = "NintendoEMU";
			ClientSize = new Size (Settings.GetInt ("Width", 1000), Settings.GetInt ("Height", 640));
			StartPosition = FormStartPosition.CenterScreen;
			try { Icon = Icon.ExtractAssociatedIcon (Application.ExecutablePath); } catch { }

			images.ImageSize = new Size (Library.TW, Library.TH);
			images.ColorDepth = ColorDepth.Depth32Bit;
			list.Dock = DockStyle.Fill;
			list.LargeImageList = images;
			list.BackColor = Color.FromArgb (0x14, 0x14, 0x14);
			list.ForeColor = Color.White;
			list.BorderStyle = BorderStyle.None;
			list.MultiSelect = true;
			list.HideSelection = false;
			list.FullRowSelect = true;
			list.Columns.Add ("Name", 320); list.Columns.Add ("System", 140); list.Columns.Add ("File", 520);
			list.ItemActivate += (s, e) => Play ();
			list.KeyDown += (s, e) => { if (e.KeyCode == Keys.Delete) { Remove (); e.Handled = true; } };
			list.AllowDrop = true;
			list.DragEnter += (s, e) => e.Effect = e.Data.GetDataPresent (DataFormats.FileDrop) ? DragDropEffects.Copy : DragDropEffects.None;
			list.DragDrop += (s, e) => AddPaths ((string[]) e.Data.GetData (DataFormats.FileDrop));
			list.SelectedIndexChanged += (s, e) => UpdateStatus ();

			BuildMenu ();
			status.Items.Add (statusText);
			Controls.Add (list);
			Controls.Add (menu);
			Controls.Add (status);
			MainMenuStrip = menu;
			SetView (Settings.Get ("View", "tiles") != "details");

			padTimer.Tick += (s, e) => PollPad ();
			padTimer.Start ();
			Shown += (s, e) =>
			{
				Fill ();
				if (startRom != null)
				{
					Library.Add (startRom);
					Fill ();
					var g = Library.Games.FirstOrDefault (x => string.Equals (x.Path, startRom, StringComparison.OrdinalIgnoreCase));
					if (g != null) GameForm.Play (g, this);
				}
				else if (Library.Games.Count == 0) statusText.Text = "No game yet: Library > Add Games... or Add Folder..., or drop files and folders here.";
			};
		}

		void BuildMenu ()
		{
			var lib = new ToolStripMenuItem ("&Library");
			lib.DropDownItems.Add (new ToolStripMenuItem ("&Play", null, (s, e) => Play ()) { ShortcutKeyDisplayString = "Enter" });
			lib.DropDownItems.Add (new ToolStripSeparator ());
			lib.DropDownItems.Add (new ToolStripMenuItem ("&Add Games...", null, (s, e) => AddGames ()) { ShortcutKeys = Keys.Control | Keys.O });
			lib.DropDownItems.Add (new ToolStripMenuItem ("Add &Folder...", null, (s, e) => AddFolder ()) { ShortcutKeys = Keys.Control | Keys.Shift | Keys.O });
			lib.DropDownItems.Add (new ToolStripMenuItem ("&Remove from Library", null, (s, e) => Remove ()) { ShortcutKeyDisplayString = "Del" });
			lib.DropDownItems.Add (new ToolStripMenuItem ("&Show in Explorer", null, (s, e) => ShowInExplorer ()));
			lib.DropDownItems.Add (new ToolStripSeparator ());
			lib.DropDownItems.Add (new ToolStripMenuItem ("E&xit", null, (s, e) => Close ()));
			var view = new ToolStripMenuItem ("&View");
			miTiles = new ToolStripMenuItem ("&Tiles", null, (s, e) => SetView (true));
			miDetails = new ToolStripMenuItem ("&Details", null, (s, e) => SetView (false));
			view.DropDownItems.Add (miTiles); view.DropDownItems.Add (miDetails);
			view.DropDownItems.Add (new ToolStripSeparator ());
			view.DropDownItems.Add (new ToolStripMenuItem ("&Refresh Pictures", null, (s, e) => RefreshPictures ()) { ShortcutKeys = Keys.F5 });
			var opt = new ToolStripMenuItem ("&Options");
			miSound = new ToolStripMenuItem ("&Sound", null, (s, e) => { miSound.Checked = !miSound.Checked; Settings.Set ("Sound", miSound.Checked); }) { Checked = Settings.GetBool ("Sound", true) };
			miSmooth = new ToolStripMenuItem ("S&mooth Picture", null, (s, e) => { miSmooth.Checked = !miSmooth.Checked; Settings.Set ("Smooth", miSmooth.Checked); }) { Checked = Settings.GetBool ("Smooth", false) };
			miFull = new ToolStripMenuItem ("Play &Full Screen", null, (s, e) => { miFull.Checked = !miFull.Checked; Settings.Set ("FullScreen", miFull.Checked); }) { Checked = Settings.GetBool ("FullScreen", false) };
			var res = new ToolStripMenuItem ("&3D Resolution (Nintendo 64, GameCube)");
			for (int k = 1; k <= 4; k++)
			{
				int kk = k;
				var it = new ToolStripMenuItem (k + "x the console's") { Checked = Settings.GetInt ("Scale3D", 2) == k };
				it.Click += (s, e) => { Settings.Set ("Scale3D", kk); foreach (ToolStripMenuItem o in res.DropDownItems) o.Checked = o == it; };
				res.DropDownItems.Add (it);
			}
			opt.DropDownItems.AddRange (new ToolStripItem[] { miSound, miSmooth, miFull, res });
			var help = new ToolStripMenuItem ("&Help");
			help.DropDownItems.Add (new ToolStripMenuItem ("&Controls...", null, (s, e) => ShowControls ()));
			help.DropDownItems.Add (new ToolStripMenuItem ("&About NintendoEMU...", null, (s, e) => MessageBox.Show (this,
				"NintendoEMU\n\nThe emulators of Onyx (Raspberry Pi 4) on Windows: Game Boy, Game Boy Color, Game Boy Advance, NES, " +
				"Super Nintendo, Nintendo 64 and GameCube -- the same cores as on the Pi.\n\nThe library: " + Store.File_ ("library.txt") +
				"\nThe saves: <game>.sav beside each game (the same files as on Onyx).", "About NintendoEMU")));
			menu.Items.AddRange (new ToolStripItem[] { lib, view, opt, help });
		}

		void ShowControls ()
		{
			string t = "";
			foreach (var s in new[] { Sys.GB, Sys.GBA, Sys.SNES, Sys.N64, Sys.GC })
				t += (s == Sys.GB ? "Game Boy / Color, NES" : Native.SystemName (s)) + "\n" + GameForm.Controls_ (s) + "\n\n";
			t += "In a game: P pause, F11 or Alt+Enter full screen (Esc back), F12 the speed, Ctrl+R reset, Ctrl+W close.";
			MessageBox.Show (this, t, "Controls");
		}

		void SetView (bool tiles)
		{
			list.View = tiles ? View.LargeIcon : View.Details;
			miTiles.Checked = tiles; miDetails.Checked = !tiles;
			Settings.Set ("View", tiles ? "tiles" : "details");
		}

		// the list, from the library: a group per system, by name
		void Fill ()
		{
			list.BeginUpdate ();
			list.Items.Clear (); list.Groups.Clear ();
			var groups = new Dictionary<Sys, ListViewGroup> ();
			foreach (var s in Order) { var g = new ListViewGroup (Native.SystemName (s)); groups[s] = g; list.Groups.Add (g); }
			foreach (var g in Library.Games.OrderBy (x => Array.IndexOf (Order, x.System)).ThenBy (x => x.Name, StringComparer.CurrentCultureIgnoreCase))
			{
				string key = g.Path.ToLowerInvariant ();
				if (!images.Images.ContainsKey (key))
				{
					var b = Library.CachedThumb (g);
					if (b == null) { b = Library.Label (g, g.Name, null); lock (thumbQueue) thumbQueue.Enqueue (g); }
					images.Images.Add (key, b);
				}
				var it = new ListViewItem (new[] { g.Name, g.SystemName, g.Path }, key) { Tag = g, Group = groups[g.System] };
				if (!g.Exists) it.ForeColor = Color.Gray;
				list.Items.Add (it);
			}
			list.EndUpdate ();
			UpdateStatus ();
			StartThumbs ();
		}

		void UpdateStatus ()
		{
			int n = Library.Games.Count, sel = list.SelectedItems.Count;
			statusText.Text = n + (n == 1 ? " game" : " games") + (sel == 1 ? "  -  " + ((Game) list.SelectedItems[0].Tag).Path : sel > 1 ? "  -  " + sel + " selected" : "");
		}

		// the pictures not made yet, made one by one by a worker thread (a 2D game runs 7 s unseen)
		void StartThumbs ()
		{
			if (thumbThread != null && thumbThread.IsAlive) return;
			thumbThread = new Thread (() =>
			{
				for (;;)
				{
					Game g;
					lock (thumbQueue) { if (thumbQueue.Count == 0) return; g = thumbQueue.Dequeue (); }
					if (!g.Exists) continue;
					Bitmap b = null;
					try { b = Library.MakeThumb (g); } catch { }
					if (b == null) continue;
					try
					{
						BeginInvoke ((Action) (() =>
						{
							string key = g.Path.ToLowerInvariant ();
							int i = images.Images.IndexOfKey (key);
							if (i >= 0) { images.Images[i] = b; list.Invalidate (); }
						}));
					}
					catch { return; }	// (the window is gone)
				}
			}) { IsBackground = true, Priority = ThreadPriority.BelowNormal, Name = "pictures" };
			thumbThread.Start ();
		}

		void RefreshPictures ()
		{
			lock (thumbQueue) foreach (var g in Library.Games) thumbQueue.Enqueue (g);	// (made again, over the cached ones)
			StartThumbs ();
		}

		Game Selected => list.SelectedItems.Count > 0 ? (Game) list.SelectedItems[0].Tag : list.FocusedItem?.Tag as Game;

		void Play ()
		{
			var g = Selected;
			if (g == null) return;
			if (!g.Exists)
			{
				if (MessageBox.Show (this, "The file is not there any more:\n" + g.Path + "\n\nRemove it from the library?", "NintendoEMU", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
				{ Library.Remove (g); Fill (); }
				return;
			}
			GameForm.Play (g, this);
		}

		void AddGames ()
		{
			using (var d = new OpenFileDialog
			{
				Title = "Add games to the library",
				Filter = "Games (" + Native.Extensions.Replace (";", ", ") + ")|" + Native.Extensions + "|All files (*.*)|*.*",
				Multiselect = true,
				InitialDirectory = Settings.Get ("LastFolder", "")
			})
			{
				if (d.ShowDialog (this) != DialogResult.OK) return;
				Settings.Set ("LastFolder", Path.GetDirectoryName (d.FileNames[0]));
				AddPaths (d.FileNames);
			}
		}
		void AddFolder ()
		{
			using (var d = new FolderBrowserDialog { Description = "A folder of games (its sub-folders too)", SelectedPath = Settings.Get ("LastFolder", "") })
			{
				if (d.ShowDialog (this) != DialogResult.OK) return;
				Settings.Set ("LastFolder", d.SelectedPath);
				AddPaths (new[] { d.SelectedPath });
			}
		}
		void AddPaths (string[] paths)
		{
			int n = 0;
			Cursor = Cursors.WaitCursor;
			foreach (var p in paths)
			{
				if (Directory.Exists (p)) n += Library.AddFolder (p);
				else if (Library.Add (p, false)) n++;
			}
			Library.Save ();
			Cursor = Cursors.Default;
			Fill ();
			statusText.Text = n == 0 ? "No new game (known extensions: " + Native.Extensions.Replace ("*", "") + ")" : n + (n == 1 ? " game added" : " games added");
		}
		void Remove ()
		{
			var sel = list.SelectedItems.Cast<ListViewItem> ().Select (i => (Game) i.Tag).ToList ();
			if (sel.Count == 0) return;
			string what = sel.Count == 1 ? sel[0].Name : sel.Count + " games";
			if (MessageBox.Show (this, "Remove " + what + " from the library?\n(The files stay on the disk.)", "NintendoEMU", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
			foreach (var g in sel) Library.Games.Remove (g);
			Library.Save ();
			Fill ();
		}
		void ShowInExplorer ()
		{
			var g = Selected;
			if (g == null) return;
			try { System.Diagnostics.Process.Start ("explorer.exe", "/select,\"" + g.Path + "\""); } catch { }
		}

		// a gamepad moves in the library (when it has the focus and no game runs)
		void PollPad ()
		{
			if (!ContainsFocus || GameForm.Current != null || list.Items.Count == 0) { lastPad = 0; return; }
			uint b;
			try { b = Native.ne_pad_buttons (0); } catch { padTimer.Stop (); return; }
			uint press = b & ~lastPad;
			bool held = (b & (Pad.Up | Pad.Down | Pad.Left | Pad.Right)) != 0 && b == lastPad;
			lastPad = b;
			if ((press & (Pad.Start | Pad.A)) != 0) { Play (); return; }
			if (held) { if (++padRepeat < 8) return; padRepeat = 6; } else padRepeat = 0;
			uint d = press != 0 ? press : held ? b : 0;
			Keys k = (d & Pad.Right) != 0 ? Keys.Right : (d & Pad.Left) != 0 ? Keys.Left : (d & Pad.Down) != 0 ? Keys.Down : (d & Pad.Up) != 0 ? Keys.Up : Keys.None;
			if (k == Keys.None) return;
			if (list.SelectedItems.Count == 0) { list.Items[0].Selected = true; list.Items[0].Focused = true; list.Items[0].EnsureVisible (); return; }
			var cur = list.FocusedItem ?? list.SelectedItems[0];
			var dir = k == Keys.Right ? SearchDirectionHint.Right : k == Keys.Left ? SearchDirectionHint.Left : k == Keys.Down ? SearchDirectionHint.Down : SearchDirectionHint.Up;
			ListViewItem next = list.View == View.Details
				? (k == Keys.Down && cur.Index + 1 < list.Items.Count ? list.Items[cur.Index + 1] : k == Keys.Up && cur.Index > 0 ? list.Items[cur.Index - 1] : null)
				: list.FindNearestItem (dir, cur.Position);
			if (next == null) return;
			list.SelectedItems.Clear ();
			next.Selected = true; next.Focused = true; next.EnsureVisible ();
		}

		protected override void OnFormClosing (FormClosingEventArgs e)
		{
			GameForm.Current?.Close ();
			if (WindowState == FormWindowState.Normal) { Settings.Set ("Width", ClientSize.Width); Settings.Set ("Height", ClientSize.Height); }
			base.OnFormClosing (e);
		}
	}
}
