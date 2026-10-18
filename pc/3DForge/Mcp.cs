// Mcp.cs -- `3DForge.exe --mcp`: a Model Context Protocol server (JSON-RPC 2.0 over stdin / stdout, a message a line)
// through which an assistant drives 3DForge: it starts no window of its own -- it hands each tool's call to the
// 3DForge window open on this PC (Control.cs, the pipe \\.\pipe\3DForge), and starts one if there is none.
//   claude mcp add 3dforge -- "C:\...\3DForge.exe" --mcp
// MIT licence (Onyx).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Reflection;
using System.Text;
using System.Threading;


namespace Forge
{
	static class Mcp
	{
		static readonly JsonCodec Json = new JsonCodec ();
		static NamedPipeClientStream pipe; static StreamReader pr; static StreamWriter pw;

		const string GUIDE = "3DForge is a small parametric CAD (millimetres, Z up, the ground is XY). A part is a history of steps "
			+ "(shapes, sketches, extrusions, fillets, moves, unions...) replayed by a solid kernel; every step keeps values that can be "
			+ "changed later. Work as a user would: get_state and get_panel say where things stand; set_tool picks a tool; the panel's "
			+ "fields (set_field) give its values; command \"ok\" makes the step. make_shape does all that for a shape at once. A sketch: "
			+ "set_tool sketch, set_option Plane XY/XZ/YZ, set_field Offset, command start_sketch, then draw with the sketch tools by "
			+ "clicking in the view (view_mouse, pixels of the view: get_state's view_size; command view_top helps), close_outline, "
			+ "finish_sketch; then set_tool extrude, set_field Height, command ok. Look with screenshot after each important step.";

