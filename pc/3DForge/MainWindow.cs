// MainWindow.cs -- 3DForge's window: the menus, the tools' bar (a picture and its name each; the sketch's own while a
// sketch is drawn; Manufacture's), the 3D view, the timeline under it, the panel at the right, the status bar. What it
// shows comes from the core (forgecore.dll); after each thing done the window asks the core what changed (fc_poll).
// MIT licence (Onyx).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using Microsoft.Win32;

using static Forge.Lang;

namespace Forge
{
	class MainWindow : Window
	{
		readonly ViewHost view = new ViewHost ();
		readonly Timeline timeline = new Timeline ();
		readonly FormPanel panel = new FormPanel ();
		readonly StackPanel ribbonLeft = new StackPanel { Orientation = Orientation.Horizontal }, ribbonRight = new StackPanel { Orientation = Orientation.Horizontal };
		readonly TextBlock hint = new TextBlock { VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis, Foreground = new SolidColorBrush (Color.FromRgb (0x3C, 0x40, 0x4C)) };
		readonly TextBlock sTris = new TextBlock (), sGrid = new TextBlock { Cursor = Cursors.Hand }, sSnap = new TextBlock { Cursor = Cursors.Hand }, sDraw = new TextBlock ();
		readonly List<(ToggleButton b, Func<State, bool> on)> tiles = new List<(ToggleButton, Func<State, bool>)> ();
		Button bUndo, bRedo;
		readonly Dictionary<string, MenuItem> checks = new Dictionary<string, MenuItem> ();
		string ribbonMode = "";
		State st;
		static string lastDir;
		static readonly string[] SHAPE_NAMES = { Tr ("Box"), Tr ("Cylinder"), Tr ("Sphere"), Tr ("Torus"), Tr ("Pyramid"), Tr ("Prism"), Tr ("Taper") };
		static readonly int[] SHAPE_TOOLS = { T.BOX, T.CYL, T.SPHERE, T.TORUS, T.PYRAMID, T.PRISM, T.TAPER };
		static readonly int[] SHAPE_ICONS = { I.BOX, I.CYL, I.SPHERE, I.TORUS, I.PYRAMID, I.PRISM, I.TAPER };

		public MainWindow ()
		{
			Title = "3DForge"; Width = 1280; Height = 820; MinWidth = 900; MinHeight = 600;
			Background = (Brush) FindResource ("Face");
			try { Icon = new System.Windows.Media.Imaging.BitmapImage (new Uri ("pack://application:,,,/3dforge.ico")); } catch { }
			WindowStartupLocation = WindowStartupLocation.CenterScreen;
			UseLayoutRounding = true;
			lastDir = Settings.Get ("dir", Environment.GetFolderPath (Environment.SpecialFolder.MyDocuments));

			var root = new DockPanel ();
			var menu = BuildMenu (); DockPanel.SetDock (menu, Dock.Top); root.Children.Add (menu);
			// the tools' bar
			var bar = new Grid { Height = 64, Background = (Brush) FindResource ("Face") };
			bar.ColumnDefinitions.Add (new ColumnDefinition ()); bar.ColumnDefinitions.Add (new ColumnDefinition { Width = GridLength.Auto });
			ribbonLeft.Margin = new Thickness (8, 4, 0, 4); ribbonRight.Margin = new Thickness (0, 0, 12, 0); ribbonRight.VerticalAlignment = VerticalAlignment.Center;
			Grid.SetColumn (ribbonRight, 1); bar.Children.Add (ribbonLeft); bar.Children.Add (ribbonRight);
			var barB = new Border { Child = bar, BorderBrush = (Brush) FindResource ("Line"), BorderThickness = new Thickness (0, 1, 0, 1) };
			DockPanel.SetDock (barB, Dock.Top); root.Children.Add (barB);
			// the status bar
			var status = BuildStatus (); DockPanel.SetDock (status, Dock.Bottom); root.Children.Add (status);
			// the panel at the right
			var right = new Border { Child = panel, Width = 290, BorderBrush = (Brush) FindResource ("Line"), BorderThickness = new Thickness (1, 0, 0, 0) };
			DockPanel.SetDock (right, Dock.Right); root.Children.Add (right);
			// the view, the timeline under it
			var mid = new Grid { Margin = new Thickness (8, 8, 8, 8) };
			mid.RowDefinitions.Add (new RowDefinition ()); mid.RowDefinitions.Add (new RowDefinition { Height = GridLength.Auto });
			var vb = new Border { Child = view, BorderBrush = new SolidColorBrush (Color.FromRgb (0xB8, 0xBC, 0xC6)), BorderThickness = new Thickness (1), CornerRadius = new CornerRadius (2) };
			timeline.Margin = new Thickness (0, 6, 0, 0); Grid.SetRow (timeline, 1);
			mid.Children.Add (vb); mid.Children.Add (timeline);
			root.Children.Add (mid);
			Content = root;

			view.Changed += Sync; timeline.Changed += Sync; panel.Changed += Sync; panel.Command = RunCmd;
			timeline.Menu = TimelineMenu;
			PreviewKeyDown += OnKey;
			Closing += (s, e) => { if (!NoGuard && !Guard ()) e.Cancel = true; else Settings.Set ("dir", lastDir); };
			AllowDrop = true; Drop += OnDrop;
			CompositionTarget.Rendering += (s, e) => { Core.fc_tick (); Sync (); };
			Loaded += (s, e) => { Sync (Core.FL_PANEL | Core.FL_UI | Core.FL_VIEW); };
		}

