// Control.cs -- 3DForge driven by another program (an assistant through `3DForge.exe --mcp`, Mcp.cs): the window listens
// on the named pipe \\.\pipe\3DForge, a request a line ({"tool": name, "args": {...}}), an answer a line
// ({"text": ..., "image": base64 PNG?, "error": ...}). Each action is done on the window's thread, as the user's would
// be -- a tool chosen, the pointer in the view, a value typed in the panel --, never through a dialog (nothing waits
// on a click); the answer says what the window says then.
// MIT licence (Onyx).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Text;
using System.Threading;

using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Forge
{
	static class Control
	{
		public const string PIPE = "3DForge";
		static MainWindow W;
		static readonly JsonCodec Json = new JsonCodec ();

		public static void Start (MainWindow w)
		{
			W = w;
			var t = new Thread (Serve) { IsBackground = true, Name = "3DForge control" }; t.Start ();
		}
		static void Serve ()
		{
			while (true)
			{
				NamedPipeServerStream p;
				try { p = new NamedPipeServerStream (PIPE, PipeDirection.InOut, 4, PipeTransmissionMode.Byte, PipeOptions.Asynchronous); }
				catch (IOException) { return; }		// (another 3DForge has it)
				try { p.WaitForConnection (); } catch { p.Dispose (); continue; }
				var c = p; new Thread (() => Client (c)) { IsBackground = true }.Start ();
			}
		}
		static void Client (NamedPipeServerStream p)
		{
			using (p)
			using (var r = new StreamReader (p, new UTF8Encoding (false)))
			using (var w = new StreamWriter (p, new UTF8Encoding (false)) { AutoFlush = true })
			{
				string line;
				try
				{
					while ((line = r.ReadLine ()) != null)
					{
						Dictionary<string, object> res;
						try
						{
							var req = Json.Deserialize<Dictionary<string, object>> (line);
							var args = req.TryGetValue ("args", out var a) && a is Dictionary<string, object> d ? d : new Dictionary<string, object> ();
							res = Do ((string) req["tool"], args);
						}
						catch (Exception e) { res = new Dictionary<string, object> { ["error"] = e.Message }; }
						w.WriteLine (Json.Serialize (res));
					}
				}
				catch (IOException) { }
			}
		}

		// ---- the actions ------------------------------------------------------------------------------------------------
		class Fail : Exception { public Fail (string m) : base (m) { } }
		// (on the window's thread; an error comes back here, never to the window's own handler, which would say it in a box)
		static T UI<T> (Func<T> f)
		{
			Exception err = null;
			var v = W.Dispatcher.Invoke (() => { try { var r = f (); W.SyncNow (); return r; } catch (Exception e) { err = e; return default (T); } });
			if (err != null) throw err;
			return v;
		}
		static void UI (Action f) => UI (() => { f (); return 0; });
		static string S (Dictionary<string, object> a, string k, string dflt = null) => a.TryGetValue (k, out var v) && v != null ? Convert.ToString (v, CultureInfo.InvariantCulture) : dflt;
		static double D (Dictionary<string, object> a, string k, double dflt = 0) => a.TryGetValue (k, out var v) && v != null ? Convert.ToDouble (v, CultureInfo.InvariantCulture) : dflt;
		static bool B (Dictionary<string, object> a, string k, bool dflt = false) => a.TryGetValue (k, out var v) && v != null ? Convert.ToBoolean (v) : dflt;
		// (a name as shown -- in the user's language -- or as in English: both point at the same control)
		static bool Eq (string a, string b) => string.Equals (a, b, StringComparison.OrdinalIgnoreCase);
		static bool Same (string shown, string given) => given != null && (Eq (shown, given) || Eq (shown, Lang.Tr (given)));
		static string Num (double v) => v.ToString ("0.###", CultureInfo.InvariantCulture);

		static Dictionary<string, object> Do (string tool, Dictionary<string, object> a)
		{
			switch (tool)
			{
			case "get_state": return Text (UI (() => Json.Serialize (StateObj (true))));
			case "get_panel": return Text (UI (() => Json.Serialize (PanelObj ())));
			case "screenshot": return Image (UI (() => Shoot (S (a, "what", "window"))));
			case "command": UI (() => Command (S (a, "name"))); return Done ();
			case "set_tool": UI (() => SetTool (S (a, "tool"))); return Done ();
			case "key":
			{
				string k = S (a, "key", "").ToLowerInvariant ();
				int code = k == "escape" || k == "esc" ? 27 : k == "enter" || k == "return" ? 13 : k == "delete" || k == "del" ? 46 : throw new Fail ("key: escape, enter or delete");
				UI (() => Core.fc_key (code)); return Done ();
			}
			case "set_field": UI (() => SetField (S (a, "name"), S (a, "value"), (int) D (a, "index", 0))); return Done ();
			case "set_option": UI (() => SetOption (S (a, "label"), S (a, "value"))); return Done ();
			case "press": { int req = UI (() => Press (S (a, "button"))); return Done (req); }
			case "pick": UI (() => Pick (S (a, "list"), S (a, "item"))); return Done ();
			case "view_mouse": UI (() => Mouse (a)); return Done ();
			case "look": UI (() => Core.fc_view_look (D (a, "azimuth", -58), D (a, "elevation", 28))); return Done ();
			case "timeline": UI (() => Timeline (S (a, "action", "select"), (int) D (a, "index"))); return Done ();
			case "make_shape": return MakeShape (a);
			case "new_part":
				UI (() => { if (Core.State ().Changed && !B (a, "discard_changes")) throw new Fail ("The part has unsaved changes: save it first, or pass discard_changes."); Core.fc_new (); });
				return Done ();
			case "open_file":
			{
				string path = S (a, "path") ?? throw new Fail ("path?");
				UI (() => { if (Core.State ().Changed && !B (a, "discard_changes")) throw new Fail ("The part has unsaved changes: save it first, or pass discard_changes."); if (!Core.Open (Path.GetFullPath (path))) throw new Fail ("That file is not a 3DForge part (.3df)."); });
				return Done ();
			}
			case "save_file":
			{
				string path = S (a, "path");
				UI (() => { string p = path != null ? Path.GetFullPath (path) : Core.Path; if (p.Length == 0) throw new Fail ("The part has no file yet: give a path."); if (!p.EndsWith (".3df", StringComparison.OrdinalIgnoreCase)) p += ".3df"; if (!Core.Save (p)) throw new Fail ("The file could not be written."); });
				return Done ();
			}
			case "add_canvas": { string err = UI (() => Core.CanvasAdd (Path.GetFullPath (S (a, "path") ?? ""))); if (err.Length > 0) throw new Fail (err); return Done (); }
			case "export": return Export (a);
			case "write_gcode": return Gcode (S (a, "path"));
			case "write_print_file": return PrintFile (S (a, "path"));
			case "close_window":
				UI (() => { if (Core.State ().Changed && !B (a, "discard_changes")) throw new Fail ("The part has unsaved changes: save it first, or pass discard_changes."); W.NoGuard = true; W.Dispatcher.BeginInvoke (new Action (W.Close)); return 0; });
				return Text ("closed");
			}
			throw new Fail ("No such tool: " + tool);
		}
		static Dictionary<string, object> Text (string s) => new Dictionary<string, object> { ["text"] = s };
		static Dictionary<string, object> Image (string png) => new Dictionary<string, object> { ["image"] = png };
		// After an action: what the window says now (the hint, the tool, the step being made).
		static Dictionary<string, object> Done (int req = 0)
		{
			var s = UI (() => StateObj (false));
			if (req == Core.REQ_GCODE) s["note"] = "Use write_gcode to write the G-code.";
			if (req == Core.REQ_PRINTFILE) s["note"] = "Use write_print_file to write the print file.";
			return Text (Json.Serialize (s));
		}

		// ---- the state ----
		static readonly string[] TOOLS = { "select", "box", "cylinder", "sketch", "extrude", "fillet", "chamfer", "move", "union", "subtract", "intersect", "measure",
			"pyramid", "prism", "taper", "torus", "sphere", "line", "rectangle", "circle", "arc", "arc3", "spline", "point", "midline" };
		static Dictionary<string, object> StateObj (bool full)
		{
			var st = Core.State ();
			var o = new Dictionary<string, object>
			{
				["mode"] = st.Sketching ? "sketch" : !st.CamMode ? "design" : st.Gen == 0 ? "manufacture: milling" : st.Gen == 1 ? "manufacture: resin printing" : "manufacture: filament printing",
				["tool"] = st.Tool >= 0 && st.Tool < TOOLS.Length ? TOOLS[st.Tool] : st.Tool.ToString (),
				["step"] = st.Step, ["making"] = st.HasPend,
				["hint"] = Core.Hint,
				["triangles"] = st.Tris, ["unsaved"] = st.Changed,
			};
			var head = Form ().FirstOrDefault (f => f[0] == "H");
			if (head != null) o["panel"] = head[2] + (head[3].Length > 0 ? " (" + head[3] + (head[4].Length > 0 ? ", " + head[4] : "") + ")" : "");
			if (!full) return o;
			o["file"] = Core.Path; o["view_size"] = new[] { W.ViewSize.Width, W.ViewSize.Height };
			o["bodies"] = Core.List (0);
			o["selected_body"] = st.SelBody; o["selected_step"] = st.SelFeat;
			var tl = new List<object> (); int n = Core.fc_tl_count ();
			for (int i = 0; i < n; i++)
			{
				int fl = Core.TlItem (i, out _, out string t);
				tl.Add (new Dictionary<string, object> { ["index"] = i, ["text"] = t.Replace ("  \u00B7  ", ": "), ["selected"] = (fl & 1) != 0, ["rolled_back"] = (fl & 2) != 0, ["failed"] = (fl & 4) != 0 });
			}
			o[st.Sketching ? "sketch_elements" : st.CamMode ? "operations" : "history"] = tl;
			if (!st.Sketching && !st.CamMode) o["rebuilt_up_to"] = Core.fc_tl_upto ();
			o["edges_shown"] = st.Edges; o["grid_shown"] = st.Grid; o["see_through"] = st.SeeThrough; o["snap"] = st.Snap;
			return o;
		}
		static List<string[]> Form () => Core.Form ().Split ('\x1E').Where (s => s.Length > 0).Select (s => s.Split ('\x1F')).ToList ();
		static string F (string[] f, int i) => i < f.Length ? f[i] : "";
		// The panel as a list: what each control is, its value.
		static List<object> PanelObj ()
		{
			var o = new List<object> (); string section = "";
			foreach (var f in Form ())
			{
				var d = new Dictionary<string, object> ();
				switch (f[0])
				{
				case "H": d["title"] = F (f, 2); d["subtitle"] = (F (f, 3) + " " + F (f, 4)).Trim (); break;
				case "L": if (F (f, 3) == "1") { section = F (f, 1); d["section"] = section; } else d["note"] = F (f, 1); break;
				case "F": d["field"] = F (f, 2); d["value"] = F (f, 3); if (F (f, 4).Length > 0) d["unit"] = F (f, 4); if (section.Length > 0) d["section"] = section; if (F (f, 5) == "1") d["focused"] = true; break;
				case "T": d["text_field"] = F (f, 2); d["value"] = F (f, 3); break;
				case "C": d["check"] = F (f, 2); d["checked"] = F (f, 3) == "1"; break;
				case "S": case "D": d["choice"] = F (f, 2).Length > 0 ? F (f, 2) : section; d["options"] = f.Skip (4).ToArray (); int k = int.Parse (F (f, 3)); d["value"] = k >= 0 && k + 4 < f.Length ? f[k + 4] : ""; break;
				case "B": d["button"] = F (f, 2); break;
				case "P": d["body"] = F (f, 1); d["process"] = F (f, 2); d["note"] = "pick: list body / process"; break;
				case "O": d["choice"] = "Operation"; d["options"] = new[] { "New body", "Union", "Subtract", "Intersect" }; d["value"] = new[] { "New body", "Union", "Subtract", "Intersect" }[int.Parse (F (f, 1))]; break;
				case "W": d["choice"] = "Colour"; d["options"] = new[] { "1", "2", "3", "4", "5" }; d["value"] = (Array.IndexOf (f.Skip (2).ToArray (), F (f, 1)) + 1).ToString (); break;
				case "R": d["note"] = F (f, 1) + " " + F (f, 2); break;
				case "K": d["measure"] = F (f, 1); d["value"] = F (f, 2); break;
				case "I": d["note"] = string.Join (" ", f.Skip (1)); break;
				case "X": d[F (f, 1) == "0" ? "ok" : "warning"] = F (f, 2); break;
				case "Y": d[F (f, 1) == "0" ? "ok" : "warning"] = (F (f, 2) + " \u2014 " + F (f, 4)).Trim (' ', '\u2014'); break;
				case "G": d["choice"] = "Origin"; d["value"] = F (f, 1); d["note"] = "0..26: i + 3*(2-row) + 9*(2-level), level 0 top"; break;
				}
				if (d.Count > 0) o.Add (d);
			}
			return o;
		}

		// ---- the actions' details ----
		static readonly Dictionary<string, int> CMDS = new Dictionary<string, int>
		{
			["undo"] = Cmd.UNDO, ["redo"] = Cmd.REDO, ["ok"] = Cmd.OK, ["cancel"] = Cmd.CANCEL, ["delete"] = Cmd.DELETE, ["edit_sketch"] = Cmd.EDIT_SKETCH,
			["roll_back_here"] = Cmd.ROLL_HERE, ["roll_to_end"] = Cmd.ROLL_END, ["delete_element"] = Cmd.DEL_ELEMENT, ["toggle_construction"] = Cmd.SK_CONS, ["start_sketch"] = Cmd.SK_START,
			["finish_sketch"] = Cmd.SK_FINISH, ["cancel_sketch"] = Cmd.SK_CANCEL, ["close_outline"] = Cmd.SK_CLOSE,
			["design"] = Cmd.MODE_DESIGN, ["manufacture"] = Cmd.MODE_CAM, ["setup"] = Cmd.CAM_SETUP, ["tool_and_machine"] = Cmd.CAM_TOOL,
			["add_clearing"] = Cmd.CAM_CLEAR, ["add_contour"] = Cmd.CAM_CONTOUR, ["simulate"] = Cmd.CAM_SIM, ["validate"] = Cmd.CAM_VALIDATE, ["revert"] = Cmd.CAM_REVERT,
			["delete_operation"] = Cmd.CAM_DELETE, ["print_setup"] = Cmd.PR_SETUP, ["resin_or_filament"] = Cmd.PR_RESIN, ["supports"] = Cmd.PR_SUPPORTS, ["layers"] = Cmd.PR_LAYERS,
			["generate_supports"] = Cmd.PR_GENERATE, ["remove_supports"] = Cmd.PR_CLEAR, ["delete_canvas"] = Cmd.CANVAS_DELETE,
			["home"] = Cmd.HOME, ["fit"] = Cmd.FIT, ["view_top"] = Cmd.TOP, ["view_front"] = Cmd.FRONT, ["view_right"] = Cmd.RIGHT,
			["toggle_edges"] = Cmd.EDGES, ["toggle_grid"] = Cmd.GRID, ["toggle_see_through"] = Cmd.SEETHROUGH, ["toggle_snap"] = Cmd.SNAP,
			["process_milling"] = Cmd.PROC_MILL, ["process_resin"] = Cmd.PROC_RESIN, ["process_filament"] = Cmd.PROC_FDM,
		};
		public static string CommandNames => string.Join (", ", CMDS.Keys);
		public static string ToolNames => string.Join (", ", TOOLS);
		static void Command (string name)
		{
			if (name == null || !CMDS.TryGetValue (name.ToLowerInvariant (), out int c)) throw new Fail ("No such command; they are: " + CommandNames);
			Core.fc_cmd (c);
		}
		static void SetTool (string name)
		{
			int t = Array.IndexOf (TOOLS, (name ?? "").ToLowerInvariant ());
			if (t < 0) throw new Fail ("No such tool; they are: " + ToolNames);
			Core.fc_tool (t, 0);
		}
		// A field of the panel, by its name (the index-th of that name; a section may be given: "Scale/Along X").
		static void SetField (string name, string value, int index)
		{
			if (name == null || value == null) throw new Fail ("name and value?");
			string sec = null, nm = name; int sl = name.IndexOf ('/'); if (sl > 0) { sec = name.Substring (0, sl).Trim (); nm = name.Substring (sl + 1).Trim (); }
			string section = ""; int k = 0;
			foreach (var f in Form ())
			{
				if (f[0] == "L" && F (f, 3) == "1") section = F (f, 1);
				if ((f[0] == "F" || f[0] == "T") && Same (F (f, 2), nm) && (sec == null || Same (section, sec)))
				{
					if (k++ < index) continue;
					Core.FormText (int.Parse (f[1]), value.Replace (',', '.')); return;
				}
			}
			// (a cylinder's or an extrusion's size is called Height going up, Depth going down: either name, the way given)
			bool h = Same (Lang.Tr ("Height"), nm) || Eq (nm, "Height"), d = Same (Lang.Tr ("Depth"), nm) || Eq (nm, "Depth");
			if ((h || d) && sec == null && index == 0)
			{
				string other = h ? "Depth" : "Height";
				foreach (var f in Form ())
					if (f[0] == "F" && Same (F (f, 2), other)) { double v = Math.Abs (double.Parse (value.Replace (',', '.'), CultureInfo.InvariantCulture)); Core.FormText (int.Parse (f[1]), (d ? "-" : "") + Num (v)); return; }
			}
			throw new Fail ("No field \"" + name + "\" in the panel now (get_panel lists them).");
		}
		// A check box (value true / false), a choice (value: one of its options), the operation, the colour, the origin.
		static void SetOption (string label, string value)
		{
			if (label == null || value == null) throw new Fail ("label and value?");
			string section = "";
			foreach (var f in Form ())
			{
				if (f[0] == "L" && F (f, 3) == "1") section = F (f, 1);
				bool named (string n) => Same (n, label) || Same (label, n);
				if (f[0] == "C" && named (F (f, 2))) { bool on = value.Equals ("true", StringComparison.OrdinalIgnoreCase) || value == "1" || value.Equals ("on", StringComparison.OrdinalIgnoreCase); Core.fc_form_check (int.Parse (f[1]), on ? 1 : 0); return; }
				if ((f[0] == "S" || f[0] == "D") && named (F (f, 2).Length > 0 ? F (f, 2) : section))
				{
					int i = Array.FindIndex (f.Skip (4).ToArray (), o => Same (o, value));
					if (i < 0) throw new Fail (label + ": one of " + string.Join (", ", f.Skip (4)));
					if (f[0] == "S") Core.fc_form_seg (int.Parse (f[1]), i); else Core.fc_form_drop (int.Parse (f[1]), i);
					return;
				}
				if (f[0] == "O" && named ("Operation"))
				{
					int i = Array.FindIndex (new[] { "New body", "Union", "Subtract", "Intersect" }, o => Eq (o, value) || Eq (Lang.Tr (o), value));
					if (i < 0) throw new Fail ("Operation: New body, Union, Subtract or Intersect"); Core.fc_form_op (i); return;
				}
				if (f[0] == "W" && named ("Colour")) { Core.fc_form_colour (int.Parse (value) - 1); return; }
				if (f[0] == "G" && named ("Origin")) { Core.fc_form_origin (int.Parse (value)); return; }
			}
			throw new Fail ("No option \"" + label + "\" in the panel now (get_panel lists them).");
		}
		// A button of the panel, by its text. -> what it asks the window to show (G-code, print file), else 0.
		static int Press (string text)
		{
			foreach (var f in Form ())
				if (f[0] == "B" && Same (F (f, 2), text))
				{
					int c = int.Parse (f[1]);
					if (c == Cmd.PR_PRESET) throw new Fail ("That button opens a list: use pick with list \"resin\".");
					if (c == Cmd.PR_FILE) return Core.REQ_PRINTFILE;
					return Core.fc_cmd (c);
				}
			if (string.Equals (text, "Enter", StringComparison.OrdinalIgnoreCase)) return Core.fc_form_enter ();
			throw new Fail ("No button \"" + text + "\" in the panel now.");
		}
		// One of the lists a panel's button opens: the body cut or printed, the process, the resin.
		static void Pick (string list, string item)
		{
			int which = list == "body" ? 0 : list == "process" ? 1 : list == "resin" ? 2 : throw new Fail ("list: body, process or resin");
			var items = Core.List (which);
			int i = int.TryParse (item, out int n) ? n : Array.FindIndex (items, x => x.IndexOf (item ?? "\u0001", StringComparison.OrdinalIgnoreCase) >= 0);
			if (i < 0 || i >= items.Length) throw new Fail (list + ": one of " + string.Join (" | ", items));
			Core.fc_pick (which, i);
		}
		// The pointer in the view (its pixels from the top left corner): move, click, right_click, drag (to x2, y2), wheel.
		static void Mouse (Dictionary<string, object> a)
		{
			string act = S (a, "action", "click"); int x = (int) D (a, "x"), y = (int) D (a, "y"), m = B (a, "shift") ? 2 : 0;
			switch (act)
			{
			case "move": Core.fc_view_mouse (x, y, 0, 0, m); break;
			case "click": Core.fc_view_mouse (x, y, 0, 0, m); Core.fc_view_mouse (x, y, 1, 0, m); Core.fc_view_mouse (x, y, 0, 0, m); break;
			case "double_click": for (int i = 0; i < 2; i++) { Core.fc_view_mouse (x, y, 0, 0, m); Core.fc_view_mouse (x, y, 1, 0, m); Core.fc_view_mouse (x, y, 0, 0, m); } break;
			case "right_click": Core.fc_view_mouse (x, y, 2, 0, m); Core.fc_view_mouse (x, y, 0, 0, m); break;
			case "drag":
			{
				int x2 = (int) D (a, "x2"), y2 = (int) D (a, "y2"), b = S (a, "button", "left") == "right" ? 2 : 1;
				Core.fc_view_mouse (x, y, 0, 0, m); Core.fc_view_mouse (x, y, b, 0, m);
				for (int i = 1; i <= 16; i++) Core.fc_view_mouse (x + (x2 - x) * i / 16, y + (y2 - y) * i / 16, b, 0, m);
				Core.fc_view_mouse (x2, y2, 0, 0, m); break;
			}
			case "wheel": Core.fc_view_mouse (x, y, 0, (int) D (a, "notches", 1), m); break;
			default: throw new Fail ("action: move, click, double_click, right_click, drag or wheel");
			}
		}
		static void Timeline (string act, int i)
		{
			if (act == "select") Core.fc_tl_pick (i, 0, 0);
			else if (act == "open") Core.fc_tl_pick (i, 1, 0);
			else if (act == "roll_to") Core.fc_tl_set_upto (i);
			else throw new Fail ("action: select, open (a sketch) or roll_to (the part rebuilt up to that many steps)");
		}
		// A shape made from its values, without the pointer: the tool, its fields, the operation, OK.
		static Dictionary<string, object> MakeShape (Dictionary<string, object> a)
		{
			string shape = S (a, "shape", "box").ToLowerInvariant ();
			if (!new[] { "box", "cylinder", "sphere", "torus", "pyramid", "prism", "taper" }.Contains (shape)) throw new Fail ("shape: box, cylinder, sphere, torus, pyramid, prism or taper");
			var vals = a.TryGetValue ("values", out var v) && v is Dictionary<string, object> d ? d : new Dictionary<string, object> ();
			int before = UI (() => Core.State ().Feats);
			UI (() =>
			{
				SetTool (shape);
				try
				{
					foreach (var kv in vals) SetField (kv.Key, Convert.ToString (kv.Value, CultureInfo.InvariantCulture), 0);
					string op = S (a, "operation"); if (op != null) SetOption ("Operation", op);
				}
				catch { Core.fc_tool (T.SELECT, 0); throw; }		// (nothing left half made)
				Core.fc_cmd (Cmd.OK);
			});
			var r = Done (); int after = UI (() => Core.State ().Feats);
			if (after == before) throw new Fail ("Not made: " + UI (() => Core.Hint));
			return r;
		}
		static Dictionary<string, object> Export (Dictionary<string, object> a)
		{
			string[] FMT = { "stl", "obj", "dxf", "svg", "pdf", "png" }, VIEWS = { "front", "back", "left", "right", "top", "bottom", "screen" };
			string path = S (a, "path") ?? throw new Fail ("path?");
			string ext = Path.GetExtension (path).TrimStart ('.').ToLowerInvariant ();
			int fmt = Array.IndexOf (FMT, (S (a, "format") ?? ext).ToLowerInvariant ()); if (fmt < 0) throw new Fail ("format: stl, obj, dxf, svg, pdf or png");
			if (ext != FMT[fmt]) path += "." + FMT[fmt];
			int view = Array.IndexOf (VIEWS, S (a, "view", "front").ToLowerInvariant ()); if (view < 0) throw new Fail ("view: " + string.Join (", ", VIEWS));
			int curve = Array.IndexOf (new[] { "draft", "fine", "very fine" }, S (a, "curves", "fine").ToLowerInvariant ()); if (curve < 0) curve = 1;
			bool body = S (a, "what", "part") == "body";
			string full = Path.GetFullPath (path);
			var info = UI (() =>
			{
				if (body && Core.State ().SelBody < 0) throw new Fail ("No body is selected.");
				var o = Core.Export (fmt, body ? 1 : 0, curve, view, B (a, "binary", true), B (a, "hidden_edges"), Path.GetFileNameWithoutExtension (full));
				if (o[4] <= 0) throw new Fail ("There is nothing to export yet.");
				if (!Core.ExportWrite (full)) throw new Fail ("The file could not be written.");
				return o;
			});
			return Text (Json.Serialize (new Dictionary<string, object> { ["written"] = full, ["bytes"] = info[4], ["triangles"] = info[0], ["lines"] = info[1], ["closed_solid"] = info[5] != 0 }));
		}
		static Dictionary<string, object> Gcode (string path)
		{
			if (path == null) throw new Fail ("path?");
			string full = Path.GetFullPath (path); if (Path.GetExtension (full).Length == 0) full += ".nc";
			return UI (() =>
			{
				var st = Core.State (); if (!st.CamMode || st.Gen != 0) throw new Fail ("Manufacture with the milling process first (command manufacture, process_milling).");
				Core.fc_cmd (Cmd.GCODE);
				bool good = Core.Gcode (out string checks, out string sum);
				if (!good) throw new Fail ("Not written:\n" + checks);
				if (!Core.GcodeWrite (full)) throw new Fail ("The file could not be written.");
				return Text ("Written " + full + "\n" + checks + sum);
			});
		}
		static Dictionary<string, object> PrintFile (string path)
		{
			if (path == null) throw new Fail ("path?");
			if (!UI (() => Core.State ().Resin)) throw new Fail ("Manufacture with the resin process first (command manufacture, process_resin).");
			UI (() => { if (!Core.State ().Sliced) Core.fc_cmd (Cmd.PR_LAYERS); });
			for (int i = 0; i < 1200 && !UI (() => Core.State ().Sliced); i++) Thread.Sleep (100);	// (the layers are cut a few at each frame)
			return UI (() =>
			{
				if (!Core.State ().Sliced) throw new Fail ("The layers are not cut yet: " + Core.Hint);
				bool good = Core.PrintFile (out string checks, out string sum, out string ext);
				string full = Path.GetFullPath (path); if (!full.EndsWith ("." + ext, StringComparison.OrdinalIgnoreCase)) full += "." + ext;
				if (!good) throw new Fail ("Not written:\n" + checks);
				if (!Core.PrintFileWrite (full)) throw new Fail ("The file could not be written.");
				return Text ("Written " + full + "\n" + checks + sum);
			});
		}
		// The window's picture (or the view's alone), PNG in base64: drawn off the screen, whatever covers the window.
		static string Shoot (string what)
		{
			FrameworkElement el = what == "view" ? (FrameworkElement) W.View : (FrameworkElement) W.Content;
			W.SyncNow (); W.UpdateLayout ();		// (what changed drawn first: the timeline, the panel)
			int pw = Math.Max (1, (int) Math.Ceiling (el.ActualWidth)), ph = Math.Max (1, (int) Math.Ceiling (el.ActualHeight));
			var rt = new RenderTargetBitmap (pw, ph, 96, 96, PixelFormats.Pbgra32);
			var dv = new DrawingVisual ();
			using (var dc = dv.RenderOpen ()) { dc.DrawRectangle (W.Background, null, new Rect (0, 0, pw, ph)); dc.DrawRectangle (new VisualBrush (el) { Stretch = Stretch.None, AlignmentX = AlignmentX.Left, AlignmentY = AlignmentY.Top }, null, new Rect (0, 0, pw, ph)); }
			rt.Render (dv);
			var enc = new PngBitmapEncoder (); enc.Frames.Add (BitmapFrame.Create (rt));
			using (var ms = new MemoryStream ()) { enc.Save (ms); return Convert.ToBase64String (ms.ToArray ()); }
		}
	}
}