		static object Tool (string name, string desc, object props = null, string[] req = null)
		{
			var schema = new Dictionary<string, object> { ["type"] = "object", ["properties"] = props ?? new Dictionary<string, object> () };
			if (req != null) schema["required"] = req;
			return new Dictionary<string, object> { ["name"] = name, ["description"] = desc, ["inputSchema"] = schema };
		}
		static Dictionary<string, object> P (params object[] kv)
		{
			var d = new Dictionary<string, object> ();
			for (int i = 0; i + 2 < kv.Length + 1; i += 3) d[(string) kv[i]] = new Dictionary<string, object> { ["type"] = kv[i + 1], ["description"] = kv[i + 2] };
			return d;
		}
		static List<object> Tools () => new List<object>
		{
			Tool ("get_state", "Where things stand: the mode (design, sketch, manufacture), the tool and its step, the hint the window shows, the file, the bodies, the history (or the sketch's elements, or Manufacture's operations), the view's size in pixels."),
			Tool ("get_panel", "The panel at the right as a list: its fields (with their values and units), check boxes, choices and their options, buttons, measures, checks. What set_field, set_option and press act on."),
			Tool ("screenshot", "A picture of the 3DForge window (or of its 3D view alone), drawn whatever covers it on the screen.", P ("what", "string", "\"window\" (default) or \"view\"")),
			Tool ("command", "A command of the window. Names: " + Control.CommandNames + ".", P ("name", "string", "the command"), new[] { "name" }),
			Tool ("set_tool", "Pick a tool (as clicking it in the bar). Tools: " + Control.ToolNames + " (line .. point: while a sketch is drawn).", P ("tool", "string", "the tool"), new[] { "tool" }),
			Tool ("set_field", "Type a value in a field of the panel, by the field's name (get_panel). Several of the same name: index, or \"Section/Name\" (e.g. \"Scale (1: as it is)/Along X\").",
				P ("name", "string", "the field's name", "value", "string", "the value (a number in millimetres or degrees; a name for a text field)", "index", "integer", "which one of that name, from 0"), new[] { "name", "value" }),
			Tool ("set_option", "Set a check box (value true/false) or a choice of the panel (value: one of its options): e.g. Plane = XZ, Operation = Subtract, Side = Inside, Colour = 1..5, Origin = 0..26.",
				P ("label", "string", "the check box's text or the choice's name", "value", "string", "true / false, or the option"), new[] { "label", "value" }),
			Tool ("press", "Press a button of the panel by its text (OK, Cancel, Generate, Delete this step, Start the sketch...); \"Enter\" is Enter in a field.", P ("button", "string", "its text"), new[] { "button" }),
			Tool ("pick", "Choose in a list a panel's button opens: list \"body\" (the body cut or printed), \"process\" (Milling, Filament printing, Resin printing - a printer), \"resin\" (a resin's preset).",
				P ("list", "string", "body, process or resin", "item", "string", "its index, or a part of its name"), new[] { "list", "item" }),
			Tool ("view_mouse", "The pointer in the 3D view, as the user's: move, click, double_click, right_click, drag (to x2, y2; button left turns the view, right pans), wheel (notches > 0 zoom in). x, y: pixels from the view's top left (get_state view_size). The tools take their clicks: a shape's centre on a face or the ground, a sketch's points, the edges to round, the bodies to combine.",
				P ("action", "string", "move, click, double_click, right_click, drag or wheel", "x", "number", "x", "y", "number", "y", "x2", "number", "drag's end x", "y2", "number", "drag's end y", "button", "string", "drag: left or right", "notches", "integer", "wheel", "shift", "boolean", "Shift held"), new[] { "action", "x", "y" }),
			Tool ("look", "Turn the view: azimuth and elevation in degrees (-90, 90: from the top; -90, 0: the front; 0, 0: the right; -58, 28: home).", P ("azimuth", "number", "degrees", "elevation", "number", "degrees")),
			Tool ("key", "A key: escape (cancel the step, end a run of lines), enter (confirm), delete (the selected step or element).", P ("key", "string", "escape, enter or delete"), new[] { "key" }),
			Tool ("timeline", "The history under the view: select a step (its values in the panel), open a sketch step, or roll_to: the part rebuilt up to that many steps (the count of steps: all).",
				P ("action", "string", "select, open or roll_to", "index", "integer", "the step (from 0), or the count for roll_to"), new[] { "index" }),
			Tool ("make_shape", "Make a shape from its values at once: box (Width, Depth, Height, Corner X/Centre X, Y, Z, Turned), cylinder (Diameter, Height or Depth), sphere (Radius), torus (Ring radius, Tube radius), pyramid / prism (Sides, Base radius, Height), taper (+ Top radius); Centre X, Y, Z place it. operation: New body, Union, Subtract, Intersect (with the selected body).",
				new Dictionary<string, object> { ["shape"] = new Dictionary<string, object> { ["type"] = "string" }, ["values"] = new Dictionary<string, object> { ["type"] = "object", ["description"] = "field name -> value, e.g. {\"Width\": 40, \"Depth\": 20, \"Height\": 10}" }, ["operation"] = new Dictionary<string, object> { ["type"] = "string" } }, new[] { "shape" }),
			Tool ("new_part", "Start a new part.", P ("discard_changes", "boolean", "drop unsaved changes")),
			Tool ("open_file", "Open a part (.3df).", P ("path", "string", "its path", "discard_changes", "boolean", "drop unsaved changes"), new[] { "path" }),
			Tool ("save_file", "Save the part (.3df): to path, or to its file.", P ("path", "string", "where (optional)")),
			Tool ("add_canvas", "Lay a picture (PNG, JPEG...) on a plane, to draw over.", P ("path", "string", "the picture"), new[] { "path" }),
			Tool ("export", "Export: stl, obj (meshes); dxf, svg, pdf (a flat drawing seen from a side, at the part's size); png (a picture).",
				P ("path", "string", "the file", "format", "string", "stl obj dxf svg pdf png (else from the extension)", "what", "string", "part (default) or body (the selected one)", "curves", "string", "draft, fine, very fine", "view", "string", "front back left right top bottom screen", "binary", "boolean", "STL binary (default true)", "hidden_edges", "boolean", "drawings: hidden edges dashed"), new[] { "path" }),
			Tool ("write_gcode", "Write the G-code (GRBL) of Manufacture's milling operations, once checked.", P ("path", "string", "the .nc file"), new[] { "path" }),
			Tool ("close_window", "Close the 3DForge window (the next call starts it again).", P ("discard_changes", "boolean", "drop unsaved changes")),
			Tool ("write_print_file", "Write the resin printer's file (the layers are cut first; it takes a while).", P ("path", "string", "the file"), new[] { "path" }),
		};