		// ---- what changed: shown again -------------------------------------------------------------------------------------
		void Sync () => Sync (0);
		public FrameworkElement View => view;
		public Size ViewSize => new Size ((int) view.ActualWidth, (int) view.ActualHeight);
		public bool NoGuard;		// (a shot: the part is never saved)
		public void SyncNow () => Sync (Core.FL_UI | Core.FL_VIEW);
		void Sync (int extra)
		{
			int f = Core.fc_poll () | extra;
			if (f == 0) return;
			if ((f & (Core.FL_UI | Core.FL_PANEL | Core.FL_FIELDS)) != 0)
			{
				st = Core.State ();
				Shot.Trace ("sync ribbon"); Ribbon (); Shot.Trace ("timeline"); timeline.Refresh (st); Status (); Shot.Trace ("panel"); panel.Refresh (); Shot.Trace ("panel done");
				string p = Core.Path, name = p.Length > 0 ? Path.GetFileName (p) : Tr ("Untitled");
				Title = name + (st.Changed ? " •" : "") + " — 3DForge";
			}
			if ((f & Core.FL_VIEW) != 0) { Shot.Trace ("draw"); view.Draw (); Shot.Trace ("drawn"); }
			if ((f & Core.FL_PRINTFILE) != 0) Dispatcher.BeginInvoke (new Action (() => ShowRequest (Core.REQ_PRINTFILE)));
		}

		// ---- the tools' bar -------------------------------------------------------------------------------------------------
		void Ribbon ()
		{
			if (st.Mode != ribbonMode) BuildRibbon ();
			foreach (var t in tiles) t.b.IsChecked = t.on (st);
			if (bUndo != null) { bUndo.IsEnabled = st.Undo > 0 && !st.CamMode && !st.Sketching; bRedo.IsEnabled = st.Redo > 0 && !st.CamMode && !st.Sketching; }
			ribbonRight.Children.Clear ();
			if (st.Sketching) { AddAction (Tr ("Cancel"), Cmd.SK_CANCEL, false); AddAction (Tr ("Finish sketch"), Cmd.SK_FINISH, true); }
			else if (st.CamMode && st.Gen == 0)
			{
				if (st.CamDirty) { AddAction (Tr ("Cancel"), Cmd.CAM_REVERT, false); AddAction (Tr ("Validate"), Cmd.CAM_VALIDATE, true); }
				AddAction (Tr ("G-code"), Cmd.GCODE, true, I.GCODE);
			}
			else if (st.Resin) AddAction (Tr ("Print file"), Cmd.PR_FILE, true, I.GCODE);
			else if (!st.CamMode) AddAction (Tr ("Export"), Cmd.EXPORT, true, I.EXPORT);
			foreach (var kv in checks)
				kv.Value.IsChecked = kv.Key == "edges" ? st.Edges : kv.Key == "grid" ? st.Grid : kv.Key == "thru" ? st.SeeThrough : kv.Key == "snap" ? st.Snap : false;
		}
		void AddAction (string text, int cmd, bool accent, int icon = -1)
		{
			object content = text;
			if (icon >= 0)
			{
				var sp = new StackPanel { Orientation = Orientation.Horizontal };
				sp.Children.Add (Icons.Image (icon, 18, accent ? 0xFFFFFFu : Icons.INK, accent ? 0xFFFFFFu : Icons.ACC)); sp.Children.Add (new TextBlock { Text = text, Margin = new Thickness (8, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center });
				content = sp;
			}
			var b = new Button { Content = content, Style = (Style) FindResource (accent ? "AccentButton" : "Plain"), Margin = new Thickness (8, 0, 0, 0), MinWidth = 84, Height = 30 };
			b.Click += (s, e) => RunCmd (cmd);
			ribbonRight.Children.Add (b);
		}
		void BuildRibbon ()
		{
			ribbonMode = st.Mode; ribbonLeft.Children.Clear (); tiles.Clear ();
			ribbonLeft.Children.Add (FlatButton ("", Tr ("New (Ctrl+N)"), () => RunCmd (Cmd.NEW)));
			ribbonLeft.Children.Add (FlatButton ("", Tr ("Open (Ctrl+O)"), () => RunCmd (Cmd.OPEN)));
			ribbonLeft.Children.Add (FlatButton ("", Tr ("Save (Ctrl+S)"), () => RunCmd (Cmd.SAVE)));
			Sep ();
			ribbonLeft.Children.Add (bUndo = FlatButton ("", Tr ("Undo (Ctrl+Z)"), () => RunCmd (Cmd.UNDO)));
			ribbonLeft.Children.Add (bRedo = FlatButton ("", Tr ("Redo (Ctrl+Y)"), () => RunCmd (Cmd.REDO)));
			Sep ();
			if (!st.Sketching)		// the program's two halves: Design, Manufacture
			{
				var box = new Border { BorderBrush = (Brush) FindResource ("FieldLine"), BorderThickness = new Thickness (1), CornerRadius = new CornerRadius (5), Background = Brushes.White, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness (2, 0, 4, 0) };
				var sp = new StackPanel ();
				foreach (var (name, cmd, on) in new[] { (Tr ("Design"), Cmd.MODE_DESIGN, !st.CamMode), (Tr ("Manufacture"), Cmd.MODE_CAM, st.CamMode) })
				{
					var rb = new RadioButton { Content = name, IsChecked = on, Style = (Style) FindResource ("Seg"), GroupName = "mode", MinWidth = 96, FontSize = 12 };
					rb.Click += (s, e) => RunCmd (cmd); sp.Children.Add (rb);
				}
				box.Child = sp; ribbonLeft.Children.Add (box);
				Sep ();
			}
			if (st.Sketching)
			{
				Tile (I.LINE, Tr ("Line"), s => s.Tool == T.LINE, () => Core.fc_tool (T.LINE, 0));
				Tile (I.RECT, Tr ("Rectangle"), s => s.Tool == T.RECT, () => Core.fc_tool (T.RECT, 0));
				Tile (I.CIRCLE, Tr ("Circle"), s => s.Tool == T.CIRCLE, () => Core.fc_tool (T.CIRCLE, 0));
				Tile (I.ARC, Tr ("Arc"), s => s.Tool == T.ARC, () => Core.fc_tool (T.ARC, 0));
				Tile (I.ARC3, Tr ("3-pt arc"), s => s.Tool == T.ARC3, () => Core.fc_tool (T.ARC3, 0));
				Tile (I.SPLINE, Tr ("Spline"), s => s.Tool == T.SPLINE, () => Core.fc_tool (T.SPLINE, 0));
				Tile (I.POINT, Tr ("Point"), s => s.Tool == T.POINT, () => Core.fc_tool (T.POINT, 0));
				Tile (I.CLOSE, Tr ("Close"), s => false, () => Core.fc_cmd (Cmd.SK_CLOSE));
			}
			else if (st.CamMode && st.Gen == 0)
			{
				Tile (I.CAM_SETUP, Tr ("Setup"), s => s.CamPage == 0 && !s.CamSim, () => Core.fc_cmd (Cmd.CAM_SETUP));
				Tile (I.CAM_TOOL, Tr ("Tool"), s => s.CamPage == 1 && !s.CamSim, () => Core.fc_cmd (Cmd.CAM_TOOL));
				Sep ();
				Tile (I.CAM_CLEAR, Tr ("Clearing"), s => s.CamPage == 2 && s.CamOpKind == 0 && !s.CamSim, () => Core.fc_cmd (Cmd.CAM_CLEAR));
				Tile (I.CAM_CONTOUR, Tr ("Contour"), s => s.CamPage == 2 && s.CamOpKind == 1 && !s.CamSim, () => Core.fc_cmd (Cmd.CAM_CONTOUR));
				Sep ();
				Tile (I.PLAY, Tr ("Simulate"), s => s.CamSim, () => Core.fc_cmd (Cmd.CAM_SIM));
			}
			else if (st.CamMode)
			{
				Tile (I.PR_PLATE, Tr ("Setup"), s => s.CamPage == 0, () => Core.fc_cmd (Cmd.PR_SETUP));
				if (st.Gen == 1) Tile (I.PR_RESIN, Tr ("Resin"), s => s.CamPage == 1, () => Core.fc_cmd (Cmd.PR_RESIN));
				else Tile (I.PR_FIL, Tr ("Filament"), s => s.CamPage == 1, () => Core.fc_cmd (Cmd.PR_RESIN));
				Sep ();
				if (st.Gen == 1) { Tile (I.PR_SUP, Tr ("Supports"), s => s.CamPage == 2, () => Core.fc_cmd (Cmd.PR_SUPPORTS)); Sep (); }
				Tile (I.PR_LAYERS, Tr ("Layers"), s => s.CamPage == 3, () => Core.fc_cmd (Cmd.PR_LAYERS));
			}
			else
			{
				ShapesTile ();
				Tile (I.SKETCH, Tr ("Sketch"), s => s.Tool == T.SKETCH, () => Core.fc_tool (T.SKETCH, 1));
				Tile (I.EXTRUDE, Tr ("Extrude"), s => s.Tool == T.EXTRUDE, () => Core.fc_tool (T.EXTRUDE, 1));
				Sep ();
				Tile (I.FILLET, Tr ("Fillet"), s => s.Tool == T.FILLET, () => Core.fc_tool (T.FILLET, 1));
				Tile (I.CHAMFER, Tr ("Chamfer"), s => s.Tool == T.CHAMFER, () => Core.fc_tool (T.CHAMFER, 1));
				Tile (I.MOVE, Tr ("Move"), s => s.Tool == T.MOVE, () => Core.fc_tool (T.MOVE, 1));
				Sep ();
				Tile (I.UNION, Tr ("Union"), s => s.Tool == T.UNION, () => Core.fc_tool (T.UNION, 1));
				Tile (I.SUB, Tr ("Subtract"), s => s.Tool == T.SUB, () => Core.fc_tool (T.SUB, 1));
				Tile (I.INT, Tr ("Intersect"), s => s.Tool == T.INT, () => Core.fc_tool (T.INT, 1));
				Sep ();
				Tile (I.MEASURE, Tr ("Measure"), s => s.Tool == T.MEASURE, () => Core.fc_tool (T.MEASURE, 1));
			}
		}
		Button FlatButton (string glyph, string tip, Action a)
		{
			var b = new Button { Content = glyph, Style = (Style) FindResource ("Flat"), ToolTip = tip, VerticalAlignment = VerticalAlignment.Center };
			b.Click += (s, e) => a ();
			return b;
		}
		void Sep () => ribbonLeft.Children.Add (new Border { Width = 1, Margin = new Thickness (7, 8, 7, 8), Background = (Brush) FindResource ("Line") });
		ToggleButton Tile (int icon, string name, Func<State, bool> on, Action a, bool more = false)
		{
			var sp = new StackPanel ();
			sp.Children.Add (Icons.Image (icon, 26));
			var label = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness (0, 2, 0, 0) };
			label.Children.Add (new TextBlock { Text = name, FontSize = 11.5 });
			if (more) label.Children.Add (new TextBlock { Text = "", FontFamily = new FontFamily ("Segoe MDL2 Assets"), FontSize = 8, Margin = new Thickness (4, 3, 0, 0) });
			sp.Children.Add (label);
			var b = new ToggleButton { Content = sp, Style = (Style) FindResource ("Tile") };
			b.Click += (s, e) => { a (); Sync (); };
			ribbonLeft.Children.Add (b); tiles.Add ((b, on));
			return b;
		}
		// The shapes, unfolded under their button: a picture and its name each.
		void ShapesTile ()
		{
			ToggleButton tile = null;
			var pop = new Popup { StaysOpen = false, AllowsTransparency = true, Placement = PlacementMode.Bottom, PopupAnimation = PopupAnimation.Fade };
			var grid = new UniformGrid { Columns = 4 };
			for (int i = 0; i < SHAPE_TOOLS.Length; i++)
			{
				int t = SHAPE_TOOLS[i];
				var sp = new StackPanel { Width = 66 }; sp.Children.Add (Icons.Image (SHAPE_ICONS[i], 30)); sp.Children.Add (new TextBlock { Text = SHAPE_NAMES[i], HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness (0, 4, 0, 0), FontSize = 11.5 });
				var b = new Button { Content = sp, Style = (Style) FindResource ("Plain"), BorderBrush = Brushes.Transparent, Background = Brushes.Transparent, Padding = new Thickness (4, 8, 4, 6), Margin = new Thickness (2) };
				b.Click += (s, e) => { pop.IsOpen = false; Core.fc_tool (t, 0); Sync (); };
				grid.Children.Add (b);
			}
			pop.Child = new Border { Child = grid, Background = Brushes.White, BorderBrush = (Brush) FindResource ("FieldLine"), BorderThickness = new Thickness (1), CornerRadius = new CornerRadius (8), Padding = new Thickness (6), Margin = new Thickness (0, 0, 8, 8),
				Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 10, ShadowDepth = 2, Opacity = 0.25 } };
			tile = Tile (I.SHAPES, Tr ("Shapes"), s => T.Shape (s.Tool), () => { pop.PlacementTarget = tile; pop.IsOpen = true; }, true);
		}