		public static void Run ()
		{
			var stdin = new StreamReader (Console.OpenStandardInput (), new UTF8Encoding (false));
			var stdout = new StreamWriter (Console.OpenStandardOutput (), new UTF8Encoding (false)) { AutoFlush = true, NewLine = "\n" };
			string line;
			while ((line = stdin.ReadLine ()) != null)
			{
				if (line.Trim ().Length == 0) continue;
				Dictionary<string, object> msg;
				try { msg = Json.Deserialize<Dictionary<string, object>> (line); } catch { continue; }
				if (!msg.TryGetValue ("id", out var id)) continue;		// (a notification)
				string method = msg.TryGetValue ("method", out var m) ? (string) m : "";
				var prm = msg.TryGetValue ("params", out var pp) && pp is Dictionary<string, object> pd ? pd : new Dictionary<string, object> ();
				object result = null; Dictionary<string, object> error = null;
				switch (method)
				{
				case "initialize":
					result = new Dictionary<string, object>
					{
						["protocolVersion"] = prm.TryGetValue ("protocolVersion", out var v) ? v : "2025-06-18",
						["capabilities"] = new Dictionary<string, object> { ["tools"] = new Dictionary<string, object> () },
						["serverInfo"] = new Dictionary<string, object> { ["name"] = "3dforge", ["version"] = "1.0.0" },
						["instructions"] = GUIDE,
					};
					break;
				case "ping": result = new Dictionary<string, object> (); break;
				case "tools/list": result = new Dictionary<string, object> { ["tools"] = Tools () }; break;
				case "tools/call":
				{
					string name = prm.TryGetValue ("name", out var n) ? (string) n : "";
					var args = prm.TryGetValue ("arguments", out var a) && a is Dictionary<string, object> ad ? ad : new Dictionary<string, object> ();
					result = Call (name, args); break;
				}
				default: error = new Dictionary<string, object> { ["code"] = -32601, ["message"] = "Method not found: " + method }; break;
				}
				var reply = new Dictionary<string, object> { ["jsonrpc"] = "2.0", ["id"] = id };
				if (error != null) reply["error"] = error; else reply["result"] = result;
				stdout.WriteLine (Json.Serialize (reply));
			}
		}
		static object Call (string name, Dictionary<string, object> args)
		{
			Dictionary<string, object> r;
			try { r = Send (new Dictionary<string, object> { ["tool"] = name, ["args"] = args }); }
			catch (Exception e) { r = new Dictionary<string, object> { ["error"] = e.Message }; }
			var content = new List<object> ();
			if (r.TryGetValue ("text", out var t)) content.Add (new Dictionary<string, object> { ["type"] = "text", ["text"] = t });
			if (r.TryGetValue ("image", out var im)) content.Add (new Dictionary<string, object> { ["type"] = "image", ["data"] = im, ["mimeType"] = "image/png" });
			bool err = r.TryGetValue ("error", out var e2);
			if (err) content.Add (new Dictionary<string, object> { ["type"] = "text", ["text"] = Convert.ToString (e2) });
			return new Dictionary<string, object> { ["content"] = content, ["isError"] = err };
		}
		// The window's answer; the window started first when none is open.
		static Dictionary<string, object> Send (Dictionary<string, object> req)
		{
			for (int tries = 0; tries < 2; tries++)
			{
				if (pipe == null || !pipe.IsConnected) Connect ();
				try
				{
					pw.WriteLine (Json.Serialize (req));
					string line = pr.ReadLine ();
					if (line != null) return Json.Deserialize<Dictionary<string, object>> (line);
				}
				catch (IOException) { }
				pipe?.Dispose (); pipe = null;		// (the window was closed: again, with a new one)
			}
			throw new Exception ("3DForge's window does not answer.");
		}
		static void Connect ()
		{
			bool started = false;
			for (int i = 0; i < 100; i++)
			{
				var p = new NamedPipeClientStream (".", Control.PIPE, PipeDirection.InOut);
				try
				{
					p.Connect (started ? 300 : 200);
					pipe = p; pr = new StreamReader (p, new UTF8Encoding (false)); pw = new StreamWriter (p, new UTF8Encoding (false)) { AutoFlush = true, NewLine = "\n" };
					return;
				}
				catch (Exception) { p.Dispose (); }
				if (!started)
				{
					Process.Start (new ProcessStartInfo (Assembly.GetExecutingAssembly ().Location) { UseShellExecute = true, WorkingDirectory = AppDomain.CurrentDomain.BaseDirectory });		// (its own handles: not the MCP client's pipes)
					started = true;
				}
				Thread.Sleep (200);
			}
			throw new Exception ("3DForge could not be started.");
		}
	}
}