		// ---- the status bar ---------------------------------------------------------------------------------------------------
		FrameworkElement BuildStatus ()
		{
			var g = new DockPanel { Height = 28, Background = (Brush) FindResource ("Face"), LastChildFill = true };
			var right = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness (0, 0, 10, 0) };
			foreach (var t in new[] { sTris, sGrid, sSnap, sDraw }) { t.Foreground = (Brush) FindResource ("Dim"); t.VerticalAlignment = VerticalAlignment.Center; t.FontSize = 11.5; t.Margin = new Thickness (14, 0, 0, 0); right.Children.Add (t); }
			right.Children.Add (new TextBlock { Text = "mm", Foreground = (Brush) FindResource ("Dim"), FontSize = 11.5, Margin = new Thickness (14, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center });
			sGrid.MouseLeftButtonUp += (s, e) => RunCmd (Cmd.GRID); sSnap.MouseLeftButtonUp += (s, e) => RunCmd (Cmd.SNAP);
			sGrid.ToolTip = Tr ("Show or hide the grid"); sSnap.ToolTip = Tr ("Snap to 1 mm, to points and angles");
			DockPanel.SetDock (right, Dock.Right); g.Children.Add (right);
			var info = Icons.Image (I.INFO, 16); info.Margin = new Thickness (10, 0, 8, 0);
			var left = new DockPanel (); DockPanel.SetDock (info, Dock.Left); left.Children.Add (info); left.Children.Add (hint);
			g.Children.Add (left);
			return new Border { Child = g, BorderBrush = (Brush) FindResource ("Line"), BorderThickness = new Thickness (0, 1, 0, 0) };
		}
		void Status ()
		{
			hint.Text = Core.Hint;
			sTris.Text = string.Format (Tr ("{0} triangles"), st.Tris.ToString ("N0", System.Globalization.CultureInfo.CurrentCulture));
			sGrid.Text = st.Grid ? Tr ("Grid 10 mm") : Tr ("Grid off"); sSnap.Text = st.Snap ? Tr ("Snap 1 mm") : Tr ("Snap off");
			sDraw.Text = Core.fc_gl () == 1 ? Tr ("Drawn by the graphics card") : string.Format (Tr ("Drawn by {0} threads"), st.Threads);
		}

		// ---- the menus --------------------------------------------------------------------------------------------------------
		Menu BuildMenu ()
		{
			var m = new Menu { Background = (Brush) FindResource ("Face"), Padding = new Thickness (4, 2, 4, 2) };
			MenuItem Top (string h) { var t = new MenuItem { Header = h }; m.Items.Add (t); return t; }
			MenuItem Item (MenuItem parent, string h, Action a, string key = null) { var it = new MenuItem { Header = h, InputGestureText = key }; it.Click += (s, e) => { a (); Sync (); }; parent.Items.Add (it); return it; }
			void Check (MenuItem parent, string h, string id, int cmd) { var it = Item (parent, h, () => RunCmd (cmd)); it.IsCheckable = false; checks[id] = it; }
			void SepM (MenuItem parent) => parent.Items.Add (new Separator ());

			var file = Top (TrMenu ("_File"));
			Item (file, TrMenu ("_New"), () => RunCmd (Cmd.NEW), "Ctrl+N"); Item (file, TrMenu ("_Open..."), () => RunCmd (Cmd.OPEN), "Ctrl+O");
			SepM (file); Item (file, TrMenu ("_Save"), () => RunCmd (Cmd.SAVE), "Ctrl+S"); Item (file, TrMenu ("Save _As..."), SaveAs, "Ctrl+Shift+S");
			SepM (file); Item (file, TrMenu ("_Export..."), () => RunCmd (Cmd.EXPORT), "Ctrl+E"); Item (file, TrMenu ("Import _Canvas..."), () => RunCmd (Cmd.CANVAS_IMPORT));
			SepM (file); Item (file, TrMenu ("E_xit"), Close, "Alt+F4");
			var edit = Top (TrMenu ("_Edit"));
			Item (edit, TrMenu ("_Undo"), () => RunCmd (Cmd.UNDO), "Ctrl+Z"); Item (edit, TrMenu ("_Redo"), () => RunCmd (Cmd.REDO), "Ctrl+Y");
			SepM (edit); Item (edit, TrMenu ("_Delete Step"), () => RunCmd (Cmd.DELETE), Tr ("Del")); Item (edit, TrMenu ("Roll _Back to the Step"), () => RunCmd (Cmd.ROLL_HERE)); Item (edit, TrMenu ("Roll to the _End"), () => RunCmd (Cmd.ROLL_END));
			var vw = Top (TrMenu ("_View"));
			Item (vw, TrMenu ("_Home"), () => RunCmd (Cmd.HOME)); Item (vw, TrMenu ("_Fit"), () => RunCmd (Cmd.FIT));
			SepM (vw); Item (vw, TrMenu ("_Top"), () => RunCmd (Cmd.TOP)); Item (vw, TrMenu ("F_ront"), () => RunCmd (Cmd.FRONT)); Item (vw, TrMenu ("_Right"), () => RunCmd (Cmd.RIGHT));
			SepM (vw); Check (vw, TrMenu ("_Edges"), "edges", Cmd.EDGES); Check (vw, TrMenu ("_Grid"), "grid", Cmd.GRID); Check (vw, TrMenu ("_See Through"), "thru", Cmd.SEETHROUGH); Check (vw, TrMenu ("S_nap"), "snap", Cmd.SNAP);
			var create = Top (TrMenu ("_Create"));
			for (int i = 0; i < SHAPE_TOOLS.Length; i++) { int t = SHAPE_TOOLS[i]; Item (create, t == T.TAPER ? Tr ("Tapered Prism") : SHAPE_NAMES[i], () => Core.fc_tool (t, 0)); }
			SepM (create); Item (create, TrMenu ("_Sketch"), () => Core.fc_tool (T.SKETCH, 0)); Item (create, TrMenu ("_Extrude"), () => Core.fc_tool (T.EXTRUDE, 0));
			var mod = Top (TrMenu ("_Modify"));
			Item (mod, TrMenu ("_Fillet"), () => Core.fc_tool (T.FILLET, 0)); Item (mod, TrMenu ("_Chamfer"), () => Core.fc_tool (T.CHAMFER, 0)); Item (mod, TrMenu ("_Move"), () => Core.fc_tool (T.MOVE, 0));
			SepM (mod); Item (mod, TrMenu ("_Union"), () => Core.fc_tool (T.UNION, 0)); Item (mod, TrMenu ("_Subtract"), () => Core.fc_tool (T.SUB, 0)); Item (mod, TrMenu ("_Intersect"), () => Core.fc_tool (T.INT, 0));
			SepM (mod); Item (mod, TrMenu ("M_easure"), () => Core.fc_tool (T.MEASURE, 0));
			var man = Top (TrMenu ("M_anufacture"));
			Item (man, TrMenu ("_Design"), () => RunCmd (Cmd.MODE_DESIGN)); Item (man, TrMenu ("_Manufacture"), () => RunCmd (Cmd.MODE_CAM));
			SepM (man); Item (man, TrMenu ("_Setup"), () => RunCmd (Cmd.CAM_SETUP)); Item (man, TrMenu ("_Tool and Machine"), () => RunCmd (Cmd.CAM_TOOL));
			SepM (man); Item (man, TrMenu ("New _Clearing"), () => RunCmd (Cmd.CAM_CLEAR)); Item (man, TrMenu ("New C_ontour"), () => RunCmd (Cmd.CAM_CONTOUR));
			SepM (man); Item (man, TrMenu ("S_imulate"), () => RunCmd (Cmd.CAM_SIM)); Item (man, TrMenu ("_G-code / Print File..."), () => RunCmd (Cmd.GCODE), "Ctrl+G");
			SepM (man); Item (man, TrMenu ("Process: _Milling"), () => RunCmd (Cmd.PROC_MILL)); Item (man, TrMenu ("Process: _Resin Printing"), () => RunCmd (Cmd.PROC_RESIN)); Item (man, TrMenu ("Process: _Filament Printing"), () => RunCmd (Cmd.PROC_FDM));
			SepM (man); Item (man, TrMenu ("Su_pports"), () => RunCmd (Cmd.PR_SUPPORTS)); Item (man, Tr ("Generate Supports"), () => RunCmd (Cmd.PR_GENERATE)); Item (man, TrMenu ("_Layers"), () => RunCmd (Cmd.PR_LAYERS));
			var help = Top (TrMenu ("_Help"));
			Item (help, TrMenu ("_About 3DForge"), About);
			return m;
		}
		void About ()
		{
			MessageBox.Show (this, Tr ("3DForge 1.0 for Windows\n\nA small parametric CAD: shapes, sketches and extrusions, unions, cuts, fillets and chamfers, every step kept in a history; ")
				+ Tr ("G-code for a router, the layers for a resin printer, the nozzle's path for a filament printer.\n\n")
				+ Tr ("The same program as Onyx's 3DForge, built for Windows. MIT licence.\nGeometry: Manifold (Apache-2.0), Clipper2 (BSL-1.0)."), Tr ("About 3DForge"), MessageBoxButton.OK, MessageBoxImage.Information);
		}

		// ---- the commands ------------------------------------------------------------------------------------------------------
		public void RunCmd (int cmd)
		{
			if (cmd < 0) { ShowRequest (-cmd); return; }		// (a field's Enter that asks for a dialog)
			switch (cmd)
			{
			case Cmd.NEW: if (Guard ()) Core.fc_new (); break;
			case Cmd.OPEN: Open (); break;
			case Cmd.SAVE: Save (); break;
			case Cmd.EXPORT: if (!st.Sketching && !st.CamMode) new ExportDialog (this).ShowDialog (); break;
			case Cmd.CANVAS_IMPORT: ImportCanvas (); break;
			case Cmd.UNDO: case Cmd.REDO: if (Keyboard.FocusedElement is TextBox tb) { if (cmd == Cmd.UNDO) tb.Undo (); else tb.Redo (); } else Core.fc_cmd (cmd); break;
			case Cmd.PR_PRESET:
			{
				var m = new ContextMenu (); var list = Core.List (2);
				for (int i = 0; i < list.Length; i++) { int k = i; var mi = new MenuItem { Header = list[i] }; mi.Click += (s, e) => { Core.fc_pick (2, k); Sync (); }; m.Items.Add (mi); }
				m.Placement = PlacementMode.MousePoint; m.IsOpen = true; break;
			}
			default: ShowRequest (Core.fc_cmd (cmd)); break;
			}
			Sync ();
		}
		void ShowRequest (int req)
		{
			if (req == Core.REQ_GCODE) new GcodeDialog (this).ShowDialog ();
			else if (req == Core.REQ_PRINTFILE) new PrintFileDialog (this).ShowDialog ();
			Sync ();
		}
		ContextMenu TimelineMenu (int i, int flags)
		{
			st = Core.State ();
			var m = new ContextMenu ();
			MenuItem Add (string h, int cmd, bool en = true) { var it = new MenuItem { Header = h, IsEnabled = en }; it.Click += (s, e) => RunCmd (cmd); m.Items.Add (it); return it; }
			if (st.Printer || st.Sketching) return null;
			if (st.CamMode) { if (i == 0) return null; Add (Tr ("Delete"), Cmd.CAM_DELETE); return m; }
			int n = Core.fc_tl_count (), upto = Core.fc_tl_upto ();
			if ((flags & 8) != 0) Add (Tr ("Edit sketch"), Cmd.EDIT_SKETCH);
			Add (Tr ("Roll back to here"), Cmd.ROLL_HERE, i + 1 != upto); Add (Tr ("Roll to the end"), Cmd.ROLL_END, upto < n);
			m.Items.Add (new Separator ()); Add (Tr ("Delete"), Cmd.DELETE);
			return m;
		}

		// ---- the keys ------------------------------------------------------------------------------------------------------------
		void OnKey (object s, KeyEventArgs e)
		{
			bool inText = Keyboard.FocusedElement is TextBox;
			var mod = Keyboard.Modifiers; Key k = e.Key == Key.System ? e.SystemKey : e.Key;
			if (mod == ModifierKeys.Control)
			{
				int c = k == Key.N ? Cmd.NEW : k == Key.O ? Cmd.OPEN : k == Key.S ? Cmd.SAVE : k == Key.E ? Cmd.EXPORT : k == Key.G ? Cmd.GCODE : -1;
				if (!inText && (k == Key.Z || k == Key.Y)) c = k == Key.Z ? Cmd.UNDO : Cmd.REDO;
				if (c >= 0) { RunCmd (c); e.Handled = true; }
				return;
			}
			if (mod == (ModifierKeys.Control | ModifierKeys.Shift) && k == Key.S) { SaveAs (); e.Handled = true; return; }
			if (mod != ModifierKeys.None) return;
			if (k == Key.Escape) { Core.fc_key (27); if (inText) Keyboard.ClearFocus (); Sync (); e.Handled = true; }
			else if (k == Key.Enter && !inText) { Core.fc_key (13); Sync (); e.Handled = true; }
			else if (k == Key.Delete && !inText) { Core.fc_key (46); Sync (); e.Handled = true; }
		}

		// ---- the files --------------------------------------------------------------------------------------------------------------
		bool Guard ()
		{
			var s = Core.State ();
			if (!s.Changed) return true;
			string p = Core.Path, name = p.Length > 0 ? Path.GetFileName (p) : Tr ("this part");
			var r = MessageBox.Show (this, string.Format (Tr ("Save the changes to {0}?"), name), Tr ("3DForge"), MessageBoxButton.YesNoCancel, MessageBoxImage.Question);
			if (r == MessageBoxResult.Cancel) return false;
			if (r == MessageBoxResult.Yes) return Save ();
			return true;
		}
		void Open ()
		{
			if (!Guard ()) return;
			var d = new OpenFileDialog { Filter = Tr ("3DForge parts (*.3df)|*.3df|All files|*.*"), InitialDirectory = lastDir };
			if (d.ShowDialog (this) == true) OpenPath (d.FileName, false);
		}
		public void OpenPath (string path, bool guard = true)
		{
			if (guard && !Guard ()) return;
			if (!Core.Open (path)) MessageBox.Show (this, Tr ("That file is not a 3DForge part (.3df)."), Tr ("Open"), MessageBoxButton.OK, MessageBoxImage.Warning);
			else lastDir = Path.GetDirectoryName (path);
			Sync (Core.FL_PANEL | Core.FL_UI | Core.FL_VIEW);
		}
		bool Save ()
		{
			string p = Core.Path;
			if (p.Length == 0) return SaveAsPath ();
			if (!Core.Save (p)) { MessageBox.Show (this, Tr ("The file could not be written."), Tr ("Save"), MessageBoxButton.OK, MessageBoxImage.Warning); return false; }
			Sync (Core.FL_UI); return true;
		}
		void SaveAs () => SaveAsPath ();
		bool SaveAsPath ()
		{
			string p = Core.Path;
			var d = new SaveFileDialog { Filter = Tr ("3DForge parts (*.3df)|*.3df"), DefaultExt = ".3df", AddExtension = true, InitialDirectory = p.Length > 0 ? Path.GetDirectoryName (p) : lastDir, FileName = p.Length > 0 ? Path.GetFileName (p) : Tr ("part.3df") };
			if (d.ShowDialog (this) != true) return false;
			lastDir = Path.GetDirectoryName (d.FileName);
			if (!Core.Save (d.FileName)) { MessageBox.Show (this, Tr ("The file could not be written."), Tr ("Save"), MessageBoxButton.OK, MessageBoxImage.Warning); return false; }
			Sync (Core.FL_UI); return true;
		}
		static readonly string PICTURES = Tr ("Pictures|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff|All files|*.*");
		static bool IsPicture (string p) => new[] { ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff" }.Contains (Path.GetExtension (p).ToLowerInvariant ());
		void ImportCanvas ()
		{
			if (st.CamMode) { Core.SetHint (Tr ("A canvas belongs to Design.")); return; }
			var d = new OpenFileDialog { Filter = PICTURES, InitialDirectory = lastDir };
			if (d.ShowDialog (this) == true) AddCanvas (d.FileName);
		}
		void AddCanvas (string path)
		{
			string err = Core.CanvasAdd (path);
			if (err.Length > 0) MessageBox.Show (this, err, Tr ("Canvas"), MessageBoxButton.OK, MessageBoxImage.Warning);
			Sync ();
		}
		void OnDrop (object s, DragEventArgs e)
		{
			if (!(e.Data.GetData (DataFormats.FileDrop) is string[] files) || files.Length == 0) return;
			if (IsPicture (files[0])) AddCanvas (files[0]); else OpenPath (files[0]);
		}
		public static string LastDir { get => lastDir; set => lastDir = value; }
	}

	// What is kept between two runs: %APPDATA%\3DForge\window.ini (key=value).
	static class Settings
	{
		static string File => Path.Combine (App.ConfigDir, "window.ini");
		static Dictionary<string, string> load ()
		{
			var d = new Dictionary<string, string> ();
			try { foreach (var l in System.IO.File.ReadAllLines (File)) { int i = l.IndexOf ('='); if (i > 0) d[l.Substring (0, i)] = l.Substring (i + 1); } } catch { }
			return d;
		}
		public static string Get (string k, string dflt) { var d = load (); return d.TryGetValue (k, out var v) && v.Length > 0 ? v : dflt; }
		public static void Set (string k, string v)
		{
			var d = load (); d[k] = v ?? "";
			try { System.IO.File.WriteAllLines (File, d.Select (p => p.Key + "=" + p.Value)); } catch { }
		}
	}
}
